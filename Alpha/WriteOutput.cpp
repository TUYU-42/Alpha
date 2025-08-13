// WriteOutput.cpp
#include "WriteOutput.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <regex>
using namespace std;

WriteOutput::WriteOutput(const string& outputName,
    const MergeMapping& mergeMap,
    const DefData& originalDefData,
    const vector<MergedFF>& mergedFFResults,
    const VerilogParser* verilogParser)
    : outputName_(outputName),
    mergeMap_(mergeMap),
    originalDefData_(originalDefData),
    mergedFFResults_(mergedFFResults),
    verilogParser_(verilogParser),
    libParser_(nullptr) {

    // 建立階層映射
    if (verilogParser_) {
        buildHierarchicalMapping();
    }
}

// 建立階層名稱映射
void WriteOutput::buildHierarchicalMapping() {
    if (!verilogParser_) return;

    // 從 VerilogParser 取得階層映射
    hierarchicalMapping_ = verilogParser_->getHierarchicalNameMapping();

    // 建立反向映射
    for (const auto& pair : hierarchicalMapping_) {
        reverseMapping_[pair.second] = pair.first;
    }

    cout << "[WriteOutput] Built hierarchical mapping with "
        << hierarchicalMapping_.size() << " entries" << endl;
}

// 取得完整路徑
string WriteOutput::getFullPath(const string& localName) const {
    auto it = hierarchicalMapping_.find(localName);
    if (it != hierarchicalMapping_.end()) {
        return it->second;
    }
    return localName; // 如果找不到，返回原名
}

// 取得局部名稱
string WriteOutput::getLocalName(const string& fullPath) const {
    auto it = reverseMapping_.find(fullPath);
    if (it != reverseMapping_.end()) {
        return it->second;
    }

    // 嘗試從路徑提取最後一部分
    size_t lastSlash = fullPath.find_last_of('/');
    if (lastSlash != string::npos) {
        return fullPath.substr(lastSlash + 1);
    }

    return fullPath;
}

// 判斷是否為 FF instance（使用 LibParser）
bool WriteOutput::isFlipFlopInstance(const string& cellType) const {
    if (!libParser_) {
        // 如果沒有 LibParser，使用名稱模式判斷
        return (cellType.find("FF") != string::npos ||
            cellType.find("FSDN") != string::npos ||
            cellType.find("FSDNQ") != string::npos ||
            cellType.find("DFF") != string::npos);
    }

    // 使用 LibParser 判斷
    const LibCell* cell = libParser_->getCell(cellType);
    if (!cell) return false;

    // 檢查是否為 FF
    return cell->hasFF || !cell->singleBitDegenerate.empty() ||
        cell->name.find("FF") != string::npos ||
        cell->name.find("FSDN") != string::npos;
}
string WriteOutput::generateMBFFInstance(const MergedFF& mergedFF,
    const VerilogInstance& origInst) const {
    stringstream ss;

    // 取得 MBFF 的 LibCell 資訊
    const LibCell* mbffCell = nullptr;
    if (libParser_) {
        mbffCell = libParser_->getCell(mergedFF.mbffType);
    }

    ss << mergedFF.mbffType << " " << mergedFF.newInstanceName << " (\n";

    // 收集所有的 pin 連接
    vector<pair<string, string>> pinConnections;

    // 處理每個被合併的 FF 的連接
    for (size_t bitIdx = 0; bitIdx < mergedFF.mergedFFs.size(); ++bitIdx) {
        const string& singleFFName = mergedFF.mergedFFs[bitIdx];

        // 找到原始 FF 的連接資訊
        const VerilogInstance* sourceInst = nullptr;

        // 在 instances 中查找
        for (const auto& inst : verilogParser_->getInstances()) {
            if (inst.instName == singleFFName ||
                ("\\" + inst.instName + " ") == singleFFName ||
                inst.instName == ("\\" + singleFFName + " ")) {
                sourceInst = &inst;
                break;
            }
        }

        if (sourceInst) {
            // 從原始 instance 取得連接
            string dNet = "UNCONNECTED";
            string qNet = "UNCONNECTED";

            for (const auto& conn : sourceInst->connections) {
                if (conn.first == "D") dNet = conn.second;
                else if (conn.first == "Q") qNet = conn.second;
            }

            // 根據 MBFF 的 pin 格式產生連接
            if (mbffCell && mbffCell->hasBundle("D")) {
                pinConnections.push_back({ "D[" + to_string(bitIdx) + "]", dNet });
            }
            else {
                pinConnections.push_back({ "D" + to_string(bitIdx), dNet });
            }

            if (mbffCell && mbffCell->hasBundle("Q")) {
                pinConnections.push_back({ "Q[" + to_string(bitIdx) + "]", qNet });
            }
            else {
                pinConnections.push_back({ "Q" + to_string(bitIdx), qNet });
            }

            // 處理第一個和最後一個 FF 的 scan pins
            if (bitIdx == 0) {
                for (const auto& conn : sourceInst->connections) {
                    if (conn.first == "SI" && conn.second != "UNCONNECTED") {
                        pinConnections.push_back({ "SI", conn.second });
                        break;
                    }
                }
            }

            if (bitIdx == mergedFF.mergedFFs.size() - 1) {
                for (const auto& conn : sourceInst->connections) {
                    if (conn.first == "SO" && conn.second != "UNCONNECTED") {
                        pinConnections.push_back({ "SO", conn.second });
                        break;
                    }
                }
            }
        }
    }

    // 處理共用的 pins（從第一個 FF 取得）
    if (!mergedFF.mergedFFs.empty()) {
        const string& firstFF = mergedFF.mergedFFs[0];
        const VerilogInstance* firstInst = nullptr;

        // 找到第一個 FF 的 instance
        for (const auto& inst : verilogParser_->getInstances()) {
            if (inst.instName == firstFF ||
                ("\\" + inst.instName + " ") == firstFF ||
                inst.instName == ("\\" + firstFF + " ")) {
                firstInst = &inst;
                break;
            }
        }

        if (firstInst) {
            // Clock pin
            for (const auto& conn : firstInst->connections) {
                if (conn.first == "CK" || conn.first == "CLK") {
                    string clockPinName = "CK";
                    if (mbffCell) {
                        if (mbffCell->pins.find("CLK") != mbffCell->pins.end()) {
                            clockPinName = "CLK";
                        }
                    }
                    pinConnections.push_back({ clockPinName, conn.second });
                    break;
                }
            }

            // SE pin
            for (const auto& conn : firstInst->connections) {
                if (conn.first == "SE" && conn.second != "UNCONNECTED") {
                    pinConnections.push_back({ "SE", conn.second });
                    break;
                }
            }

            // VSS/VDD
            for (const auto& conn : firstInst->connections) {
                if (conn.first == "VSS") {
                    pinConnections.push_back({ "VSS", conn.second });
                }
                else if (conn.first == "VDD") {
                    pinConnections.push_back({ "VDD", conn.second });
                }
            }
        }
    }

    // 如果沒有 VSS/VDD，加上預設的
    bool hasVSS = false, hasVDD = false;
    for (const auto& conn : pinConnections) {
        if (conn.first == "VSS") hasVSS = true;
        if (conn.first == "VDD") hasVDD = true;
    }
    if (!hasVSS) pinConnections.push_back({ "VSS", "VSS" });
    if (!hasVDD) pinConnections.push_back({ "VDD", "VDD" });

    // 輸出所有 pin 連接
    for (size_t i = 0; i < pinConnections.size(); ++i) {
        if (i > 0) ss << ",\n";
        ss << "  ." << pinConnections[i].first
            << "(" << pinConnections[i].second << ")";
    }

    ss << "\n);\n\n";

    return ss.str();
}
// 取得 bit index
int WriteOutput::getBitIndexFromPairs(const vector<pair<int, string>>& pairs,
    const string& instanceName) const {
    for (const auto& pair : pairs) {
        if (pair.second == instanceName) {
            return pair.first;
        }
    }
    return -1;
}

