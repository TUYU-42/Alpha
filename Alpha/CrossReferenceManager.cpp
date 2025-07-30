#include "CrossReferenceManager.h"
#include "LibParser.h"
#include <iostream>
#include <algorithm>
#include <queue>

using namespace std;

// 取得 instance 對應的 LibCell
const LibCell* CrossReferenceManager::getCellForInstance(const VerilogInstance& inst) {
    // 先查 cache
    auto it = cellCache_.find(inst.cellType);
    if (it != cellCache_.end()) {
        return it->second;
    }

    // 從 LibParser 查詢
    const LibCell* cell = libParser_->getCell(inst.cellType);
    if (cell) {
        cellCache_[inst.cellType] = cell;
    }

    return cell;
}

// 取得 instance 的所有 scan pins
vector<string> CrossReferenceManager::getInstanceScanPins(const VerilogInstance& inst) {
    // 查 cache
    auto it = scanPinCache_.find(inst.cellType);
    if (it != scanPinCache_.end()) {
        return it->second;
    }

    // 從 LibParser 查詢
    vector<string> scanPins = libParser_->getCellScanPins(inst.cellType);
    scanPinCache_[inst.cellType] = scanPins;

    return scanPins;
}

// 判斷 instance 的某個 pin 是否為 scan pin
bool CrossReferenceManager::isInstanceScanPin(const VerilogInstance& inst, const string& pinName) {
    return libParser_->isScanPin(inst.cellType, pinName);
}

// 取得 instance 的 bundle members
vector<string> CrossReferenceManager::getInstanceBundleMembers(const VerilogInstance& inst, const string& bundleName) {
    const LibCell* cell = getCellForInstance(inst);
    if (!cell) {
        return vector<string>();
    }

    return cell->getBundleMembers(bundleName);
}

// 找出 instance 的 scan out pin (可能是 SO 或某個 Qn)
string CrossReferenceManager::findScanOutPin(const VerilogInstance& inst) {
    const LibCell* cell = getCellForInstance(inst);
    if (!cell) {
        return "";
    }

    // 首先檢查是否有 SO pin
    for (const auto& conn : inst.connections) {
        if (conn.first == "SO" || conn.first == "so") {
            return conn.first;
        }
    }

    // 如果沒有 SO，檢查 Q bundle
    if (cell->hasBundle("Q")) {
        vector<string> qPins = cell->getBundleMembers("Q");

        // 對於多位元 FF，通常最高位的 Q 是 scan out
        if (!qPins.empty()) {
            string highestQ = qPins.back();  // 假設已排序

            // 確認這個 pin 在 instance connections 中存在
            for (const auto& conn : inst.connections) {
                if (conn.first == highestQ) {
                    return highestQ;
                }
            }
        }
    }

    // 單位元 FF，使用 Q 作為 scan out
    for (const auto& conn : inst.connections) {
        if (conn.first == "Q" || conn.first == "q") {
            return conn.first;
        }
    }

    return "";
}

