#include "WriteOutput.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <regex>
#include <unordered_set>
#include <cassert>
#include <array>
using namespace std;





// 是否為 escaped 標識（Verilog 以 '\' 開頭，並常以空白結尾）
inline bool WO_isEscaped(const std::string& s) {
    return !s.empty() && s[0] == '\\';
}

// 去掉一次 Verilog 逃逸（若最後一個字元是空白也去掉）
inline std::string WO_unescapeOnce(const std::string& s) {
    if (!WO_isEscaped(s)) return s;
    if (!s.empty() && s.back() == ' ') return s.substr(1, s.size() - 2);
    return s.substr(1);
}

// 規格化 ID：目前僅移除 Verilog 的反斜線逃逸與尾端空白
inline std::string WO_normId(const std::string& id) {
    return WO_unescapeOnce(id);
}

// 規格化路徑：針對每個以 '/' 分隔的 segment 做 unescape
inline std::string WO_normPath(const std::string& path) {
    std::string out; out.reserve(path.size());
    size_t i = 0;
    while (i < path.size()) {
        size_t j = path.find('/', i);
        std::string seg = (j == std::string::npos) ? path.substr(i) : path.substr(i, j - i);
        seg = WO_normId(seg);
        if (!out.empty()) out.push_back('/');
        out += seg;
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return out;
}

// 取得 basename（最後一段）
inline std::string WO_basename(const std::string& path) {
    size_t pos = path.find_last_of('/');
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

// 取得 dirname（去掉最後一段，若沒有 '/' 回傳空字串）
inline std::string WO_dirname(const std::string& path) {
    size_t pos = path.find_last_of('/');
    return (pos == std::string::npos) ? std::string() : path.substr(0, pos);
}








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

    // Step 1: 預先驗證所有 merged groups 的完整性
    std::unordered_set<std::string> processedGroups;
    std::unordered_set<std::string> consumedFFs;
    std::vector<std::pair<const MergedFF*, std::vector<const FlipFlopInfo*>>> validGroups;

    // 建立 FF lookup map
    std::unordered_map<std::string, const FlipFlopInfo*> ffLookup;
    for (const auto& ff : originalDefData_.flipFlops) {
        // 支援多種名稱格式
        ffLookup[ff.instName] = &ff;
        ffLookup[WO_normPath(ff.instName)] = &ff;

        // 處理 escaped names
        if (ff.instName[0] == '\\') {
            std::string unescaped = WO_unescapeOnce(ff.instName);
            ffLookup[unescaped] = &ff;
        }
    }

    // Step 2: 驗證每個 merged group
    for (const auto& mergedFF : mergedFFResults_) {
        std::vector<const FlipFlopInfo*> groupFFs;
        bool allValid = true;

        for (const auto& ffName : mergedFF.mergedFFs) {
            // 嘗試多種名稱格式查找
            const FlipFlopInfo* ffInfo = nullptr;

            // 直接查找
            auto it = ffLookup.find(ffName);
            if (it != ffLookup.end()) {
                ffInfo = it->second;
            }

            // 嘗試規格化後查找
            if (!ffInfo) {
                it = ffLookup.find(WO_normPath(ffName));
                if (it != ffLookup.end()) {
                    ffInfo = it->second;
                }
            }

            // 嘗試 basename 查找
            if (!ffInfo) {
                std::string baseName = WO_basename(ffName);
                it = ffLookup.find(baseName);
                if (it != ffLookup.end()) {
                    ffInfo = it->second;
                }
            }

            if (!ffInfo) {
                cerr << "  Warning: Cannot find FF info for " << ffName << " in group "
                    << mergedFF.newInstanceName << endl;
                allValid = false;
                break;
            }

            groupFFs.push_back(ffInfo);
        }

        if (allValid && !groupFFs.empty()) {
            validGroups.push_back({ &mergedFF, groupFFs });
            processedGroups.insert(WO_normPath(mergedFF.newInstanceName));

            // 標記 consumed
            for (const auto& ffInfo : groupFFs) {
                consumedFFs.insert(ffInfo->instName);
                consumedFFs.insert(WO_normPath(ffInfo->instName));
            }
        }
    }

    // Step 3: 計算實際 instance 數量
    int mergedCount = validGroups.size();
    int unmergedCount = 0;

    for (const auto& ff : originalDefData_.flipFlops) {
        if (consumedFFs.find(ff.instName) == consumedFFs.end() &&
            consumedFFs.find(WO_normPath(ff.instName)) == consumedFFs.end()) {
            unmergedCount++;
        }
    }

    int totalCount = mergedCount + unmergedCount;

    cout << "  Valid merged groups: " << mergedCount << endl;
    cout << "  Unmerged FFs: " << unmergedCount << endl;
    cout << "  Total instances: " << totalCount << endl;

    // Step 4: 寫入 header
    mapFile << "CellInst " << totalCount << endl;

    // Step 5: 寫入 merged FF mappings（對齊 bit 順序）
    for (const auto& [mergedFF, groupFFs] : validGroups) {
        const LibCell* mbffCell = nullptr;
        if (libParser_) {
            mbffCell = libParser_->getCell(mergedFF->mbffType);
        }

        // 處理每個 bit
        for (size_t bitIdx = 0; bitIdx < groupFFs.size(); ++bitIdx) {
            const auto* ffInfo = groupFFs[bitIdx];
            std::string origName = ffInfo->instName;
            std::string mbffName = mergedFF->newInstanceName;

            // 取得正確的 pin 名稱（使用 library bundle 或 fallback）
            std::string dPin = "D" + std::to_string(bitIdx);
            std::string qPin = "Q" + std::to_string(bitIdx);
            std::string qnPin = "QN" + std::to_string(bitIdx);

            if (mbffCell) {
                if (mbffCell->hasBundle("D")) {
                    auto members = mbffCell->getBundleMembers("D");
                    if (bitIdx < members.size()) dPin = members[bitIdx];
                }
                if (mbffCell->hasBundle("Q")) {
                    auto members = mbffCell->getBundleMembers("Q");
                    if (bitIdx < members.size()) qPin = members[bitIdx];
                }
                if (mbffCell->hasBundle("QN")) {
                    auto members = mbffCell->getBundleMembers("QN");
                    if (bitIdx < members.size()) qnPin = members[bitIdx];
                }
            }

            // 寫入 mappings
            mapFile << origName << "/D map " << mbffName << "/" << dPin << endl;
            mapFile << origName << "/Q map " << mbffName << "/" << qPin << endl;

            // QN（檢查原始 FF 是否有 QN）
            bool hasQN = (ffInfo->cellType.find("FSDNQ") == std::string::npos);
            if (hasQN && mbffCell && mbffCell->pins.find(qnPin) != mbffCell->pins.end()) {
                mapFile << origName << "/QN map " << mbffName << "/" << qnPin << endl;
            }

            // Clock（共用）
            mapFile << origName << "/CK map " << mbffName << "/CK" << endl;

            // Scan pins（第一個和最後一個）
            if (bitIdx == 0 && !ffInfo->scanIn.empty() && ffInfo->scanIn != "UNCONNECTED") {
                mapFile << origName << "/SI map " << mbffName << "/SI" << endl;
            }
            if (bitIdx == groupFFs.size() - 1 && !ffInfo->scanOut.empty() && ffInfo->scanOut != "UNCONNECTED") {
                mapFile << origName << "/SO map " << mbffName << "/SO" << endl;
            }
        }
    }

    // Step 6: 寫入未合併的 FFs
    for (const auto& ff : originalDefData_.flipFlops) {
        if (consumedFFs.find(ff.instName) == consumedFFs.end() &&
            consumedFFs.find(WO_normPath(ff.instName)) == consumedFFs.end()) {

            bool hasQN = (ff.cellType.find("FSDNQ") == std::string::npos);

            mapFile << ff.instName << "/D map " << ff.instName << "/D" << endl;
            mapFile << ff.instName << "/Q map " << ff.instName << "/Q" << endl;
            if (hasQN) {
                mapFile << ff.instName << "/QN map " << ff.instName << "/QN" << endl;
            }
            mapFile << ff.instName << "/CK map " << ff.instName << "/CK" << endl;

            if (!ff.scanIn.empty() && ff.scanIn != "UNCONNECTED") {
                mapFile << ff.instName << "/SI map " << ff.instName << "/SI" << endl;
            }
            if (!ff.scanOut.empty() && ff.scanOut != "UNCONNECTED") {
                mapFile << ff.instName << "/SO map " << ff.instName << "/SO" << endl;
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
        auto itOld = simpleToFullNameMap_.find(simpleName);
        if (itOld != simpleToFullNameMap_.end() && itOld->second != ff.instName) {
            std::cout << "[DBG][buildSimpleToFull] collision: simple='"
                << simpleName << "' old='" << itOld->second
                << "' new='" << ff.instName << "'\n";
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


    const std::string targetTag = "merged_127"; // 你要追的 MBFF 名稱（可改）
    // 找到目標 MBFF
    const MergedFF* target = nullptr;
    for (const auto& mf : mergedFFResults_) {
        // 比對 full 或簡名都行
        std::string last = mf.newInstanceName;
        size_t p = last.find_last_of('/');
        if (p != std::string::npos) last = last.substr(p + 1);
        if (mf.newInstanceName == targetTag || last == targetTag) {
            target = &mf; break;
        }
    }

    if (!target) {
        std::cout << "[DBG][buildSimpleToFull] target '" << targetTag
            << "' NOT FOUND in mergedFFResults_. "
            << "=> 可能 WriteOutput 收到的 mergedFFResults_ 裡沒有這顆。\n";
    }
    else {
        std::cout << "[DBG][buildSimpleToFull] target '" << targetTag
            << "' found. bits=" << target->mergedFFs.size() << "\n";
        for (const auto& sff : target->mergedFFs) {
            std::string simple = sff;
            size_t p = simple.find_last_of('/');
            if (p != std::string::npos) simple = simple.substr(p + 1);

            auto it = simpleToFullNameMap_.find(simple);
            std::cout << "  [SIMPLE] '" << simple << "' -> map: ";
            if (it == simpleToFullNameMap_.end()) std::cout << "<none>\n";
            else std::cout << "'" << it->second << "'\n";

            std::string bySimple_get = getFullPath(simple);
            std::string byFull_get = getFullPath(sff);
            std::cout << "     getFullPath(simple)=" << bySimple_get << "\n";
            std::cout << "     getFullPath(full??)=" << byFull_get << "\n";

            bool m1 = mergeMap_.isMerged(sff);
            bool m2 = mergeMap_.isMerged(bySimple_get);
            bool m3 = mergeMap_.isMerged(byFull_get);
            std::cout << "     isMerged(sff)=" << m1
                << " isMerged(getFull(simple))=" << m2
                << " isMerged(getFull(full??))=" << m3 << "\n";
        }
    }
}
// writeVerilog() 中處理模組實例化的修正部分
// 完整的 writeVerilog() 方法，包含 QN 支援
bool WriteOutput::writeVerilog() {
    using std::string;
    using std::vector;
    using std::unordered_set;
    using std::unordered_map;

    const string outputFilename = outputName_ + ".v";
    std::ofstream fout(outputFilename);
    if (!fout.is_open()) {
        std::cerr << "[WriteOutput] Error: Cannot create output file: " << outputFilename << std::endl;
        return false;
    }
    if (!verilogParser_) {
        std::cerr << "[WriteOutput] Error: VerilogParser not set" << std::endl;
        return false;
    }

    // ---------- helpers ----------
    auto keepEscaped = [](string id) {
        if (!id.empty() && id[0] == '\\' && id.back() != ' ') id.push_back(' ');
        return id;
        };
    auto escapeIfBus = [](const string& name) {
        if (!name.empty() && name[0] != '\\' && (name.find('[') != string::npos || name.find(']') != string::npos))
            return string("\\") + name + " ";
        return name;
        };
    auto unescapeIfEscaped = [](string s) {
        if (!s.empty() && s[0] == '\\' && s.back() == ' ') return s.substr(1, s.size() - 2);
        return s;
        };
    auto looksLikeOutput = [&](string n) {
        if (!n.empty() && n[0] == '\\' && n.back() == ' ') n = n.substr(1, n.size() - 2);
        return n.rfind("qo_", 0) == 0;
        };
    auto basename = [](const string& p) {
        size_t s = p.find_last_of('/');
        return (s == string::npos) ? p : p.substr(s + 1);
        };
    auto br2dunder = [](string s) { // a[3] -> a__3__
        string out; out.reserve(s.size() + 4);
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '[') {
                size_t j = i + 1, k = j;
                while (k < s.size() && isdigit((unsigned char)s[k])) ++k;
                if (k < s.size() && s[k] == ']' && k > j) {
                    out += "__";
                    out.append(s.begin() + j, s.begin() + k);
                    out += "__";
                    i = k;
                    continue;
                }
            }
            out.push_back(s[i]);
        }
        return out;
        };
    auto br2under = [](string s) { // a[3] -> a_3_
        string out; out.reserve(s.size() + 2);
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '[') {
                size_t j = i + 1, k = j;
                while (k < s.size() && isdigit((unsigned char)s[k])) ++k;
                if (k < s.size() && s[k] == ']' && k > j) {
                    out.push_back('_');
                    out.append(s.begin() + j, s.begin() + k);
                    out.push_back('_');
                    i = k;
                    continue;
                }
            }
            out.push_back(s[i]);
        }
        return out;
        };
    auto nameKeys = [&](string s) { // 允許 \a[3] , a[3] , a__3__ , a_3_
        vector<string> ks;
        ks.push_back(s);
        string t = unescapeIfEscaped(s);
        ks.push_back(t);
        ks.push_back(br2dunder(t));
        ks.push_back(br2under(t));
        return ks;
        };
    auto nthBundlePin = [&](const LibCell* cell, const char* bundle, size_t bitIdx, const char* fallbackPrefix) {
        if (cell && cell->hasBundle(bundle)) {
            auto v = cell->getBundleMembers(bundle);
            if (bitIdx < v.size()) return v[bitIdx];
        }
        return std::string(fallbackPrefix) + std::to_string(bitIdx);
        };
    auto escapeFormalPinIfNeeded = [&](string pin) {
        // formal port 若含 [] 需用 escaped identifier
        if (!pin.empty() && pin[0] == '\\') return keepEscaped(pin);
        if (pin.find('[') != string::npos || pin.find(']') != string::npos) return string("\\") + pin + " ";
        return pin;
        };
    auto ensureUniqueName = [&](unordered_set<string>& used, string n) {
        string base = n;
        if (!base.empty() && base[0] == '\\') base = unescapeIfEscaped(base);
        string cand = base;
        int suf = 0;
        while (used.count(cand)) cand = base + "_mbff" + std::to_string(++suf);
        used.insert(cand);
        if (cand.find('[') != string::npos || cand.find(']') != string::npos || cand.find("__") != string::npos)
            cand = "\\" + cand + " ";
        return cand;
        };

    // 供應 merged group（同一份定義，用於所有 module）
    // 建立：merged 名稱 & 每顆 leaf 都可 lookup
    unordered_map<string, const MergedFF*> grpByMergedName;   // key: WO_normPath(newInstanceName)
    unordered_map<string, const MergedFF*> grpByLeafFullpath; // key: WO_normPath(leaf full path)
    for (const auto& g : mergedFFResults_) {
        grpByMergedName[WO_normPath(g.newInstanceName)] = &g;
        for (const auto& lf : g.mergedFFs) grpByLeafFullpath[WO_normPath(lf)] = &g;
    }

    // 跨 module 去重：避免同一個 group 在多個 module 重複輸出
    unordered_set<string> emittedGroups; // key = WO_normPath(newInstanceName)

    const auto& modules = verilogParser_->getModules();

    for (const auto& module : modules) {
        // ---------- collect ports/wires ----------
        unordered_set<string> headerPorts(module.ports.begin(), module.ports.end());

        unordered_map<string, string> dirMap;
        for (auto& p : module.inputs)  dirMap[p] = "input";
        for (auto& p : module.outputs) dirMap[p] = "output";
        for (auto& p : module.inouts)  dirMap[p] = "inout";
        for (auto& hp : headerPorts) if (!dirMap.count(hp)) dirMap[hp] = looksLikeOutput(hp) ? "output" : "input";

        unordered_set<string> declaredPorts_raw;
        auto addSet = [&](const vector<string>& v) { for (auto& x : v) declaredPorts_raw.insert(x); };
        addSet(module.inputs); addSet(module.outputs); addSet(module.inouts);
        for (auto& hp : headerPorts) declaredPorts_raw.insert(hp);

        unordered_set<string> declaredPorts_norm;
        for (auto& s : declaredPorts_raw) declaredPorts_norm.insert(unescapeIfEscaped(s));

        unordered_set<string> declaredWires_raw;
        for (const auto& w : module.wires) declaredWires_raw.insert(w);
        for (const auto& s : module.supplies0) declaredWires_raw.insert(s);
        for (const auto& s : module.supplies1) declaredWires_raw.insert(s);

        unordered_set<string> declaredWires_norm;
        for (auto& s : declaredWires_raw) declaredWires_norm.insert(unescapeIfEscaped(s));

        std::set<string> usedNets;
        for (const auto& inst : module.instances) {
            for (const auto& conn : inst.connections) {
                string netName = conn.second;
                if (netName == "UNCONNECTED" || netName == "VSS" || netName == "VDD") continue;
                string netNorm = unescapeIfEscaped(netName);
                if (!netNorm.empty()) usedNets.insert(netNorm);
            }
        }

        // ---------- header ----------
        fout << "module " << module.name << " ( ";
        for (size_t i = 0; i < module.ports.size(); ++i) {
            if (i) fout << " , ";
            if (i && i % 4 == 0) fout << "\n    ";
            string p = module.ports[i];
            if (p[0] != '\\' && p.find('[') != string::npos) p = "\\" + p + " ";
            fout << p;
        }
        fout << " ) ;\n\n";

        auto widthOfPort = [&](const string& p_raw) -> string {
            const string p_norm = unescapeIfEscaped(p_raw);
            auto it = module.portDeclWidth.find(p_norm);
            if (it == module.portDeclWidth.end()) it = module.portDeclWidth.find(p_raw);
            return (it != module.portDeclWidth.end() && !it->second.empty()) ? (it->second + " ") : "";
            };
        for (auto& hp : module.ports) {
            fout << dirMap[hp] << " " << widthOfPort(hp) << keepEscaped(hp) << " ;\n";
        }
        fout << "\n";

        for (const auto& n : module.supplies0) fout << "supply0 " << keepEscaped(escapeIfBus(n)) << " ;\n";
        for (const auto& n : module.supplies1) fout << "supply1 " << keepEscaped(escapeIfBus(n)) << " ;\n";
        if (!module.supplies0.empty() || !module.supplies1.empty()) fout << "\n";

        unordered_set<string> s0(module.supplies0.begin(), module.supplies0.end());
        unordered_set<string> s1(module.supplies1.begin(), module.supplies1.end());

        vector<string> filteredWires;
        filteredWires.reserve(module.wires.size());
        for (auto& w : module.wires) {
            if (declaredPorts_raw.count(w)) continue;
            if (s0.count(w) || s1.count(w)) continue;
            filteredWires.push_back(w);
        }
        for (const auto& w : filteredWires) {
            string wn = w;
            if (wn[0] != '\\' && wn.find('[') != string::npos) wn = "\\" + wn + " ";
            fout << "wire " << wn << " ;\n";
        }

        vector<string> additionalWires;
        for (const auto& net : usedNets) {
            if (net.empty()) continue;
            if (net == "VDD" || net == "VSS") continue;
            if (!declaredPorts_norm.count(net) && !declaredWires_norm.count(net)) {
                additionalWires.push_back(net);
                declaredWires_norm.insert(net);
            }
        }
        if (!additionalWires.empty()) {
            if (!filteredWires.empty()) fout << "\n";
            fout << "// Additional wires for connections\n";
            for (const auto& w : additionalWires) {
                string wn = (w.find('[') != string::npos && (w.empty() || w[0] != '\\'))
                    ? ("\\" + w + " ") : w;
                fout << "wire " << wn << " ;\n";
            }
            fout << "\n";
        }

        for (const auto& stmt : module.assignStatements) fout << stmt << "\n";
        if (!module.assignStatements.empty()) fout << "\n";

        // ---------- per-module preparation ----------
        // 建立實例快速索引（僅 leaf cell）
        unordered_map<string, size_t> idxByKey; // name variant -> index
        for (size_t i = 0; i < module.instances.size(); ++i) {
            const auto& o = module.instances[i];
            if (o.isModuleInstance) continue;
            for (auto& k : nameKeys(o.instName)) idxByKey.emplace(k, i);
        }
        auto findIdxBySimple = [&](const string& simpleName) -> size_t {
            for (auto& k : nameKeys(simpleName)) {
                auto it = idxByKey.find(k);
                if (it != idxByKey.end()) return it->second;
            }
            return size_t(-1);
            };

        // 先嘗試在本 module 內「一次性」emit MBFF
        unordered_set<size_t> consumedIdx;             // 被合併掉的單顆 FF 索引
        unordered_set<string> localInstNames;          // 本 module 內避免重名（for MBFF）
        for (const auto& o : module.instances) if (!o.isModuleInstance) localInstNames.insert(unescapeIfEscaped(o.instName));

        for (const auto& g : mergedFFResults_) {
            const string gKey = WO_normPath(g.newInstanceName);
            if (emittedGroups.count(gKey)) continue;   // 已在其他 module 輸出過

            // 這個 group 的 leaf 是否全都在本 module？
            bool allHere = true;
            vector<size_t> leafIdx(g.mergedFFs.size(), size_t(-1));
            for (size_t b = 0; b < g.mergedFFs.size(); ++b) {
                string leafSimple = basename(g.mergedFFs[b]);
                size_t idx = findIdxBySimple(leafSimple);
                if (idx == size_t(-1)) { allHere = false; break; }
                leafIdx[b] = idx;
            }
            if (!allHere) continue;

            // 收集連線
            const LibCell* mbffCell = (libParser_ ? libParser_->getCell(g.mbffType) : nullptr);
            std::vector<std::pair<string, string>> conns;

            // D/Q/QN
            for (size_t bitIdx = 0; bitIdx < g.mergedFFs.size(); ++bitIdx) {
                const auto& instFF = module.instances[leafIdx[bitIdx]];
                for (const auto& k : instFF.connections) {
                    const string& pin = k.first;
                    const string& net = k.second;
                    if (pin == "D") {
                        string formal = nthBundlePin(mbffCell, "D", bitIdx, "D");
                        conns.push_back({ "." + escapeFormalPinIfNeeded(formal), net });
                    }
                    else if (pin == "Q") {
                        string formal = nthBundlePin(mbffCell, "Q", bitIdx, "Q");
                        conns.push_back({ "." + escapeFormalPinIfNeeded(formal), net });
                    }
                    else if (pin == "QN") {
                        string formal = nthBundlePin(mbffCell, "QN", bitIdx, "QN");
                        conns.push_back({ "." + escapeFormalPinIfNeeded(formal), net });
                    }
                }
            }

            // 共用腳：first 的 CK/CLK, SI, SE；last 的 SO
            if (!g.mergedFFs.empty()) {
                const auto& firstFF = module.instances[leafIdx.front()];
                const auto& lastFF = module.instances[leafIdx.back()];

                // CK/CLK, SI, SE from first
                for (const auto& k : firstFF.connections) {
                    if (k.first == "CK" || k.first == "CLK") {
                        string ck = "CK";
                        if (mbffCell && mbffCell->pins.find("CLK") != mbffCell->pins.end()) ck = "CLK";
                        conns.push_back({ "." + ck, k.second });
                    }
                    else if (k.first == "SI") {
                        conns.push_back({ ".SI", k.second });
                    }
                    else if (k.first == "SE") {
                        conns.push_back({ ".SE", k.second });
                    }
                }
                // SO from last
                for (const auto& k : lastFF.connections) {
                    if (k.first == "SO") { conns.push_back({ ".SO", k.second }); break; }
                }
            }

            // 電源
            conns.push_back({ ".VDD","VDD" });
            conns.push_back({ ".VSS","VSS" });

            // emit MBFF instance
            string instName = g.newInstanceName;
            size_t slash = instName.find_last_of('/');
            if (slash != string::npos) instName = instName.substr(slash + 1);
            instName = ensureUniqueName(localInstNames, instName);

            fout << g.mbffType << " " << instName << " ( ";
            for (size_t i = 0; i < conns.size(); ++i) {
                if (i) fout << " , ";
                if (i && i % 3 == 0) fout << "\n    ";
                string net = conns[i].second;
                if (net != "VDD" && net != "VSS" && net != "UNCONNECTED")
                    net = escapeIfBus(net);
                fout << conns[i].first << " ( " << net << " )";
            }
            fout << " ) ;\n";

            // 標記這個 group 已輸出、並把 leaf 單顆吃掉
            emittedGroups.insert(gKey);
            for (auto idx : leafIdx) consumedIdx.insert(idx);
        }

        // ---------- 再輸出剩餘 instances ----------
        for (size_t i = 0; i < module.instances.size(); ++i) {
            const auto& inst = module.instances[i];

            if (!inst.isModuleInstance && consumedIdx.count(i)) {
                // 被合併掉的單顆 FF：跳過
                continue;
            }

            // 原樣輸出
            string instNameOutput = inst.instName;
            if (instNameOutput.find('[') != string::npos ||
                instNameOutput.find(']') != string::npos ||
                instNameOutput.find("__") != string::npos) {
                instNameOutput = "\\" + unescapeIfEscaped(instNameOutput) + " ";
            }

            fout << inst.cellType << " " << instNameOutput << " ( ";
            for (size_t k = 0; k < inst.connections.size(); ++k) {
                if (k) fout << " , ";
                if (k && k % 3 == 0) fout << "\n    ";
                string pin = inst.connections[k].first;
                if (!pin.empty() && pin[0] == '\\') pin = keepEscaped(pin);
                else if (pin.find('[') != string::npos || pin.find(']') != string::npos) pin = "\\" + pin + " ";

                string net = inst.connections[k].second;
                if (net != "VDD" && net != "VSS" && net != "UNCONNECTED")
                    net = escapeIfBus(net);

                fout << "." << pin << " ( " << net << " )";
            }
            fout << " ) ;\n";
        }

        fout << "\nendmodule\n\n";
    } // end modules

    fout.close();
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

// WriteOutput.cpp

bool WriteOutput::writeDef() {
    const std::string outputFilename = outputName_ + ".def";
    std::ofstream defFile(outputFilename);
    if (!defFile.is_open()) {
        std::cerr << "[WriteOutput] Error: Cannot create DEF file: " << outputFilename << std::endl;
        return false;
    }
    std::cout << "\n[WriteOutput] Writing DEF file to " << outputFilename << "." << std::endl;

    try {
        // --------------------------
        // 0) Helpers / name utils
        // --------------------------
        if (simpleToFullNameMap_.empty()) {
            // 你專案裡已經有的函式：建立 simple->full 的查表
            buildSimpleToFullNameMapping();
        }

        auto trim = [](std::string s)->std::string {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
            return s.substr(a, b - a);
            };
        auto toSimple = [&](const std::string& n)->std::string {
            std::string s = trim(n);
            size_t p = s.find_last_of('/');
            return (p == std::string::npos) ? s : s.substr(p + 1);
            };
        auto toFull = [&](const std::string& maybeSimple)->std::string {
            auto it = simpleToFullNameMap_.find(maybeSimple);
            return (it == simpleToFullNameMap_.end()) ? maybeSimple : it->second;
            };
        auto stripBrackets = [](const std::string& s)->std::string {
            std::string t; t.reserve(s.size());
            for (char c : s) if (c != '[' && c != ']') t.push_back(c);
            return t;
            };
        // for查表：把 pin 名歸一成 D/Q/QN/CK/SI/SE/SO 基底
        auto basePin = [&](std::string p)->std::string {
            std::string t; t.reserve(p.size());
            for (char c : p) if (c != '[' && c != ']') t.push_back(c);
            while (!t.empty() && std::isdigit(static_cast<unsigned char>(t.back()))) t.pop_back();
            if (t == "CLK" || t == "CP" || t == "C") t = "CK";
            return t;
            };
        // 新端 formal pin：優先用 .lib pins，支援 D[0]↔D0
        auto pickFormalPin = [&](const LibCell* cell, const std::string& cand)->std::string {
            if (!cell) return cand;
            if (cell->pins.find(cand) != cell->pins.end()) return cand;
            std::string nb = stripBrackets(cand);
            if (cell->pins.find(nb) != cell->pins.end()) return nb;
            return cand;
            };

        // --------------------------
        // 1) 可修改拷貝
        // --------------------------
        DefData defDataCopy = originalDefData_;
        std::unordered_set<std::string> componentNameSet;

        // --------------------------
        // 2) 先從「原始 NETS」反推 leaf FF -> net（以 pin 基底）
        // --------------------------
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> leafPin2Net;
        leafPin2Net.reserve(defDataCopy.nets.size() * 2);

        for (const auto& net : defDataCopy.nets) {
            const std::string& netName = net.name;
            for (const auto& np : net.connections) {
                std::string pBase = basePin(np.pin);
                if (pBase == "D" || pBase == "Q" || pBase == "QN" || pBase == "CK" || pBase == "SI" || pBase == "SE" || pBase == "SO") {
                    leafPin2Net[np.instance][pBase] = netName;
                }
            }
        }

        // --------------------------
        // 3) 收集被併掉的 leaf FF 名（full/simple/映射 full 全部放入）
        // --------------------------
        std::unordered_set<std::string> mergedFFSet;
        mergedFFSet.reserve(2048);
        for (const auto& mbff : mergedFFResults_) {
            for (const auto& ff : mbff.mergedFFs) {
                std::string full = trim(ff);
                std::string simple = toSimple(full);
                std::string mapfull = toFull(simple);
                mergedFFSet.insert(full);
                mergedFFSet.insert(simple);
                mergedFFSet.insert(mapfull);
            }
        }

        // --------------------------
        // 4) 建 old(leaf, pinBase) -> new(MBFF, formalPin) 的映射
        // --------------------------
        std::map<std::pair<std::string, std::string>,
            std::pair<std::string, std::string>> ffPinMap;

        for (const auto& mbff : mergedFFResults_) {
            const LibCell* mbffCell = (libParser_ ? libParser_->getCell(mbff.mbffType) : nullptr);
            const size_t BW = mbff.mergedFFs.size();

            for (size_t i = 0; i < BW; ++i) {
                std::string dPin = "D" + std::to_string(i);
                std::string qPin = "Q" + std::to_string(i);
                std::string qnPin = "QN" + std::to_string(i);
                if (mbffCell) {
                    if (mbffCell->hasBundle("D")) { auto v = mbffCell->getBundleMembers("D");  if (i < v.size()) dPin = v[i]; }
                    if (mbffCell->hasBundle("Q")) { auto v = mbffCell->getBundleMembers("Q");  if (i < v.size()) qPin = v[i]; }
                    if (mbffCell->hasBundle("QN")) { auto v = mbffCell->getBundleMembers("QN"); if (i < v.size()) qnPin = v[i]; }
                }
                dPin = pickFormalPin(mbffCell, dPin);
                qPin = pickFormalPin(mbffCell, qPin);
                qnPin = pickFormalPin(mbffCell, qnPin);

                const std::string oldFull = trim(mbff.mergedFFs[i]);
                const std::string oldSimple = toSimple(oldFull);
                const std::string keys[2] = { oldFull, oldSimple };

                for (const auto& k : keys) {
                    // 舊端一律用「基底 pin」當 key
                    ffPinMap[{k, "D"}] = { mbff.newInstanceName, dPin };
                    ffPinMap[{k, "Q"}] = { mbff.newInstanceName, qPin };
                    if (!mbffCell || mbffCell->pins.find(qnPin) != mbffCell->pins.end())
                        ffPinMap[{k, "QN"}] = { mbff.newInstanceName, qnPin };
                    ffPinMap[{k, "CK"}] = { mbff.newInstanceName, "CK" };
                    ffPinMap[{k, "CLK"}] = { mbff.newInstanceName, "CK" }; // 冗餘
                    if (i == 0) {
                        ffPinMap[{k, "SI"}] = { mbff.newInstanceName, "SI" };
                        ffPinMap[{k, "SE"}] = { mbff.newInstanceName, "SE" };
                    }
                    if (i + 1 == BW) {
                        ffPinMap[{k, "SO"}] = { mbff.newInstanceName, "SO" };
                    }
                }
            }
        }

        // --------------------------
        // 5) 第一階段：替換 NETS（命中 map 就改；舊 leaf 但沒命中就丟）
        // --------------------------
        std::vector<NetInfo> newNets;
        newNets.reserve(defDataCopy.nets.size());
        int warnCnt = 0;

        for (auto net : defDataCopy.nets) {
            NetInfo n = net;
            n.connections.clear();
            std::set<std::pair<std::string, std::string>> seen; // (inst,pin) 去重

            for (const auto& np : net.connections) {
                const std::string pinBase = basePin(np.pin);
                const std::string fullByTable = toFull(toSimple(np.instance));
                const std::string instCands[3] = { trim(np.instance), fullByTable, toSimple(np.instance) };

                bool replaced = false;
                for (const auto& ic : instCands) {
                    auto it = ffPinMap.find({ ic, pinBase });
                    if (it != ffPinMap.end()) {
                        const auto& newInst = it->second.first;
                        const auto& newPin = it->second.second;
                        if (seen.emplace(newInst, newPin).second)
                            n.connections.push_back(NetPin{ newInst, newPin });
                        replaced = true;
                        break;
                    }
                }

                if (!replaced) {
                    bool isMerged = mergedFFSet.count(trim(np.instance)) ||
                        mergedFFSet.count(toSimple(np.instance)) ||
                        mergedFFSet.count(fullByTable);
                    if (isMerged) {
                        if (warnCnt++ < 20) {
                            std::cerr << "[DEF][WARN] drop old FF conn: "
                                << np.instance << "/" << np.pin
                                << " on net " << net.name
                                << " (base=" << pinBase << " not mapped)\n";
                        }
                        continue; // 丟掉舊 leaf 連線
                    }
                    if (seen.emplace(np.instance, np.pin).second)
                        n.connections.push_back(np); // 非 FF 或未合併：保留
                }
            }

            net.connections.swap(n.connections);
            newNets.push_back(std::move(net));
        }
        defDataCopy.nets.swap(newNets);

        // --------------------------
        // 6) 第二階段：建立 net -> set(inst,pin) 查找（供回填）
        // --------------------------
        std::unordered_map<std::string, std::set<std::pair<std::string, std::string>>> netConnSet;
        netConnSet.reserve(defDataCopy.nets.size());
        for (const auto& net : defDataCopy.nets) {
            auto& S = netConnSet[net.name];
            for (const auto& np : net.connections) S.emplace(np.instance, np.pin);
        }
        auto ensureConnected = [&](const std::string& netName,
            const std::string& inst, const std::string& pin) {
                if (netName.empty() || inst.empty() || pin.empty()) return;
                auto& S = netConnSet[netName];
                if (S.emplace(inst, pin).second) {
                    // push back 到 defDataCopy.nets
                    for (auto& n : defDataCopy.nets) {
                        if (n.name == netName) {
                            n.connections.push_back({ inst, pin });
                            break;
                        }
                    }
                }
            };

        // --------------------------
        // 7) 第三階段：回填任何漏網的 MBFF 連線（依據 leafPin2Net）
        // --------------------------
        for (const auto& mbff : mergedFFResults_) {
            const size_t BW = mbff.mergedFFs.size();
            const LibCell* mbffCell = (libParser_ ? libParser_->getCell(mbff.mbffType) : nullptr);

            auto formalName = [&](const std::string& bundle, size_t i, const std::string& fallback) {
                std::string p = fallback;
                if (mbffCell && mbffCell->hasBundle(bundle)) {
                    auto v = mbffCell->getBundleMembers(bundle);
                    if (i < v.size()) p = v[i];
                }
                if (mbffCell && mbffCell->pins.find(p) == mbffCell->pins.end()) {
                    std::string nb; nb.reserve(p.size());
                    for (char c : p) if (c != '[' && c != ']') nb.push_back(c);
                    if (mbffCell->pins.find(nb) != mbffCell->pins.end()) p = nb;
                }
                return p;
                };

            // 共有腳：bit0 / last 的 leaf 找 net
            std::string ckNet, siNet, seNet, soNet;
            {
                auto it0 = leafPin2Net.find(mbff.mergedFFs.front());
                if (it0 != leafPin2Net.end()) {
                    if (it0->second.count("CK")) ckNet = it0->second.at("CK");
                    if (it0->second.count("SI")) siNet = it0->second.at("SI");
                    if (it0->second.count("SE")) seNet = it0->second.at("SE");
                }
                auto itL = leafPin2Net.find(mbff.mergedFFs.back());
                if (itL != leafPin2Net.end()) {
                    if (itL->second.count("SO")) soNet = itL->second.at("SO");
                }
            }
            if (!ckNet.empty()) ensureConnected(ckNet, mbff.newInstanceName, "CK");
            if (!siNet.empty()) ensureConnected(siNet, mbff.newInstanceName, "SI");
            if (!seNet.empty()) ensureConnected(seNet, mbff.newInstanceName, "SE");
            if (!soNet.empty()) ensureConnected(soNet, mbff.newInstanceName, "SO");

            // 逐 bit：D/Q/(QN)
            for (size_t i = 0; i < BW; ++i) {
                const std::string& leaf = mbff.mergedFFs[i];
                auto it = leafPin2Net.find(leaf);
                if (it == leafPin2Net.end()) continue;

                const std::string dNet = (it->second.count("D") ? it->second.at("D") : "");
                const std::string qNet = (it->second.count("Q") ? it->second.at("Q") : "");
                const std::string nNet = (it->second.count("QN") ? it->second.at("QN") : "");

                if (!dNet.empty()) {
                    std::string dPin = formalName("D", i, "D" + std::to_string(i));
                    ensureConnected(dNet, mbff.newInstanceName, dPin);
                }
                if (!qNet.empty()) {
                    std::string qPin = formalName("Q", i, "Q" + std::to_string(i));
                    ensureConnected(qNet, mbff.newInstanceName, qPin);
                }
                if (!nNet.empty()) {
                    std::string nPin = formalName("QN", i, "QN" + std::to_string(i));
                    if (!mbffCell || mbffCell->pins.find(nPin) != mbffCell->pins.end())
                        ensureConnected(nNet, mbff.newInstanceName, nPin);
                }
            }
        }

        // --------------------------
        // 8) COMPONENTS：新增 MBFF、移除被消耗 leaf FF
        // --------------------------
        std::unordered_set<std::string> consumed;
        for (const auto& mbff : mergedFFResults_) {
            for (const auto& ff : mbff.mergedFFs) {
                consumed.insert(trim(ff));
                consumed.insert(toSimple(ff));
                consumed.insert(toFull(toSimple(ff)));
            }
        }

        std::vector<ComponentInfo> newComponents;
        std::unordered_set<std::string> seenNames;

        // 先加 MBFF
        for (const auto& mbff : mergedFFResults_) {
            if (!seenNames.insert(mbff.newInstanceName).second) {
                std::cerr << "[Warn] Duplicate MBFF name, skip: " << mbff.newInstanceName << "\n";
                continue;
            }
            ComponentInfo c;
            c.name = mbff.newInstanceName;
            c.cellType = mbff.mbffType;
            c.x = mbff.newX;
            c.y = mbff.newY;
            c.orient = mbff.orientation.empty() ? "N" : mbff.orientation;
            c.isFF = true;
            c.isMergedFF = true;
            newComponents.push_back(std::move(c));
        }
        // 再補上其餘未被消耗的元件
        for (const auto& comp : defDataCopy.components) {
            if (consumed.count(comp.name)) continue;
            if (!seenNames.insert(comp.name).second) continue;
            newComponents.push_back(comp);
        }

        // --------------------------
        // 9) 輸出 DEF
        // --------------------------
        defFile << "VERSION 5.8 ;\n";
        defFile << "DIVIDERCHAR \"/\" ;\n";
        defFile << "BUSBITCHARS \"[]\" ;\n";
        defFile << "DESIGN top ;\n";
        defFile << "UNITS DISTANCE MICRONS " << defDataCopy.units << " ;\n";
        defFile << "PROPERTYDEFINITIONS\n";
        defFile << "COMPONENTPIN ACCESS_DIRECTION STRING ;\n";
        defFile << "END PROPERTYDEFINITIONS\n";

        defFile << "DIEAREA ( "
            << defDataCopy.dieArea.xMin << " " << defDataCopy.dieArea.yMin
            << " ) ( " << defDataCopy.dieArea.xMin << " " << defDataCopy.dieArea.yMax
            << " ) ( " << defDataCopy.dieArea.xMax << " " << defDataCopy.dieArea.yMax
            << " ) ( " << defDataCopy.dieArea.xMax << " " << defDataCopy.dieArea.yMin
            << " ) ;\n\n";

        for (const auto& row : defDataCopy.rows) {
            defFile << "ROW " << row.name << " " << row.siteName << " "
                << row.x << " " << row.y << " " << row.orientation
                << " DO " << row.count << " BY " << row.by
                << " STEP " << row.stepX << " " << row.stepY << " ;\n";
        }
        defFile << "\n";

        for (const auto& track : defDataCopy.tracks) {
            defFile << "TRACKS " << track.direction << " " << track.start
                << " DO " << track.count << " STEP " << track.step
                << " LAYER " << track.layer << " ;\n";
        }
        defFile << "\n";

        defFile << "COMPONENTS " << newComponents.size() << " ;\n";
        for (const auto& comp : newComponents) {
            if (componentNameSet.count(comp.name)) {
                std::cerr << "[Error] Duplicate component output in DEF: " << comp.name << std::endl;
                continue;
            }
            componentNameSet.insert(comp.name);
            defFile << "- " << comp.name << " " << comp.cellType
                << " + PLACED ( " << comp.x << " " << comp.y << " ) "
                << comp.orient << " ;\n";
        }
        defFile << "END COMPONENTS\n\n";

        defFile << "NETS " << defDataCopy.nets.size() << " ;\n";
        for (const auto& net : defDataCopy.nets) {
            defFile << "- " << net.name << "\n";
            for (size_t i = 0; i < net.connections.size(); ++i) {
                defFile << "  ( " << net.connections[i].instance
                    << " " << net.connections[i].pin << " )";
                if (i + 1 < net.connections.size()) defFile << "\n";
            }
            if (!net.use.empty()) defFile << "\n  + USE " << net.use;
            defFile << " ;\n";
        }
        defFile << "END NETS\n\n";

        defFile << "END DESIGN\n";
        defFile.close();

        std::cout << "  ✓ DEF file written successfully\n"
            << "    - Components: " << newComponents.size() << "\n"
            << "    - Nets: " << defDataCopy.nets.size() << "\n"
            << "    - New MBFFs: " << mergedFFResults_.size() << std::endl;
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