// 寫入 mapping list
bool WriteOutput::writeMapList() {
    string mappingFile = outputName_ + ".list";
    ofstream mapFile(mappingFile);
    if (!mapFile.is_open()) {
        cerr << "[WriteOutput] Error: Cannot create mapping file: " << mappingFile << endl;
        return false;
    }

    cout << "\n[WriteOutput] Writing mapping list to " << mappingFile << "..." << endl;

    // 計算 FF 數量
    int originalFFCount = static_cast<int>(originalDefData_.flipFlops.size());
    int mergedSingleFFCount = static_cast<int>(mergeMap_.singleToMultiBitName.size());
    int newMultiBitFFCount = static_cast<int>(mergeMap_.multiBitToSingles.size());
    int adjustedFFCount = originalFFCount - mergedSingleFFCount + newMultiBitFFCount;

    cout << "  Original FF count: " << originalFFCount << endl;
    cout << "  Merged single FFs: " << mergedSingleFFCount << endl;
    cout << "  New multi-bit FFs: " << newMultiBitFFCount << endl;
    cout << "  Adjusted FF count: " << adjustedFFCount << endl;

    // 寫入 header
    mapFile << "CellInst " << adjustedFFCount << endl;

    // 緩存 MBFF 的 bit mapping
    unordered_map<string, vector<pair<int, string>>> mbffBitCache;

    // 處理每個 FF 的 mapping
    for (const auto& ff : originalDefData_.flipFlops) {
        string instName = ff.instName;

        // 取得完整路徑名稱
        string fullPath = getFullPath(instName);

        // 檢查是否被合併
        if (mergeMap_.isMerged(fullPath)) {
            // 被合併了
            string mbff = mergeMap_.getMergedName(fullPath);

            // 取得 MBFF 的 bit mapping
            if (mbffBitCache.find(mbff) == mbffBitCache.end()) {
                mbffBitCache[mbff] = mergeMap_.getBitIndexedPairs(mbff);
            }

            const auto& pairs = mbffBitCache[mbff];
            int bitIndex = getBitIndexFromPairs(pairs, fullPath);

            if (bitIndex == -1) {
                cerr << "  Warning: Cannot find bit index for " << fullPath
                    << " in MBFF " << mbff << endl;
                bitIndex = 0;
            }

            // 寫入 mapping
            mapFile << instName << "/D map " << mbff << "/D" << bitIndex << endl;
            mapFile << instName << "/Q map " << mbff << "/Q" << bitIndex << endl;
            mapFile << instName << "/CK map " << mbff << "/CK" << endl;

            // 處理 scan pins
            if (!ff.scanIn.empty() && ff.scanIn != "UNCONNECTED") {
                mapFile << instName << "/SI map " << mbff << "/SI" << bitIndex << endl;
            }
            if (!ff.scanOut.empty() && ff.scanOut != "UNCONNECTED") {
                mapFile << instName << "/SO map " << mbff << "/SO" << bitIndex << endl;
            }
        }
        else {
            // 沒有被合併，1對1 mapping
            mapFile << instName << "/D map " << instName << "/D" << endl;
            mapFile << instName << "/Q map " << instName << "/Q" << endl;
            mapFile << instName << "/CK map " << instName << "/CK" << endl;

            if (!ff.scanIn.empty() && ff.scanIn != "UNCONNECTED") {
                mapFile << instName << "/SI map " << instName << "/SI" << endl;
            }
            if (!ff.scanOut.empty() && ff.scanOut != "UNCONNECTED") {
                mapFile << instName << "/SO map " << instName << "/SO" << endl;
            }
        }
    }

    mapFile.close();
    cout << "  ✓ Generated " << mappingFile << endl;
    return true;
}
void WriteOutput::buildSimpleToFullNameMapping() {
    simpleToFullNameMap_.clear();

    // 從 DEF components 建立映射
    for (const auto& comp : originalDefData_.components) {
        // 提取簡單名稱（最後一個 / 之後的部分）
        string simpleName = comp.name;
        size_t lastSlash = simpleName.find_last_of('/');
        if (lastSlash != string::npos) {
            simpleName = simpleName.substr(lastSlash + 1);
        }

        // 建立映射：簡單名稱 -> 完整路徑
        simpleToFullNameMap_[simpleName] = comp.name;
    }

    // 也從 flipFlops 建立映射
    for (const auto& ff : originalDefData_.flipFlops) {
        string simpleName = ff.instName;
        size_t lastSlash = simpleName.find_last_of('/');
        if (lastSlash != string::npos) {
            simpleName = simpleName.substr(lastSlash + 1);
        }
        simpleToFullNameMap_[simpleName] = ff.instName;
    }

    cout << "[WriteOutput] Built simple-to-full name mapping with "
        << simpleToFullNameMap_.size() << " entries" << endl;

    // 除錯：顯示一些映射範例
    cout << "[Debug] Sample name mappings:" << endl;
    int count = 0;
    for (const auto& pair : simpleToFullNameMap_) {
        if (count++ >= 5) break;
        cout << "  " << pair.first << " -> " << pair.second << endl;
    }
}
// writeVerilog() 中處理模組實例化的修正部分
// 完整的 writeVerilog() 方法，包含 QN 支援
// WriteOutput::writeVerilog() 完整修改版本
bool WriteOutput::writeVerilog() {
    string outputFilename = outputName_ + ".v";

    cout << "\n[WriteOutput] Writing Verilog netlist..." << endl;
    cout << "  Output: " << outputFilename << endl;

    ofstream fout(outputFilename);
    if (!fout.is_open()) {
        cerr << "[WriteOutput] Error: Cannot create output file: " << outputFilename << endl;
        return false;
    }
    if (!verilogParser_) {
        cerr << "[WriteOutput] Error: VerilogParser not set" << endl;
        return false;
    }

    // 小工具
    auto escapeIfBus = [](std::string name) {
        if (!name.empty() && name[0] != '\\' && name.find('[') != std::string::npos)
            return std::string("\\") + name + " ";
        return name;
        };
    auto keepEscaped = [](std::string id) {
        if (!id.empty() && id[0] == '\\' && id.back() != ' ') id.push_back(' ');
        return id;
        };
    auto norm = [](std::string s) { // 去掉 leading '\' 與 trailing ' '（僅對 escaped）
        if (!s.empty() && s[0] == '\\' && !s.empty() && s.back() == ' ')
            return s.substr(1, s.size() - 2);
        return s;
        };
    auto looksLikeOutput = [](std::string n) {
        if (!n.empty() && n[0] == '\\' && n.back() == ' ') n = n.substr(1, n.size() - 2);
        return n.rfind("qo_", 0) == 0;
        };

    // 名稱映射與被合併 FF 快取
    buildSimpleToFullNameMapping();
    unordered_map<string, const MergedFF*> mergedFFLookup;
    for (const auto& mergedFF : mergedFFResults_) {
        for (const auto& singleFF : mergedFF.mergedFFs)
            mergedFFLookup[singleFF] = &mergedFF;
    }
    set<string> outputtedMBFFs, skippedInstances;

    const auto& modules = verilogParser_->getModules();

    for (const auto& module : modules) {
        // ====== 收集 nets 與宣告集合 ======
        // header ports（原樣 token，含 escaped）
        unordered_set<string> headerPorts(module.ports.begin(), module.ports.end());

        // dirMap：已知 input/output/inout 先放進來
        unordered_map<string, string> dirMap;
        for (auto& p : module.inputs)  dirMap[p] = "input";
        for (auto& p : module.outputs) dirMap[p] = "output";
        for (auto& p : module.inouts)  dirMap[p] = "inout";

        // header 有但三類未涵蓋 → 用 heuristic 補方向
        for (auto& hp : headerPorts) {
            if (!dirMap.count(hp)) dirMap[hp] = looksLikeOutput(hp) ? "output" : "input";
        }

        // 宣告的 ports（原樣 token）
        unordered_set<string> declaredPorts_raw;
        auto addSet = [&](const vector<string>& v) { for (auto& x : v) declaredPorts_raw.insert(x); };
        addSet(module.inputs);
        addSet(module.outputs);
        addSet(module.inouts);
        for (auto& hp : headerPorts) declaredPorts_raw.insert(hp);

        // 正規化版本（拿來跟 usedNets 比較）
        unordered_set<string> declaredPorts_norm;
        for (auto& s : declaredPorts_raw) declaredPorts_norm.insert(norm(s));

        // 已宣告 wires（原樣）
        unordered_set<string> declaredWires_raw;
        for (const auto& w : module.wires) declaredWires_raw.insert(w);
        for (const auto& s : module.supplies0) declaredWires_raw.insert(s);
        for (const auto& s : module.supplies1) declaredWires_raw.insert(s);

        // 正規化 wires
        unordered_set<string> declaredWires_norm;
        for (auto& s : declaredWires_raw) declaredWires_norm.insert(norm(s));

        // usedNets：從所有 instances 掃描
        std::set<std::string> usedNets; // 都是 normalized 名稱
        for (const auto& inst : module.instances) {
            for (const auto& conn : inst.connections) {
                std::string netName = conn.second;
                if (netName == "UNCONNECTED" || netName == "VSS" || netName == "VDD") continue;
                std::string netNorm = norm(netName);
                if (netNorm.empty()) continue;    // 重要：不要把空名丟進集合
                usedNets.insert(netNorm);
            }
        }
        // ====== module header ======
        fout << "module " << module.name << " ( ";
        for (size_t i = 0; i < module.ports.size(); ++i) {
            if (i) fout << " , ";
            if (i && i % 4 == 0) fout << "\n    ";
            string p = module.ports[i];
            if (p[0] != '\\' && p.find('[') != string::npos) p = "\\" + p + " ";
            fout << p;
        }
        fout << " ) ;\n\n";

        // ====== 方向宣告（覆蓋 headerPorts 全部） ======
        // 依 header 順序印，保持可讀性
        for (auto& hp : module.ports) {
            string id = keepEscaped(hp);
            std::string dir = dirMap[hp];
            fout << dirMap[hp] << " " << id << " ;\n";
            std::string rng;
            auto itW = module.portDeclWidth.find(norm(hp));
            if (itW != module.portDeclWidth.end() && !itW->second.empty())
                rng = itW->second + " ";

            // 輸出時把寬度插在方向與名稱之間
            fout << dir << " " << rng << id << " ;\n";
        }
        fout << "\n";

        // ====== supply0/supply1：放在方向宣告後、wire 前 ======
        for (const auto& n : module.supplies0) fout << "supply0 " << keepEscaped(escapeIfBus(n)) << " ;\n";
        for (const auto& n : module.supplies1) fout << "supply1 " << keepEscaped(escapeIfBus(n)) << " ;\n";
        if (!module.supplies0.empty() || !module.supplies1.empty()) fout << "\n";

        // ====== 原有 wires：先過濾掉 ports 與 supplies ======
        vector<string> filteredWires;
        filteredWires.reserve(module.wires.size());
        for (auto& w : module.wires) {
            if (declaredPorts_raw.count(w)) continue;          // 不把 port 印成 wire
            if (!module.supplies0.empty() && module.supplies0.end() != find(module.supplies0.begin(), module.supplies0.end(), w)) continue;
            if (!module.supplies1.empty() && module.supplies1.end() != find(module.supplies1.begin(), module.supplies1.end(), w)) continue;
            filteredWires.push_back(w);
        }
        for (const auto& w : filteredWires) {
            string wn = w;
            if (wn[0] != '\\' && wn.find('[') != string::npos) wn = "\\" + wn + " ";
            fout << "wire " << wn << " ;\n";
        }

        // ====== 需要補充的 wires（用 normalized 比對） ======
      // ====== 需要補充的 wires（用 normalized 比對 + 再次防空） ======
        std::vector<std::string> additionalWires;
        for (const auto& net : usedNets) {
            if (net.empty()) continue;                    // 保護：避免空名
            if (net == "VDD" || net == "VSS") continue;  // 跳過電源
            if (!declaredPorts_norm.count(net) && !declaredWires_norm.count(net)) {
                additionalWires.push_back(net);
                declaredWires_norm.insert(net);
            }
        }
        if (!additionalWires.empty()) {
            if (!filteredWires.empty()) fout << "\n";
            fout << "// Additional wires for connections\n";
            for (const auto& w : additionalWires) {
                std::string wn = (w.find('[') != std::string::npos && (w.empty() || w[0] != '\\'))
                    ? ("\\" + w + " ")
                    : w;
                fout << "wire " << wn << " ;\n";
            }
            fout << "\n";
        }


        // ====== assign 語句（保持原樣） ======
        for (const auto& stmt : module.assignStatements) fout << stmt << "\n";
        if (!module.assignStatements.empty()) fout << "\n";

        // ====== instances（沿用你原本的處理：module instance / cell / MBFF） ======
        for (const auto& inst : module.instances) {
            if (inst.isModuleInstance) {
                // 模組實例化：名稱與 pin/net 的 escaped 保持一致
                string instNameOutput = inst.instName;
                bool needEscape = (instNameOutput.find('[') != string::npos || instNameOutput.find(']') != string::npos || instNameOutput.find("__") != string::npos);
                if (instNameOutput.size() && instNameOutput[0] == '\\') instNameOutput = keepEscaped(instNameOutput);
                else if (needEscape) instNameOutput = "\\" + instNameOutput + " ";

                fout << inst.cellType << " " << instNameOutput << " ( ";
                bool first = true; int cnt = 0;
                for (auto& kv : inst.connections) {
                    if (!first) { fout << " , "; if (++cnt % 3 == 0) fout << "\n    "; }
                    first = false;
                    string pin = kv.first;
                    if (pin.size() && pin[0] == '\\') pin = keepEscaped(pin);
                    else if (pin.find('[') != string::npos || pin.find(']') != string::npos) pin = "\\" + pin + " ";
                    string net = kv.second;
                    if (net != "VDD" && net != "VSS" && net != "UNCONNECTED") {
                        if (net.size() && net[0] == '\\') net = keepEscaped(net);
                        else if (net.find('[') != string::npos || net.find(']') != string::npos) net = "\\" + net + " ";
                    }
                    fout << "." << pin << " ( " << net << " )";
                }
                fout << " ) ;\n";
                continue;
            }

            // 以下沿用你原本邏輯：FF 合併略過舊 inst，輸出 MBFF；其他 cell 直接原樣輸出
            string fullName = inst.instName;
            auto itmap = simpleToFullNameMap_.find(inst.instName);
            if (itmap != simpleToFullNameMap_.end()) fullName = itmap->second;

            bool shouldSkip = false;
            string mbffName;
            const MergedFF* mergedFFPtr = nullptr;

            if (isFlipFlopInstance(inst.cellType) && mergeMap_.isMerged(fullName)) {
                shouldSkip = true;
                mbffName = mergeMap_.getMergedName(fullName);
                auto it = mergedFFLookup.find(fullName);
                if (it != mergedFFLookup.end()) mergedFFPtr = it->second;
            }

            if (shouldSkip) {
                skippedInstances.insert(inst.instName);
                if (mergedFFPtr && !outputtedMBFFs.count(mbffName)) {
                    // 取簡名 + 逃逸
                    string mbffSimple = mbffName;
                    size_t slash = mbffSimple.find_last_of('/');
                    if (slash != string::npos) mbffSimple = mbffSimple.substr(slash + 1);
                    if (mbffSimple.find('[') != string::npos || mbffSimple.find("__") != string::npos) mbffSimple = "\\" + mbffSimple + " ";

                    fout << mergedFFPtr->mbffType << " " << mbffSimple << " ( ";

                    // 收 pin 連接（維持你原本的規則，含 D/Q/QN/CLK/SI/SE/VDD/VSS）
                    vector<pair<string, string>> conns;
                    const LibCell* mbffCell = (libParser_ ? libParser_->getCell(mergedFFPtr->mbffType) : nullptr);

                    // D/Q/QN bundles
                    for (size_t bitIdx = 0; bitIdx < mergedFFPtr->mergedFFs.size(); ++bitIdx) {
                        string singleFF = mergedFFPtr->mergedFFs[bitIdx];
                        string simple = singleFF; size_t s = simple.find_last_of('/'); if (s != string::npos) simple = simple.substr(s + 1);

                        // 在本 module.instances 找原 FF 連接
                        for (const auto& o : module.instances) if (o.instName == simple) {
                            for (const auto& k : o.connections) {
                                auto pushBundle = [&](const char* bname, const char* pinPrefix) {
                                    string pin = string(pinPrefix) + to_string(bitIdx);
                                    if (mbffCell && mbffCell->hasBundle(bname)) {
                                        auto m = mbffCell->getBundleMembers(bname);
                                        if (bitIdx < m.size()) pin = m[bitIdx];
                                    }
                                    conns.push_back({ "." + pin, k.second });
                                    };
                                if (k.first == "D")  pushBundle("D", "D");
                                if (k.first == "Q")  pushBundle("Q", "Q");
                                if (k.first == "QN") pushBundle("QN", "QN");
                            }
                            break;
                        }
                    }

                    // 共用 pins（CLK/CK, SI/SE from first, SO from last）
                    if (!mergedFFPtr->mergedFFs.empty()) {
                        auto firstFF = mergedFFPtr->mergedFFs.front();
                        auto lastFF = mergedFFPtr->mergedFFs.back();
                        auto simpleFirst = firstFF; size_t s1 = simpleFirst.find_last_of('/'); if (s1 != string::npos) simpleFirst = simpleFirst.substr(s1 + 1);
                        auto simpleLast = lastFF;  size_t s2 = simpleLast.find_last_of('/');  if (s2 != string::npos)  simpleLast = simpleLast.substr(s2 + 1);

                        for (const auto& o : module.instances) if (o.instName == simpleFirst) {
                            for (const auto& k : o.connections) {
                                if (k.first == "CK" || k.first == "CLK") {
                                    string ck = "CK";
                                    if (mbffCell && mbffCell->pins.find("CLK") != mbffCell->pins.end()) ck = "CLK";
                                    conns.push_back({ "." + ck, k.second });
                                }
                                else if (k.first == "SI") conns.push_back({ ".SI", k.second });
                                else if (k.first == "SE")  conns.push_back({ ".SE", k.second });
                            }
                            break;
                        }
                        for (const auto& o : module.instances) if (o.instName == simpleLast) {
                            for (const auto& k : o.connections) if (k.first == "SO") { conns.push_back({ ".SO", k.second }); break; }
                            break;
                        }
                    }
                    conns.push_back({ ".VDD","VDD" });
                    conns.push_back({ ".VSS","VSS" });

                    // 輸出連接（每3個換行、net 需要時 escaped）
                    for (size_t i = 0;i < conns.size();++i) {
                        if (i) fout << " , ";
                        if (i && i % 3 == 0) fout << "\n    ";
                        string net = conns[i].second;
                        if (net != "VDD" && net != "VSS" &&
                            net.size() && net[0] != '\\' && net.find('[') != string::npos) net = "\\" + net + " ";
                        fout << conns[i].first << " ( " << net << " )";
                    }
                    fout << " ) ;\n";
                    outputtedMBFFs.insert(mbffName);
                }
                continue;
            }

            // 非合併 cell：原樣輸出（處理 escaped）
            string instNameOutput = inst.instName;
            if (instNameOutput.find('[') != string::npos || instNameOutput.find("__") != string::npos)
                instNameOutput = "\\" + instNameOutput + " ";
            fout << inst.cellType << " " << instNameOutput << " ( ";
            for (size_t i = 0;i < inst.connections.size();++i) {
                if (i) fout << " , ";
                if (i && i % 3 == 0) fout << "\n    ";
                string net = inst.connections[i].second;
                if (net != "VDD" && net != "VSS" &&
                    net.size() && net[0] != '\\' && net.find('[') != string::npos) net = "\\" + net + " ";
                fout << "." << inst.connections[i].first << " ( " << net << " )";
            }
            fout << " ) ;\n";
        }

        fout << "\nendmodule\n\n";
    }

    fout.close();
    cout << "  ✓ Verilog netlist written successfully" << endl;
    cout << "    - Skipped instances: " << skippedInstances.size() << endl;
    cout << "    - Generated MBFFs: " << outputtedMBFFs.size() << endl;
    return true;
}