// 分析所有 scan chain 連接
vector<ScanChainSegment> CrossReferenceManager::analyzeScanChainConnections() {
    vector<ScanChainSegment> segments;
    const auto& instances = verilogParser_->getInstances();

    // 建立 net 到 instance/pin 的映射
    map<string, vector<pair<string, string>>> netToInstPin;

    for (const auto& inst : instances) {
        for (const auto& conn : inst.connections) {
            if (conn.second != "UNCONNECTED" &&
                conn.second.find("UNCONNECTED") == string::npos) {
                netToInstPin[conn.second].push_back({ inst.instName, conn.first });
            }
        }
    }

    // 尋找 scan chain 連接
    for (const auto& inst : instances) {
        // 檢查是否為 FF
        if (!VerilogUtils::isFlipFlopCell(inst.cellType)) {
            continue;
        }

        // 找出 scan out pin
        string scanOutPin = findScanOutPin(inst);
        if (scanOutPin.empty()) {
            continue;
        }

        // 找出連接的 net
        string scanOutNet = "";
        for (const auto& conn : inst.connections) {
            if (conn.first == scanOutPin) {
                scanOutNet = conn.second;
                break;
            }
        }

        if (scanOutNet.empty() || scanOutNet == "UNCONNECTED") {
            continue;
        }

        // 找出這個 net 連接到的 SI pin
        auto it = netToInstPin.find(scanOutNet);
        if (it != netToInstPin.end()) {
            for (const auto& instPin : it->second) {
                if (instPin.first != inst.instName &&  // 不是自己
                    (instPin.second == "SI" || instPin.second == "si")) {

                    ScanChainSegment segment;
                    segment.fromInstance = inst.instName;
                    segment.fromPin = scanOutPin;
                    segment.toInstance = instPin.first;
                    segment.toPin = instPin.second;
                    segment.net = scanOutNet;
                    segment.usesQAsOut = (scanOutPin != "SO" && scanOutPin != "so");

                    segments.push_back(segment);
                }
            }
        }
    }

    return segments;
}

// 建立完整的 scan chains
map<string, vector<string>> CrossReferenceManager::buildScanChains() {
    map<string, vector<string>> chains;
    vector<ScanChainSegment> segments = analyzeScanChainConnections();

    // 建立連接圖
    map<string, string> nextInChain;  // instance -> next instance
    map<string, string> prevInChain;  // instance -> prev instance

    for (const auto& seg : segments) {
        nextInChain[seg.fromInstance] = seg.toInstance;
        prevInChain[seg.toInstance] = seg.fromInstance;
    }

    // 找出 chain 起點（沒有 prev 的）
    set<string> chainStarts;
    for (const auto& pair : nextInChain) {
        if (prevInChain.find(pair.first) == prevInChain.end()) {
            chainStarts.insert(pair.first);
        }
    }

    // 建立 chains
    int chainId = 0;
    for (const string& start : chainStarts) {
        string chainName = "chain_" + to_string(chainId++);
        vector<string>& chain = chains[chainName];

        string current = start;
        set<string> visited;

        while (!current.empty() && visited.find(current) == visited.end()) {
            chain.push_back(current);
            visited.insert(current);

            auto it = nextInChain.find(current);
            current = (it != nextInChain.end()) ? it->second : "";
        }
    }

    return chains;
}

// 生成 banking mapping
vector<PinMapping> CrossReferenceManager::generateBankingMapping(
    const vector<string>& singleBitInstances,
    const string& multiBitInstance,
    const string& multiBitCellType) {

    vector<PinMapping> mappings;
    const LibCell* mbCell = libParser_->getCell(multiBitCellType);
    if (!mbCell) {
        return mappings;
    }

    // 取得 multi-bit cell 的 bundles
    vector<string> dPins, qPins;
    if (mbCell->hasBundle("D")) {
        dPins = mbCell->getBundleMembers("D");
    }
    if (mbCell->hasBundle("Q")) {
        qPins = mbCell->getBundleMembers("Q");
    }

    // 對每個 single-bit instance 建立 mapping
    for (size_t i = 0; i < singleBitInstances.size() && i < dPins.size(); ++i) {
        const string& sbInst = singleBitInstances[i];

        // D pin mapping
        PinMapping dMap;
        dMap.sourceInstance = sbInst;
        dMap.sourcePin = "D";
        dMap.targetInstance = multiBitInstance;
        dMap.targetPin = dPins[i];
        mappings.push_back(dMap);

        // Q pin mapping
        if (i < qPins.size()) {
            PinMapping qMap;
            qMap.sourceInstance = sbInst;
            qMap.sourcePin = "Q";
            qMap.targetInstance = multiBitInstance;
            qMap.targetPin = qPins[i];
            mappings.push_back(qMap);
        }

        // Clock mapping
        PinMapping clkMap;
        clkMap.sourceInstance = sbInst;
        clkMap.sourcePin = "CLK";
        clkMap.targetInstance = multiBitInstance;
        clkMap.targetPin = "CLK";
        mappings.push_back(clkMap);

        // Scan pins mapping (if any)
        if (i == 0) {  // First instance gets SI
            PinMapping siMap;
            siMap.sourceInstance = sbInst;
            siMap.sourcePin = "SI";
            siMap.targetInstance = multiBitInstance;
            siMap.targetPin = "SI";
            mappings.push_back(siMap);
        }

    }

    return mappings;
}

// 生成 debanking mapping
vector<PinMapping> CrossReferenceManager::generateDebankingMapping(
    const string& multiBitInstance,
    const vector<string>& singleBitInstances,
    const string& singleBitCellType) {

    vector<PinMapping> mappings;

    // 找到 multi-bit instance
    const VerilogInstance* mbInst = verilogParser_->findInstance(multiBitInstance);
    if (!mbInst) {
        return mappings;
    }

    const LibCell* mbCell = getCellForInstance(*mbInst);
    if (!mbCell) {
        return mappings;
    }

    // 取得 bundles
    vector<string> dPins = mbCell->getBundleMembers("D");
    vector<string> qPins = mbCell->getBundleMembers("Q");

    // 建立 debanking mappings
    for (size_t i = 0; i < singleBitInstances.size() && i < dPins.size(); ++i) {
        const string& sbInst = singleBitInstances[i];

        // D pin mapping
        PinMapping dMap;
        dMap.sourceInstance = multiBitInstance;
        dMap.sourcePin = dPins[i];
        dMap.targetInstance = sbInst;
        dMap.targetPin = "D";
        mappings.push_back(dMap);

        // Q pin mapping
        if (i < qPins.size()) {
            PinMapping qMap;
            qMap.sourceInstance = multiBitInstance;
            qMap.sourcePin = qPins[i];
            qMap.targetInstance = sbInst;
            qMap.targetPin = "Q";
            mappings.push_back(qMap);
        }

        // Clock mapping
        PinMapping clkMap;
        clkMap.sourceInstance = multiBitInstance;
        clkMap.sourcePin = "CLK";
        clkMap.targetInstance = sbInst;
        clkMap.targetPin = "CLK";
        mappings.push_back(clkMap);
    }

    // Handle scan chain connections
    // ... (根據 scan chain 順序建立 SI/SO 連接)

    return mappings;
}

// 驗證 scan chain 完整性
bool CrossReferenceManager::validateScanChainIntegrity() {
    vector<ScanChainSegment> segments = analyzeScanChainConnections();
    map<string, vector<string>> chains = buildScanChains();

    // 檢查是否有斷鏈
    set<string> allFFInstances;
    for (const auto& inst : verilogParser_->getInstances()) {
        if (VerilogUtils::isFlipFlopCell(inst.cellType)) {
            allFFInstances.insert(inst.instName);
        }
    }

    set<string> chainedInstances;
    for (const auto& chain : chains) {
        for (const string& inst : chain.second) {
            chainedInstances.insert(inst);
        }
    }

    // 找出未連接的 FF
    vector<string> unchained;
    set_difference(allFFInstances.begin(), allFFInstances.end(),
        chainedInstances.begin(), chainedInstances.end(),
        back_inserter(unchained));

    if (!unchained.empty()) {
        cout << "Warning: Found " << unchained.size() << " unchained flip-flops" << endl;
        return false;
    }

    return true;
}