// 產生 instance 字串
string WriteOutput::generateInstanceString(const VerilogInstance& inst) const {
    stringstream ss;

    ss << inst.cellType << " " << inst.instName << " (\n";

    for (size_t i = 0; i < inst.connections.size(); ++i) {
        if (i > 0) ss << ",\n";
        ss << "  ." << inst.connections[i].first
            << "(" << inst.connections[i].second << ")";
    }

    ss << "\n);\n\n";

    return ss.str();
}

// 產生 MBFF instance
string WriteOutput::generateMBFFInstance(const MergedFF& mergedFF) const {
    stringstream ss;

    // 取得 MBFF 的 LibCell 資訊
    const LibCell* mbffCell = nullptr;
    if (libParser_) {
        mbffCell = libParser_->getCell(mergedFF.mbffType);
    }

    ss << mergedFF.mbffType << " " << mergedFF.newInstanceName << " (\n";

    // 取得所有 pin 連接
    auto pinConnections = getMBFFPinConnections(mergedFF);

    // 輸出 pin 連接
    for (size_t i = 0; i < pinConnections.size(); ++i) {
        if (i > 0) ss << ",\n";
        ss << "  ." << pinConnections[i].first
            << "(" << pinConnections[i].second << ")";
    }

    ss << "\n);\n\n";

    return ss.str();
}

// 取得 MBFF 的 pin 連接

vector<pair<string, string>> WriteOutput::getMBFFPinConnections(const MergedFF& mergedFF) const {
    vector<pair<string, string>> connections;
    const LibCell* mbffCell = nullptr;
    if (libParser_) {
        mbffCell = libParser_->getCell(mergedFF.mbffType);
    }

    // === 1. 處理 D bundle ===
    if (mbffCell) {
        if (mbffCell->hasBundle("D")) {
            auto members = mbffCell->getBundleMembers("D");
            for (size_t i = 0; i < members.size() && i < mergedFF.mergedFFs.size(); ++i) {
                const string& pinName = members[i]; // e.g. D0, D1
                const string& singleFF = mergedFF.mergedFFs[i];
                string netName = findNetForPin(singleFF, "D");
                if (netName.empty() || netName == "UNCONNECTED") {
                    netName = "UNCONNECTED";
                }
                connections.push_back({ pinName, netName });
            }
        }
        else {
            // 沒有 bundle，使用 D0, D1... 格式
            for (size_t i = 0; i < mergedFF.mergedFFs.size(); ++i) {
                string pinName = "D" + to_string(i);
                string netName = findNetForPin(mergedFF.mergedFFs[i], "D");
                if (netName.empty()) netName = "UNCONNECTED";
                connections.push_back({ pinName, netName });
            }
        }

        // === 2. 處理 Q bundle ===
        if (mbffCell->hasBundle("Q")) {
            auto members = mbffCell->getBundleMembers("Q");
            for (size_t i = 0; i < members.size() && i < mergedFF.mergedFFs.size(); ++i) {
                const string& pinName = members[i]; // e.g. Q0, Q1
                const string& singleFF = mergedFF.mergedFFs[i];
                string netName = findNetForPin(singleFF, "Q");
                if (netName.empty() || netName == "UNCONNECTED") {
                    netName = "UNCONNECTED";
                }
                connections.push_back({ pinName, netName });
            }
        }
        else {
            // 檢查是否有個別的 Q pins
            for (size_t i = 0; i < mergedFF.mergedFFs.size(); ++i) {
                string pinName = "Q" + to_string(i);
                if (mbffCell->pins.find(pinName) != mbffCell->pins.end()) {
                    string netName = findNetForPin(mergedFF.mergedFFs[i], "Q");
                    if (netName.empty()) netName = "UNCONNECTED";
                    connections.push_back({ pinName, netName });
                }
            }
        }

        // === 3. 處理 QN bundle (新增) ===
        if (mbffCell->hasBundle("QN")) {
            auto members = mbffCell->getBundleMembers("QN");
            for (size_t i = 0; i < members.size() && i < mergedFF.mergedFFs.size(); ++i) {
                const string& pinName = members[i]; // e.g. QN0, QN1
                const string& singleFF = mergedFF.mergedFFs[i];

                // 嘗試找到原始 FF 的 QN 連接
                string netName = findNetForPin(singleFF, "QN");

                // 如果原始 FF 沒有 QN pin 或未連接，可以選擇保持未連接
                // 或者根據需求產生反向訊號（這需要額外的邏輯）
                if (!netName.empty() && netName != "UNCONNECTED") {
                    connections.push_back({ pinName, netName });
                }
                // 注意：如果原始單位元 FF 沒有 QN，但 MBFF 有 QN，
                // 可能需要特殊處理（例如留空或產生新的 net）
            }
        }
        else {
            // 檢查是否有個別的 QN pins（不在 bundle 中）
            for (size_t i = 0; i < mergedFF.mergedFFs.size(); ++i) {
                string pinName = "QN" + to_string(i);
                if (mbffCell && mbffCell->pins.find(pinName) != mbffCell->pins.end()) {
                    string netName = findNetForPin(mergedFF.mergedFFs[i], "QN");
                    if (!netName.empty() && netName != "UNCONNECTED") {
                        connections.push_back({ pinName, netName });
                    }
                }
            }
        }
    }
    else {
        // 沒有 lib info 的 fallback 處理
        for (size_t i = 0; i < mergedFF.mergedFFs.size(); ++i) {
            connections.push_back({ "D" + to_string(i), findNetForPin(mergedFF.mergedFFs[i], "D") });
            connections.push_back({ "Q" + to_string(i), findNetForPin(mergedFF.mergedFFs[i], "Q") });

            // 嘗試處理 QN
            string qnNet = findNetForPin(mergedFF.mergedFFs[i], "QN");
            if (!qnNet.empty() && qnNet != "UNCONNECTED") {
                connections.push_back({ "QN" + to_string(i), qnNet });
            }
        }
    }

    // === 4. 處理 scan pins ===
    for (size_t bitIdx = 0; bitIdx < mergedFF.mergedFFs.size(); ++bitIdx) {
        const string& singleFFName = mergedFF.mergedFFs[bitIdx];
        if (bitIdx == 0) {
            string siNet = findNetForPin(singleFFName, "SI");
            if (!siNet.empty() && siNet != "UNCONNECTED") {
                connections.push_back({ "SI", siNet });
            }
        }
        if (bitIdx == mergedFF.mergedFFs.size() - 1) {
            string soNet = findNetForPin(singleFFName, "SO");
            if (!soNet.empty() && soNet != "UNCONNECTED") {
                connections.push_back({ "SO", soNet });
            }
        }
    }

    // === 5. 處理共用 pins: clock, SE, reset, VSS, VDD ===
    if (!mergedFF.mergedFFs.empty()) {
        const string& firstFF = mergedFF.mergedFFs[0];

        // Clock pin
        string clkNet = findNetForPin(firstFF, "CK");
        if (clkNet.empty()) clkNet = findNetForPin(firstFF, "CLK");
        if (!clkNet.empty() && clkNet != "UNCONNECTED") {
            if (mbffCell) {
                if (mbffCell->pins.find("CK") != mbffCell->pins.end()) {
                    connections.push_back({ "CK", clkNet });
                }
                else if (mbffCell->pins.find("CLK") != mbffCell->pins.end()) {
                    connections.push_back({ "CLK", clkNet });
                }
            }
            else {
                connections.push_back({ "CK", clkNet });
            }
        }

        // SE pin
        string seNet = findNetForPin(firstFF, "SE");
        if (!seNet.empty() && seNet != "UNCONNECTED") {
            connections.push_back({ "SE", seNet });
        }

        // Reset pins (RN, RESETN, RST)
        if (mbffCell) {
            for (const auto& pinPair : mbffCell->pins) {
                const string& pinName = pinPair.first;
                if (pinName == "RN" || pinName == "RESETN" || pinName == "RST") {
                    string rstNet = findNetForPin(firstFF, pinName);
                    if (!rstNet.empty() && rstNet != "UNCONNECTED") {
                        connections.push_back({ pinName, rstNet });
                    }
                }
            }
        }

        // Power pins
        connections.push_back({ "VSS", "VSS" });
        connections.push_back({ "VDD", "VDD" });
    }

    return connections;
}