// 驗證 pin 連接
bool CrossReferenceManager::validatePinConnections(const VerilogInstance& inst) {
    const LibCell* cell = getCellForInstance(inst);
    if (!cell) {
        cout << "Error: Cell type " << inst.cellType << " not found in library" << endl;
        return false;
    }

    bool valid = true;

    // 檢查所有必要的 pins 是否都有連接
    for (const auto& pinPair : cell->pins) {
        const LibPin& pin = pinPair.second;

        // 檢查 input pins 是否有連接
        if (pin.direction == "input") {
            bool found = false;
            for (const auto& conn : inst.connections) {
                if (conn.first == pin.name) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                cout << "Warning: Input pin " << pin.name
                    << " of instance " << inst.instName << " is not connected" << endl;
                valid = false;
            }
        }
    }

    return valid;
}

// 打印 instance 詳細資訊
void CrossReferenceManager::printInstanceDetails(const string& instName) {
    const VerilogInstance* inst = verilogParser_->findInstance(instName);
    if (!inst) {
        cout << "Instance " << instName << " not found" << endl;
        return;
    }

    cout << "\n=== Instance: " << instName << " ===" << endl;
    cout << "Cell Type: " << inst->cellType << endl;

    const LibCell* cell = getCellForInstance(*inst);
    if (!cell) {
        cout << "Warning: Cell type not found in library" << endl;
        return;
    }

    cout << "Library: " << cell->libraryName << endl;
    cout << "Area: " << cell->area << endl;
    cout << "Power: " << cell->cellLeakagePower << endl;
    cout << "Bit Width: " << cell->bitWidth << endl;

    // 顯示 bundles
    if (!cell->bundles.empty()) {
        cout << "\nBundles:" << endl;
        for (const auto& bundlePair : cell->bundles) {
            const LibBundle& bundle = bundlePair.second;
            cout << "  " << bundle.name << ": ";
            for (const string& member : bundle.members) {
                cout << member << " ";
            }
            cout << endl;
        }
    }

    // 顯示連接
    cout << "\nConnections:" << endl;
    for (const auto& conn : inst->connections) {
        cout << "  ." << conn.first << "(" << conn.second << ")";

        // 標示 scan pins
        if (isInstanceScanPin(*inst, conn.first)) {
            cout << " [SCAN]";
        }

        cout << endl;
    }

    // 顯示 scan chain 資訊
    string scanOut = findScanOutPin(*inst);
    if (!scanOut.empty()) {
        cout << "\nScan Out Pin: " << scanOut;
        if (scanOut != "SO" && scanOut != "so") {
            cout << " (using Q as SO)";
        }
        cout << endl;
    }
}

// 打印 scan chain 報告
void CrossReferenceManager::printScanChainReport() {
    cout << "\n=== Scan Chain Analysis Report ===" << endl;

    vector<ScanChainSegment> segments = analyzeScanChainConnections();
    map<string, vector<string>> chains = buildScanChains();

    cout << "\nTotal Segments: " << segments.size() << endl;
    cout << "Total Chains: " << chains.size() << endl;

    // 打印每條 chain
    for (const auto& chain : chains) {
        cout << "\n" << chain.first << " (" << chain.second.size() << " FFs):" << endl;

        for (size_t i = 0; i < chain.second.size(); ++i) {
            const string& instName = chain.second[i];
            cout << "  " << (i + 1) << ". " << instName;

            // 顯示 cell type
            const VerilogInstance* inst = verilogParser_->findInstance(instName);
            if (inst) {
                cout << " (" << inst->cellType << ")";

                // 檢查是否使用 Q as SO
                string scanOut = findScanOutPin(*inst);
                if (!scanOut.empty() && scanOut != "SO" && scanOut != "so") {
                    cout << " [Q as SO: " << scanOut << "]";
                }
            }

            cout << endl;

            if (i < chain.second.size() - 1) {
                cout << "     |" << endl;
                cout << "     v" << endl;
            }
        }
    }

    // 驗證完整性
    cout << "\nIntegrity Check: ";
    if (validateScanChainIntegrity()) {
        cout << "PASSED" << endl;
    }
    else {
        cout << "FAILED" << endl;
    }
}

// 清除 cache
void CrossReferenceManager::clearCache() {
    cellCache_.clear();
    scanPinCache_.clear();
}