// 找到 pin 的 net 連接
string WriteOutput::findNetForPin(const string& ffName, const string& pinName) const {
    // 從 VerilogParser 查找
    if (verilogParser_) {
        // 取得完整路徑
        string fullPath = getFullPath(ffName);

        // 查找 instance
        const VerilogInstance* inst = verilogParser_->findInstanceByHierarchicalPath(fullPath);
        if (!inst) {
            inst = verilogParser_->findInstance(ffName);
        }

        if (inst) {
            for (const auto& conn : inst->connections) {
                if (conn.first == pinName) {
                    return conn.second;
                }
            }
        }
    }

    // 從 DefData 查找
    for (const auto& net : originalDefData_.nets) {
        for (const auto& conn : net.connections) {
            if (conn.instance == ffName && conn.pin == pinName) {
                return net.name;
            }
        }
    }

    // 從 instPinNets 查找
    for (const auto& ipn : originalDefData_.instPinNets) {
        if (ipn.inst == ffName && ipn.pin == pinName) {
            return ipn.net;
        }
    }

    return "UNCONNECTED";
}

// 寫入 DEF
bool WriteOutput::writeDef() {
    string outputFilename = outputName_ + ".def";
    ofstream defFile(outputFilename);
    if (!defFile.is_open()) {
        std::cerr << "[WriteOutput] Error: Cannot create DEF file: " << outputFilename << std::endl;

    }


    std::cout << "\n[WriteOutput] Writing DEF file to " << outputFilename << "..." << std::endl;

    try {
        // 複製一份 DefData 來修改
        DefData defDataCopy = originalDefData_;

        // ===========================
        // STEP 1: 處理 NETS 部分
        // ===========================
        std::set<std::string> mergedFFSet;
        for (const auto& mbff : mergedFFResults_) {
            for (const auto& ff : mbff.mergedFFs)
                mergedFFSet.insert(ff);
        }

        // 1.2 建立舊FF instance/pin -> 新MBFF instance/pin 的 mapping
        std::map<std::pair<std::string, std::string>, std::pair<std::string, std::string>> ffPinMap;
        for (const auto& mbff : mergedFFResults_) {
            for (size_t i = 0; i < mbff.mergedFFs.size(); ++i) {
                // D/Q
                ffPinMap[{mbff.mergedFFs[i], "D"}] = { mbff.newInstanceName, "D" + std::to_string(i) };
                ffPinMap[{mbff.mergedFFs[i], "Q"}] = { mbff.newInstanceName, "Q" + std::to_string(i) };
                // CLK (or CK)
                ffPinMap[{mbff.mergedFFs[i], "CK"}] = { mbff.newInstanceName, "CK" };
                ffPinMap[{mbff.mergedFFs[i], "CLK"}] = { mbff.newInstanceName, "CK" };
                // SI 只在第0位
                if (i == 0)
                    ffPinMap[{mbff.mergedFFs[i], "SI"}] = { mbff.newInstanceName, "SI" };
                // SO 只在最後一位
                if (i == mbff.mergedFFs.size() - 1)
                    ffPinMap[{mbff.mergedFFs[i], "SO"}] = { mbff.newInstanceName, "SO" };
            }
        }

        // 1.3 用 mapping 逐 net/pin 替換
        std::vector<NetInfo> newNets;
        for (const auto& net : defDataCopy.nets) {
            NetInfo n = net;
            std::vector<NetPin> newConnections;
            for (const auto& np : net.connections) {
                auto it = ffPinMap.find({ np.instance, np.pin });
                if (it != ffPinMap.end()) {
                    // FF 被merge，改成 MBFF/pin
                    newConnections.push_back({ it->second.first, it->second.second });
                }
                else if (mergedFFSet.count(np.instance) == 0) {
                    // 非merge FF/其他cell，保留
                    newConnections.push_back(np);
                }
                // 否則忽略（例如非首位的 SI/SO 不補）
            }
            // 移除重複
            std::set<std::pair<std::string, std::string>> exist;
            std::vector<NetPin> uniq;
            for (auto& x : newConnections) {
                if (exist.insert({ x.instance, x.pin }).second)
                    uniq.push_back(x);
            }
            if (!uniq.empty()) {
                n.connections = uniq;
                newNets.push_back(n);
            }
        }

        defDataCopy.nets = newNets;
        // ===========================
        // STEP 2: 處理 COMPONENTS 部分
        // ===========================

        // 2.1 建立新的 components list
        std::vector<ComponentInfo> newComponents;

        // 2.2 保留未被合併的 components
        for (const auto& comp : defDataCopy.components) {
            if (mergedFFSet.find(comp.name) == mergedFFSet.end()) {
                newComponents.push_back(comp);
            }
        }

        // 2.3 加入新的 MBFF components
        for (const auto& mbff : mergedFFResults_) {
            ComponentInfo newComp;
            newComp.name = mbff.newInstanceName;
            newComp.cellType = mbff.mbffType;
            newComp.x = mbff.newX;
            newComp.y = mbff.newY;
            newComp.orient = mbff.orientation;
            newComponents.push_back(newComp);
        }

        // 更新 components
        defDataCopy.components = newComponents;

        // ===========================
        // STEP 3: 寫入 DEF 檔案
        // ===========================

        // 3.1 寫入 header
        defFile << "VERSION 5.8 ;" << std::endl;
        defFile << "DIVIDERCHAR \"/\" ;" << std::endl;
        defFile << "BUSBITCHARS \"[]\" ;" << std::endl;
        defFile << "DESIGN top ;" << std::endl;
        defFile << "UNITS DISTANCE MICRONS " << defDataCopy.units << " ;" << std::endl;

        // Property definitions
        defFile << "PROPERTYDEFINITIONS" << std::endl;
        defFile << "COMPONENTPIN ACCESS_DIRECTION STRING ;" << std::endl;
        defFile << "END PROPERTYDEFINITIONS" << std::endl;

        // Die area
        defFile << "DIEAREA ( "
            << defDataCopy.dieArea.xMin << " " << defDataCopy.dieArea.yMin << " ) ( "
            << defDataCopy.dieArea.xMin << " " << defDataCopy.dieArea.yMax << " ) ( "
            << defDataCopy.dieArea.xMax << " " << defDataCopy.dieArea.yMax << " ) ( "
            << defDataCopy.dieArea.xMax << " " << defDataCopy.dieArea.yMin << " ) ;" << std::endl;
        defFile << std::endl;

        // 3.2 寫入 ROWS
        for (const auto& row : defDataCopy.rows) {
            defFile << "ROW " << row.name << " " << row.siteName << " "
                << row.x << " " << row.y << " " << row.orientation
                << " DO " << row.count << " BY " << row.by
                << " STEP " << row.stepX << " " << row.stepY << " ;" << std::endl;
        }
        defFile << std::endl;

        // 3.3 寫入 TRACKS
        for (const auto& track : defDataCopy.tracks) {
            defFile << "TRACKS " << track.direction << " " << track.start
                << " DO " << track.count << " STEP " << track.step
                << " LAYER " << track.layer << " ;" << std::endl;
        }
        defFile << std::endl;

        // 3.4 寫入 COMPONENTS
        defFile << "COMPONENTS " << newComponents.size() << " ;" << std::endl;
        for (const auto& comp : newComponents) {
            defFile << "- " << comp.name << " " << comp.cellType
                << " + PLACED ( " << comp.x << " " << comp.y << " ) "
                << comp.orient << " ;" << std::endl;
        }
        defFile << "END COMPONENTS" << std::endl;
        defFile << std::endl;

        // 3.5 寫入 PINS (如果有的話)
        if (!defDataCopy.pins.empty()) {
            defFile << "PINS " << defDataCopy.pins.size() << " ;" << std::endl;
            for (const auto& pin : defDataCopy.pins) {
                defFile << "- " << pin.name << " + NET " << pin.netName
                    << " + DIRECTION " << pin.direction
                    << " + USE " << pin.use;

                if (!pin.layer.empty()) {
                    defFile << " + LAYER " << pin.layer
                        << " ( " << pin.layerX1 << " " << pin.layerY1 << " ) "
                        << " ( " << pin.layerX2 << " " << pin.layerY2 << " )";
                }

                if (pin.placedX != 0 || pin.placedY != 0) {
                    defFile << " + PLACED ( " << pin.placedX << " " << pin.placedY << " ) "
                        << pin.orient;
                }

                defFile << " ;" << std::endl;
            }
            defFile << "END PINS" << std::endl;
            defFile << std::endl;
        }

        // 3.6 寫入 BLOCKAGES (如果有的話)
        if (!defDataCopy.blockages.empty()) {
            defFile << "BLOCKAGES " << defDataCopy.blockages.size() << " ;" << std::endl;
            for (const auto& blk : defDataCopy.blockages) {
                if (blk.type == DefBlockageInfo::PLACEMENT) {
                    defFile << "- PLACEMENT";
                }
                else {
                    defFile << "- LAYER " << blk.layer << " + SPACING " << blk.spacing;
                }
                defFile << " RECT ( " << blk.x1 << " " << blk.y1 << " ) ( "
                    << blk.x2 << " " << blk.y2 << " ) ;" << std::endl;
            }
            defFile << "END BLOCKAGES" << std::endl;
            defFile << std::endl;
        }

        // 3.7 寫入 NETS
        defFile << "NETS " << defDataCopy.nets.size() << " ;" << std::endl;
        for (const auto& net : defDataCopy.nets) {
            defFile << "- " << net.name << std::endl;
            for (size_t i = 0; i < net.connections.size(); ++i) {
                defFile << "  ( " << net.connections[i].instance
                    << " " << net.connections[i].pin << " )";
                if (i < net.connections.size() - 1)
                    defFile << std::endl;
            }
            if (!net.use.empty())
                defFile << std::endl << "  + USE " << net.use;
            defFile << " ;" << std::endl;
        }
        defFile << "END NETS" << std::endl;
        defFile << std::endl;

        // 3.8 複製其餘原始內容（跳過已處理的部分）
        if (!defDataCopy.originalDefLines.empty()) {
            enum State { NORMAL, SKIP_SECTION };
            State state = NORMAL;

            for (const auto& line : defDataCopy.originalDefLines) {
                // 跳過已經手動產生的部分
                if (line.find("VERSION") == 0) continue;
                if (line.find("DIVIDERCHAR") == 0) continue;
                if (line.find("BUSBITCHARS") == 0) continue;
                if (line.find("DESIGN") == 0) continue;
                if (line.find("UNITS") == 0) continue;
                if (line.find("PROPERTYDEFINITIONS") == 0) { state = SKIP_SECTION; continue; }
                if (line.find("DIEAREA") == 0) continue;
                if (line.find("ROW ") == 0) { state = SKIP_SECTION; continue; }
                if (line.find("TRACKS ") == 0) { state = SKIP_SECTION; continue; }
                if (line.find("COMPONENTS ") == 0) { state = SKIP_SECTION; continue; }
                if (line.find("PINS ") == 0) { state = SKIP_SECTION; continue; }
                if (line.find("BLOCKAGES ") == 0) { state = SKIP_SECTION; continue; }
                if (line.find("NETS ") == 0) { state = SKIP_SECTION; continue; }

                // 檢查 END 標記
                if (state == SKIP_SECTION) {
                    if (line.find("END PROPERTYDEFINITIONS") != std::string::npos ||
                        line.find("END ROWS") != std::string::npos ||
                        line.find("END TRACKS") != std::string::npos ||
                        line.find("END COMPONENTS") != std::string::npos ||
                        line.find("END PINS") != std::string::npos ||
                        line.find("END BLOCKAGES") != std::string::npos ||
                        line.find("END NETS") != std::string::npos) {
                        state = NORMAL;
                        continue;
                    }
                }

                // 輸出未被跳過的內容
                if (state == NORMAL) {
                    // 跳過空行（可選）
                    if (line.empty()) continue;

                    defFile << line << std::endl;
                }
            }
        }

        // 3.9 寫入結尾
        defFile << "END DESIGN" << std::endl;

        defFile.close();

        std::cout << "  ✓ DEF file written successfully" << std::endl;
        std::cout << "    - Components: " << newComponents.size() << std::endl;
        std::cout << "    - Nets: " << newNets.size() << std::endl;
        std::cout << "    - Merged FFs: " << mergedFFSet.size() << std::endl;
        std::cout << "    - New MBFFs: " << mergedFFResults_.size() << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[WriteOutput] Error writing DEF file: " << e.what() << std::endl;
        defFile.close();
        return false;
    }
}

// 寫入所有檔案
bool WriteOutput::writeAll() {
    cout << "\n=== Writing Output Files ===" << endl;

    bool success = true;

    // Step 1: Write mapping list
    if (!writeMapList()) {
        cerr << "Failed to write mapping list" << endl;
        success = false;
    }

    // Step 2: Write Verilog netlist
    if (!writeVerilog()) {
        cerr << "Failed to write Verilog netlist" << endl;
        success = false;
    }

    // Step 3: Note about DEF file
    if (!writeDef()) {
        cerr << "Failed to write DEF file" << endl;
        success = false;
    }

    if (success) {
        cout << "\n✓ All output files written successfully!" << endl;
    }
    else {
        cout << "\n✗ Some output files failed to generate" << endl;
    }

    return success;
}