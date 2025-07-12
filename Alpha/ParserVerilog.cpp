#include "ParserVerilog.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <set>
#include <map>

using namespace std;

VerilogParser::VerilogParser() : isLoaded_(false) {
    // Initialize regex patterns with improved patterns
    moduleRegex_ = regex(R"(module\s+(\w+)\s*\((.*?)\)\s*;)", regex::ECMAScript);

    // Simplified instance regex, mainly used in parseInstanceFromString
    instanceRegex_ = regex(R"(^([A-Za-z0-9_]+)\s+([A-Za-z0-9_\\]+)\s*\(\s*(.*?)\s*\)\s*;)", regex::ECMAScript);

    // Improved pin connection regex
    pinConnectionRegex_ = regex(R"(\.\s*([A-Za-z0-9_]+)\s*\(\s*([^)]+?)\s*\))", regex::ECMAScript);

    portDeclRegex_ = regex(R"((input|output|inout)\s+(?:\[\d+:\d+\]\s+)?(\w+(?:\s*,\s*\w+)*)\s*;)", regex::ECMAScript);
    wireDeclRegex_ = regex(R"(wire\s+(?:\[\d+:\d+\]\s+)?(\w+(?:\s*,\s*\w+)*)\s*;)", regex::ECMAScript);
}
void VerilogParser::analyzeScanChains(const vector<VerilogInstance>& flipFlops) {
    cout << "\n=== Scan Chain Analysis ===" << endl;

    int scanConnectedFFs = 0;
    map<string, int> scanPinStats;

    // 建立 scan chain 連接圖
    map<string, string> soToSiMap; // SO net -> SI instance
    map<string, string> siToInstanceMap; // SI net -> instance name
    map<string, string> instanceToSoMap; // instance name -> SO net
    map<string, vector<string>> scanNets; // 收集所有 scan 相關的 net

    // 分析每? flip-flop 的 scan 連接
    cout << "\n--- Scan Pin Connections ---" << endl;
    for (const auto& ff : flipFlops) {
        bool hasScanConnection = false;
        string siNet = "";
        string soNet = "";

        cout << "FF: " << ff.instName << " (" << ff.cellType << ")" << endl;

        for (const auto& conn : ff.connections) {
            string pinName = conn.first;
            string netName = conn.second;

            // 檢查是否為 scan pin
            if (VerilogUtils::isScanPin(pinName)) {
                scanPinStats[pinName]++;
                hasScanConnection = true;

                cout << "  " << pinName << " -> " << netName << endl;

                if (pinName == "SI" || pinName == "si") {
                    siNet = netName;
                    siToInstanceMap[netName] = ff.instName;
                }
                else if (pinName == "SO" || pinName == "so") {
                    soNet = netName;
                    instanceToSoMap[ff.instName] = netName;
                }

                // 收集 scan net
                if (netName != "UNCONNECTED" && netName.find("UNCONNECTED") == string::npos) {
                    scanNets[netName].push_back(ff.instName + "/" + pinName);
                }
            }
        }

        if (hasScanConnection) {
            scanConnectedFFs++;
        }

        // 建立 SO -> SI 映射
        if (!soNet.empty() && !siNet.empty()) {
            soToSiMap[soNet] = ff.instName;
        }
    }

    cout << "\n--- Scan Statistics ---" << endl;
    cout << "Flip-flops with scan connections: " << scanConnectedFFs << " / " << flipFlops.size() << endl;

    if (!scanPinStats.empty()) {
        cout << "\nScan pin usage:" << endl;
        for (const auto& stat : scanPinStats) {
            cout << "  " << stat.first << ": " << stat.second << " connections" << endl;
        }
    }

    // 分析 Scan Chain 拓撲
    cout << "\n--- Scan Chain Topology ---" << endl;
    printScanChainTopology(flipFlops, scanNets);

    // 重建 Scan Chains
    cout << "\n--- Reconstructed Scan Chains ---" << endl;
    reconstructScanChains(flipFlops);
}

void VerilogParser::printScanChainTopology(const vector<VerilogInstance>& flipFlops,
    const map<string, vector<string>>& scanNets) {
    cout << "\nScan Net Connections:" << endl;

    for (const auto& net : scanNets) {
        if (net.second.size() > 1) {
            cout << "Net '" << net.first << "' connects:" << endl;
            for (const auto& connection : net.second) {
                cout << "  " << connection << endl;
            }
            cout << endl;
        }
    }
}

void VerilogParser::reconstructScanChains(const vector<VerilogInstance>& flipFlops) {
    // 建立連接圖
    map<string, string> ffToSiNet; // FF instance -> SI net
    map<string, string> ffToSoNet; // FF instance -> SO net
    map<string, string> siNetToFF; // SI net -> FF instance  
    map<string, string> soNetToFF; // SO net -> FF instance

    // 收集所有 scan 連接
    for (const auto& ff : flipFlops) {
        for (const auto& conn : ff.connections) {
            if (conn.first == "SI" || conn.first == "si") {
                ffToSiNet[ff.instName] = conn.second;
                if (conn.second != "UNCONNECTED" &&
                    conn.second.find("UNCONNECTED") == string::npos) {
                    siNetToFF[conn.second] = ff.instName;
                }
            }
            else if (conn.first == "SO" || conn.first == "so") {
                ffToSoNet[ff.instName] = conn.second;
                if (conn.second != "UNCONNECTED" &&
                    conn.second.find("UNCONNECTED") == string::npos) {
                    soNetToFF[conn.second] = ff.instName;
                }
            }
        }
    }

    // 找到 scan chain 的起始點（SI 沒有?動的 FF）
    vector<string> chainStarts;
    set<string> visited;

    for (const auto& ff : flipFlops) {
        string ffName = ff.instName;
        if (visited.count(ffName)) continue;

        string siNet = ffToSiNet[ffName];

        // 如果 SI 沒有連接或連接到外部（不是其他 FF 的 SO）
        if (siNet.empty() || siNet == "UNCONNECTED" ||
            siNet.find("UNCONNECTED") != string::npos ||
            soNetToFF.find(siNet) == soNetToFF.end()) {
            chainStarts.push_back(ffName);
        }
    }

    cout << "Found " << chainStarts.size() << " scan chain starting points:" << endl;

    int chainIndex = 1;
    for (const auto& start : chainStarts) {
        cout << "\n=== Scan Chain " << chainIndex++ << " ===" << endl;

        vector<string> chain;
        string current = start;
        set<string> chainVisited;

        while (!current.empty() && chainVisited.find(current) == chainVisited.end()) {
            chain.push_back(current);
            chainVisited.insert(current);
            visited.insert(current);

            // 找到下一? FF（通過 SO -> SI 連接）
            string soNet = ffToSoNet[current];
            string next = "";

            if (!soNet.empty() && soNet != "UNCONNECTED" &&
                soNet.find("UNCONNECTED") == string::npos) {
                // 找到由這? SO net ?動的 FF 的 SI
                auto it = siNetToFF.find(soNet);
                if (it != siNetToFF.end()) {
                    next = it->second;
                }
            }

            current = next;
        }

        // 打印這條 scan chain
        cout << "Chain length: " << chain.size() << " flip-flops" << endl;
        cout << "Chain sequence:" << endl;

        for (size_t i = 0; i < chain.size(); ++i) {
            string ffName = chain[i];
            string siNet = ffToSiNet[ffName];
            string soNet = ffToSoNet[ffName];

            cout << "  " << (i + 1) << ". " << ffName;
            if (!siNet.empty() && siNet != "UNCONNECTED") {
                cout << " (SI: " << siNet << ")";
            }
            if (!soNet.empty() && soNet != "UNCONNECTED") {
                cout << " (SO: " << soNet << ")";
            }
            cout << endl;

            // 如果不是最後一?，顯示連接
            if (i < chain.size() - 1) {
                cout << "     |" << endl;
                cout << "     v" << endl;
            }
        }
    }

    // 檢查是否有未訪?的 FF（可能形成環路或孤立）
    vector<string> unvisited;
    for (const auto& ff : flipFlops) {
        if (visited.find(ff.instName) == visited.end()) {
            unvisited.push_back(ff.instName);
        }
    }

    if (!unvisited.empty()) {
        cout << "\n=== Unvisited Flip-Flops (possible loops or isolated) ===" << endl;
        for (const auto& ffName : unvisited) {
            cout << "  " << ffName << endl;
        }
    }
}

// 添加一??化的 scan chain 打印方法
void VerilogParser::printScanChainSummary() {
    cout << "\n=== Scan Chain Summary ===" << endl;

    // 獲取所有 flip-flops
    vector<VerilogInstance> flipFlops;
    for (const auto& instance : instances_) {
        if (VerilogUtils::isFlipFlopCell(instance.cellType)) {
            flipFlops.push_back(instance);
        }
    }

    if (flipFlops.empty()) {
        cout << "No flip-flops found for scan chain analysis." << endl;
        return;
    }

    analyzeScanChains(flipFlops);
}

bool VerilogParser::parseFile(const string& filename) {
    ifstream in(filename);
    if (!in) {
        addError("Cannot open Verilog file: " + filename);
        return false;
    }

    try {
        clear();

        // Read entire file content
        string content((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
        in.close();

        cout << "Verilog file size: " << content.length() << " characters" << endl;

        // Preprocess content
        content = VerilogUtils::removeComments(content);
        content = VerilogUtils::normalizeWhitespace(content);

        cout << "After preprocessing: " << content.length() << " characters" << endl;

        // Parse modules
        size_t pos = 0;
        while (pos < content.length()) {
            if (content.find("module", pos) != string::npos) {
                if (!parseModule(content, pos)) {
                    addWarning("Failed to parse module starting at position " + to_string(pos));
                }
            }
            else {
                break;
            }
        }

        // Extract instance-pin-net mappings
        extractInstPinNets();

        isLoaded_ = true;
        return true;
    }
    catch (const exception& e) {
        addError("Error parsing Verilog file: " + string(e.what()));
        in.close();
        return false;
    }
}

bool VerilogParser::parseFromString(const string& content) {
    try {
        clear();

        string processedContent = VerilogUtils::removeComments(content);
        processedContent = VerilogUtils::normalizeWhitespace(processedContent);

        size_t pos = 0;
        while (pos < processedContent.length()) {
            if (processedContent.find("module", pos) != string::npos) {
                if (!parseModule(processedContent, pos)) {
                    addWarning("Failed to parse module starting at position " + to_string(pos));
                }
            }
            else {
                break;
            }
        }

        extractInstPinNets();
        isLoaded_ = true;
        return true;
    }
    catch (const exception& e) {
        addError("Error parsing Verilog string: " + string(e.what()));
        return false;
    }
}

bool VerilogParser::parseModule(const string& content, size_t& pos) {
    // Find module declaration
    size_t moduleStart = content.find("module", pos);
    if (moduleStart == string::npos) return false;

    size_t moduleEnd = content.find("endmodule", moduleStart);
    if (moduleEnd == string::npos) {
        addError("Module without endmodule found");
        return false;
    }

    string moduleContent = content.substr(moduleStart, moduleEnd - moduleStart + 9);

    VerilogModule module;

    // Parse module header
    smatch moduleMatch;
    if (regex_search(moduleContent, moduleMatch, moduleRegex_)) {
        module.name = moduleMatch[1];
        string portList = moduleMatch[2];

        cout << "Parsing module: " << module.name << endl;

        if (!parsePortList(portList, module)) {
            addWarning("Failed to parse port list for module: " + module.name);
        }
    }
    else {
        addError("Invalid module declaration");
        return false;
    }

    // Parse port declarations
    size_t declPos = 0;
    parsePortDeclarations(moduleContent, declPos, module);

    // Parse wire declarations
    declPos = 0;
    parseWireDeclarations(moduleContent, declPos, module);

    // Parse instances
    declPos = 0;
    parseInstances(moduleContent, declPos, module);

    modules_.push_back(module);
    pos = moduleEnd + 9;

    return true;
}

bool VerilogParser::parsePortList(const string& portList, VerilogModule& module) {
    if (portList.empty()) return true;

    vector<string> ports = VerilogUtils::splitPortList(portList);
    for (const string& port : ports) {
        string trimmedPort = port;
        // Remove whitespace
        trimmedPort.erase(remove_if(trimmedPort.begin(), trimmedPort.end(), ::isspace), trimmedPort.end());
        if (!trimmedPort.empty()) {
            module.ports.push_back(trimmedPort);
        }
    }

    return true;
}

bool VerilogParser::parsePortDeclarations(const string& content, size_t& pos, VerilogModule& module) {
    smatch match;
    string::const_iterator start = content.cbegin();

    while (regex_search(start, content.cend(), match, portDeclRegex_)) {
        string direction = match[1];
        string portNames = match[2];

        vector<string> ports = VerilogUtils::splitPortList(portNames);
        for (const string& port : ports) {
            string trimmedPort = port;
            trimmedPort.erase(remove_if(trimmedPort.begin(), trimmedPort.end(), ::isspace), trimmedPort.end());

            if (!trimmedPort.empty()) {
                module.portDirections[trimmedPort] = direction;
                if (direction == "input") {
                    module.inputs.push_back(trimmedPort);
                }
                else if (direction == "output") {
                    module.outputs.push_back(trimmedPort);
                }
            }
        }

        start = match.suffix().first;
    }

    return true;
}

bool VerilogParser::parseWireDeclarations(const string& content, size_t& pos, VerilogModule& module) {
    smatch match;
    string::const_iterator start = content.cbegin();

    while (regex_search(start, content.cend(), match, wireDeclRegex_)) {
        string wireNames = match[1];

        vector<string> wires = VerilogUtils::splitPortList(wireNames);
        for (const string& wire : wires) {
            string trimmedWire = wire;
            trimmedWire.erase(remove_if(trimmedWire.begin(), trimmedWire.end(), ::isspace), trimmedWire.end());

            if (!trimmedWire.empty()) {
                module.wires.push_back(trimmedWire);
            }
        }

        start = match.suffix().first;
    }

    return true;
}

bool VerilogParser::parseInstances(const string& content, size_t& pos, VerilogModule& module) {
    cout << "=== Starting instance parsing ===" << endl;
    cout << "Content length: " << content.length() << endl;

    int instanceCount = 0;

    // 使用正則表達式匹配?例，?理被壓縮的內容
    regex instancePattern(
        R"((SNPS\w+)\s+(\w+)\s*\(\s*((?:[^()]*\([^)]*\)[^()]*)*[^()]*)\s*\)\s*;)",
        regex::ECMAScript
    );

    sregex_iterator iter(content.begin(), content.end(), instancePattern);
    sregex_iterator end;

    cout << "Using regex to find instances..." << endl;

    for (; iter != end; ++iter) {
        const smatch& match = *iter;

        string cellType = match[1].str();
        string instName = match[2].str();
        string connections = match[3].str();

        VerilogInstance instance;
        instance.cellType = cellType;
        instance.instName = instName;

        // 解析連接
        regex pinRegex(R"(\.\s*(\w+)\s*\(\s*([^)]+)\s*\))");
        sregex_iterator pinIter(connections.begin(), connections.end(), pinRegex);
        sregex_iterator pinEnd;

        for (; pinIter != pinEnd; ++pinIter) {
            const smatch& pinMatch = *pinIter;
            string pinName = pinMatch[1].str();
            string netName = pinMatch[2].str();

            // 清理網路名稱
            netName.erase(remove_if(netName.begin(), netName.end(), ::isspace), netName.end());

            if (netName.find("SYNOPSYS_UNCONNECTED") != string::npos) {
                netName = "UNCONNECTED";
            }

            if (!pinName.empty() && !netName.empty()) {
                instance.connections.push_back({ pinName, netName });
            }
        }

        if (!instance.connections.empty()) {
            instances_.push_back(instance);
            instanceCount++;

            if (instanceCount % 1000 == 0) {
                cout << "  Parsed " << instanceCount << " instances..." << endl;
            }

            if (instanceCount <= 5) {
                cout << "  Instance " << instanceCount << ": "
                    << cellType << " " << instName << " with "
                    << instance.connections.size() << " connections" << endl;
            }
        }
    }

    cout << "Regex parsing completed: " << instanceCount << " instances found" << endl;
    return instanceCount > 0;
}

bool VerilogParser::parseInstanceFromString(const string& instStr) {
    // 清理字符串
    string cleanStr = instStr;
    cleanStr = regex_replace(cleanStr, regex(R"(\s+)"), " ");
    cleanStr = regex_replace(cleanStr, regex(R"(^\s+|\s+$)"), "");
    
    // ?單解析：CELLTYPE INSTNAME ( ... );
    istringstream iss(cleanStr);
    string cellType, instName;
    
    if (!(iss >> cellType >> instName)) {
        return false;
    }
    
    // 查找括?內容
    size_t parenStart = cleanStr.find('(');
    size_t parenEnd = cleanStr.rfind(')');
    
    if (parenStart == string::npos || parenEnd == string::npos || parenStart >= parenEnd) {
        return false;
    }
    
    string connectionStr = cleanStr.substr(parenStart + 1, parenEnd - parenStart - 1);
    
    // ?建?例
    VerilogInstance instance;
    instance.cellType = cellType;
    instance.instName = instName;
    
    // 解析連接
    if (parseInstanceConnections(connectionStr, instance)) {
        instances_.push_back(instance);
        
        // 調?：顯示前幾??例
        static int debugCount = 0;
        debugCount++;
        if (debugCount <= 5) {
            cout << "  Parsed: " << cellType << " " << instName 
                 << " with " << instance.connections.size() << " connections" << endl;
        }
        
        return true;
    }
    
    return false;
}

// ?化的連接解析函數
bool VerilogParser::parseInstanceConnections(const string& connectionStr, VerilogInstance& instance) {
    // 使用?單的字符串分割方法
    size_t pos = 0;
    
    while (pos < connectionStr.length()) {
        // 查找 .pin(net) 模式
        size_t dotPos = connectionStr.find('.', pos);
        if (dotPos == string::npos) break;
        
        size_t openParen = connectionStr.find('(', dotPos);
        if (openParen == string::npos) break;
        
        size_t closeParen = connectionStr.find(')', openParen);
        if (closeParen == string::npos) break;
        
        // 提取引腳名稱
        string pin = connectionStr.substr(dotPos + 1, openParen - dotPos - 1);
        pin.erase(remove_if(pin.begin(), pin.end(), ::isspace), pin.end());
        
        // 提取網路名稱
        string net = connectionStr.substr(openParen + 1, closeParen - openParen - 1);
        net = VerilogUtils::cleanNetName(net);
        
        if (!pin.empty() && !net.empty()) {
            instance.connections.push_back({ pin, net });
        }
        
        pos = closeParen + 1;
    }
    
    return !instance.connections.empty();
}
void VerilogParser::extractInstPinNets() {
    instPinNets_.clear();

    for (const auto& instance : instances_) {
        for (const auto& connection : instance.connections) {
            InstPinNet ipn;
            ipn.inst = instance.instName;
            ipn.pin = connection.first;
            ipn.net = connection.second;
            instPinNets_.push_back(ipn);
        }
    }

    cout << "Extracted " << instPinNets_.size() << " instance-pin-net mappings" << endl;
}

void VerilogParser::analyzeHierarchy() {
    cout << "\n=== Verilog Hierarchy Analysis ===" << endl;

    // Analyze modules
    cout << "Modules found: " << modules_.size() << endl;
    for (const auto& module : modules_) {
        cout << "  Module: " << module.name << endl;
        cout << "    Ports: " << module.ports.size() << " (I:" << module.inputs.size()
            << ", O:" << module.outputs.size() << ")" << endl;
        cout << "    Wires: " << module.wires.size() << endl;
    }

    // Analyze instances
    cout << "\nInstances found: " << instances_.size() << endl;

    // Cell type statistics
    auto cellStats = getCellTypeStatistics();
    cout << "\nCell type statistics:" << endl;

    // Categorize statistics
    int flipFlopCount = 0;
    int logicGateCount = 0;
    int bufferCount = 0;
    int otherCount = 0;

    vector<pair<string, int>> sortedStats(cellStats.begin(), cellStats.end());
    sort(sortedStats.begin(), sortedStats.end(),
        [](const pair<string, int>& a, const pair<string, int>& b) {
            return a.second > b.second;
        });

    for (const auto& stat : sortedStats) {
        string category = VerilogUtils::categorizeCell(stat.first);
        cout << "  " << left << setw(30) << stat.first
            << right << setw(6) << stat.second << " (" << category << ")" << endl;

        if (category == "FlipFlop") flipFlopCount += stat.second;
        else if (category == "Logic") logicGateCount += stat.second;
        else if (category == "Buffer") bufferCount += stat.second;
        else otherCount += stat.second;
    }

    cout << "\nSummary by category:" << endl;
    cout << "  Flip-Flops: " << flipFlopCount << endl;
    cout << "  Logic Gates: " << logicGateCount << endl;
    cout << "  Buffers/Inverters: " << bufferCount << endl;
    cout << "  Others: " << otherCount << endl;

    // Analyze flip-flop instances
    analyzeFlipFlopInstances();
}

void VerilogParser::analyzeFlipFlopInstances() {
    cout << "\n=== Flip-Flop Instance Analysis ===" << endl;

    vector<VerilogInstance> flipFlopInstances;
    map<string, vector<string>> ffTypeGroups;

    for (const auto& instance : instances_) {
        if (VerilogUtils::isFlipFlopCell(instance.cellType)) {
            flipFlopInstances.push_back(instance);
            ffTypeGroups[instance.cellType].push_back(instance.instName);
        }
    }

    cout << "Total flip-flop instances: " << flipFlopInstances.size() << endl;

    if (!flipFlopInstances.empty()) {
        cout << "\nFlip-flop types:" << endl;
        for (const auto& group : ffTypeGroups) {
            int bitWidth = VerilogUtils::extractBitWidth(group.first);
            cout << "  " << group.first << ": " << group.second.size()
                << " instances (" << bitWidth << "-bit)" << endl;

            // Show first few instance names
            if (group.second.size() <= 5) {
                cout << "    Instances: ";
                for (size_t i = 0; i < group.second.size(); ++i) {
                    if (i > 0) cout << ", ";
                    cout << group.second[i];
                }
                cout << endl;
            }
        }

        // Analyze clock connections
        analyzeClockConnections(flipFlopInstances);

        // Analyze scan chains
        analyzeScanChains(flipFlopInstances);
    }
}

void VerilogParser::analyzeClockConnections(const vector<VerilogInstance>& flipFlops) {
    cout << "\n=== Clock Connection Analysis ===" << endl;

    map<string, vector<string>> clockDomains;

    for (const auto& ff : flipFlops) {
        string clockNet = "";

        // Find clock pin connection
        for (const auto& conn : ff.connections) {
            if (VerilogUtils::isClockPin(conn.first)) {
                clockNet = conn.second;
                break;
            }
        }

        if (!clockNet.empty()) {
            clockDomains[clockNet].push_back(ff.instName);
        }
        else {
            clockDomains["NO_CLOCK"].push_back(ff.instName);
        }
    }

    cout << "Clock domains found: " << clockDomains.size() << endl;
    for (const auto& domain : clockDomains) {
        cout << "  Clock '" << domain.first << "': " << domain.second.size() << " flip-flops" << endl;
    }
}


void VerilogParser::findClockNets() {
    cout << "\n=== Clock Net Detection ===" << endl;

    set<string> clockNets;

    // Find clock signals from module ports
    for (const auto& module : modules_) {
        for (const auto& port : module.inputs) {
            if (VerilogUtils::isClockSignal(port)) {
                clockNets.insert(port);
            }
        }
    }

    // Find clock nets from instance connections
    for (const auto& instance : instances_) {
        for (const auto& conn : instance.connections) {
            if (VerilogUtils::isClockPin(conn.first)) {
                clockNets.insert(conn.second);
            }
        }
    }

    cout << "Clock nets detected: " << clockNets.size() << endl;
    for (const auto& clkNet : clockNets) {
        cout << "  " << clkNet << endl;
    }
}

const VerilogModule* VerilogParser::findModule(const string& name) const {
    auto it = find_if(modules_.begin(), modules_.end(),
        [&name](const VerilogModule& module) { return module.name == name; });
    return (it != modules_.end()) ? &(*it) : nullptr;
}

const VerilogInstance* VerilogParser::findInstance(const string& name) const {
    auto it = find_if(instances_.begin(), instances_.end(),
        [&name](const VerilogInstance& instance) { return instance.instName == name; });
    return (it != instances_.end()) ? &(*it) : nullptr;
}

vector<string> VerilogParser::getInstancesOfType(const string& cellType) const {
    vector<string> result;
    for (const auto& instance : instances_) {
        if (instance.cellType == cellType) {
            result.push_back(instance.instName);
        }
    }
    return result;
}

vector<string> VerilogParser::getNetsConnectedToInstance(const string& instName) const {
    vector<string> result;
    for (const auto& ipn : instPinNets_) {
        if (ipn.inst == instName) {
            if (find(result.begin(), result.end(), ipn.net) == result.end()) {
                result.push_back(ipn.net);
            }
        }
    }
    return result;
}

unordered_map<string, int> VerilogParser::getCellTypeStatistics() const {
    unordered_map<string, int> stats;
    for (const auto& instance : instances_) {
        stats[instance.cellType]++;
    }
    return stats;
}

void VerilogParser::clear() {
    modules_.clear();
    instances_.clear();
    instPinNets_.clear();
    errors_.clear();
    warnings_.clear();
    isLoaded_ = false;
}

void VerilogParser::printSummary() const {
    cout << "\n=== Verilog Parser Summary ===" << endl;

    // Basic statistics
    cout << "Modules: " << modules_.size() << endl;
    cout << "Instances: " << instances_.size() << endl;
    cout << "Instance-Pin-Net mappings: " << instPinNets_.size() << endl;

    if (!modules_.empty()) {
        cout << "\nTop module: " << modules_[0].name << endl;
        cout << "  Input ports: " << modules_[0].inputs.size() << endl;
        cout << "  Output ports: " << modules_[0].outputs.size() << endl;
        cout << "  Internal wires: " << modules_[0].wires.size() << endl;
    }

    // Component type statistics
    auto cellStats = getCellTypeStatistics();
    cout << "\nCell type summary:" << endl;

    int totalFF = 0, totalLogic = 0, totalBuffer = 0, totalOther = 0;

    for (const auto& stat : cellStats) {
        string category = VerilogUtils::categorizeCell(stat.first);
        if (category == "FlipFlop") totalFF += stat.second;
        else if (category == "Logic") totalLogic += stat.second;
        else if (category == "Buffer") totalBuffer += stat.second;
        else totalOther += stat.second;
    }

    double total = static_cast<double>(instances_.size());
    if (total > 0) {
        cout << "  Flip-Flops: " << totalFF << " ("
            << fixed << setprecision(1) << (100.0 * totalFF / total) << "%)" << endl;
        cout << "  Logic Gates: " << totalLogic << " ("
            << fixed << setprecision(1) << (100.0 * totalLogic / total) << "%)" << endl;
        cout << "  Buffers/Inverters: " << totalBuffer << " ("
            << fixed << setprecision(1) << (100.0 * totalBuffer / total) << "%)" << endl;
        cout << "  Others: " << totalOther << " ("
            << fixed << setprecision(1) << (100.0 * totalOther / total) << "%)" << endl;
    }

    // Show main flip-flop types
    if (totalFF > 0) {
        cout << "\nMain flip-flop types:" << endl;
        vector<pair<string, int>> ffTypes;

        for (const auto& stat : cellStats) {
            if (VerilogUtils::isFlipFlopCell(stat.first)) {
                ffTypes.push_back(stat);
            }
        }

        sort(ffTypes.begin(), ffTypes.end(),
            [](const pair<string, int>& a, const pair<string, int>& b) {
                return a.second > b.second;
            });

        for (size_t i = 0; i < min(ffTypes.size(), size_t(5)); ++i) {
            int bitWidth = VerilogUtils::extractBitWidth(ffTypes[i].first);
            cout << "  " << ffTypes[i].first << ": " << ffTypes[i].second
                << " instances (" << bitWidth << "-bit)" << endl;
        }
    }

    // Clock signal detection
    set<string> clockNets;
    for (const auto& instance : instances_) {
        for (const auto& conn : instance.connections) {
            if (VerilogUtils::isClockPin(conn.first)) {
                clockNets.insert(conn.second);
            }
        }
    }

    if (!clockNets.empty()) {
        cout << "\nClock signals detected: " << clockNets.size() << endl;
        for (const auto& clk : clockNets) {
            cout << "  " << clk << endl;
        }
    }
}

void VerilogParser::printModules() const {
    cout << "\n=== Verilog Modules ===" << endl;
    for (const auto& module : modules_) {
        cout << "Module: " << module.name << endl;
        cout << "  Ports: " << module.ports.size() << endl;
        cout << "  Inputs: " << module.inputs.size() << endl;
        cout << "  Outputs: " << module.outputs.size() << endl;
        cout << "  Wires: " << module.wires.size() << endl;
    }
}

void VerilogParser::printInstances() const {
    cout << "\n=== Verilog Instances (First 10) ===" << endl;
    for (size_t i = 0; i < min(instances_.size(), size_t(10)); ++i) {
        const auto& instance = instances_[i];
        cout << "Instance: " << instance.instName << " (" << instance.cellType << ")" << endl;
        for (const auto& conn : instance.connections) {
            cout << "  ." << conn.first << "(" << conn.second << ")" << endl;
        }
    }
    if (instances_.size() > 10) {
        cout << "... and " << (instances_.size() - 10) << " more instances" << endl;
    }
}

bool VerilogParser::validateVerilog() const {
    // Basic validation
    bool isValid = true;

    if (modules_.empty()) {
        const_cast<VerilogParser*>(this)->addWarning("No modules found in Verilog file");
        isValid = false;
    }

    return isValid;
}

bool VerilogParser::writeVerilogFile(const string& filename) const {
    ofstream vFile(filename);
    if (!vFile.is_open()) {
        return false;
    }

    try {
        for (const auto& module : modules_) {
            vFile << "module " << module.name << " (";

            // Write port list
            for (size_t i = 0; i < module.ports.size(); ++i) {
                if (i > 0) vFile << ", ";
                vFile << module.ports[i];
            }
            vFile << ");" << endl << endl;

            // Write port declarations
            for (const auto& input : module.inputs) {
                vFile << "input " << input << ";" << endl;
            }
            for (const auto& output : module.outputs) {
                vFile << "output " << output << ";" << endl;
            }
            vFile << endl;

            // Write wire declarations
            for (const auto& wire : module.wires) {
                vFile << "wire " << wire << ";" << endl;
            }
            vFile << endl;

            vFile << "endmodule" << endl << endl;
        }

        // Write instances
        for (const auto& instance : instances_) {
            vFile << instance.cellType << " " << instance.instName << " (";
            for (size_t i = 0; i < instance.connections.size(); ++i) {
                if (i > 0) vFile << ", ";
                vFile << "." << instance.connections[i].first << "(" << instance.connections[i].second << ")";
            }
            vFile << ");" << endl;
        }

        vFile.close();
        return true;
    }
    catch (const exception& e) {
        vFile.close();
        return false;
    }
}

bool VerilogParser::writeInstPinNetMapping(const string& filename) const {
    ofstream mapFile(filename);
    if (!mapFile.is_open()) {
        return false;
    }

    try {
        mapFile << "# Instance-Pin-Net Mapping" << endl;
        mapFile << "# Generated by VerilogParser" << endl;
        mapFile << "# Format: Instance Pin Net" << endl << endl;

        for (const auto& ipn : instPinNets_) {
            mapFile << ipn.inst << " " << ipn.pin << " " << ipn.net << endl;
        }

        mapFile.close();
        return true;
    }
    catch (const exception& e) {
        mapFile.close();
        return false;
    }
}


string VerilogParser::toString() const {
    ostringstream oss;
    oss << "VerilogParser Summary:" << endl;
    oss << "  Modules: " << modules_.size() << endl;
    oss << "  Instances: " << instances_.size() << endl;
    oss << "  Instance-Pin-Net mappings: " << instPinNets_.size() << endl;
    return oss.str();
}

void VerilogParser::addError(const string& error) {
    errors_.push_back(error);
    cerr << "Verilog Error: " << error << endl;
}

void VerilogParser::addWarning(const string& warning) {
    warnings_.push_back(warning);
    cout << "Verilog Warning: " << warning << endl;
}

// Utility functions
namespace VerilogUtils {
    string removeComments(const string& content) {
        string result;
        bool inLineComment = false;
        bool inBlockComment = false;

        for (size_t i = 0; i < content.length(); ++i) {
            if (!inLineComment && !inBlockComment) {
                if (i + 1 < content.length() && content[i] == '/' && content[i + 1] == '/') {
                    inLineComment = true;
                    ++i;
                }
                else if (i + 1 < content.length() && content[i] == '/' && content[i + 1] == '*') {
                    inBlockComment = true;
                    ++i;
                }
                else {
                    result += content[i];
                }
            }
            else if (inLineComment) {
                if (content[i] == '\n') {
                    inLineComment = false;
                    result += content[i];
                }
            }
            else if (inBlockComment) {
                if (i + 1 < content.length() && content[i] == '*' && content[i + 1] == '/') {
                    inBlockComment = false;
                    ++i;
                }
            }
        }

        return result;
    }

    string normalizeWhitespace(const string& content) {
        string result;
        bool inSpace = false;

        for (char c : content) {
            if (isspace(c)) {
                if (!inSpace) {
                    result += ' ';
                    inSpace = true;
                }
            }
            else {
                result += c;
                inSpace = false;
            }
        }

        return result;
    }

    vector<string> splitPortList(const string& portList) {
        vector<string> result;
        stringstream ss(portList);
        string port;

        while (getline(ss, port, ',')) {
            // Trim whitespace
            port.erase(0, port.find_first_not_of(" \t\r\n"));
            port.erase(port.find_last_not_of(" \t\r\n") + 1);
            if (!port.empty()) {
                result.push_back(port);
            }
        }

        return result;
    }

    bool isValidIdentifier(const string& identifier) {
        if (identifier.empty()) return false;
        if (!isalpha(identifier[0]) && identifier[0] != '_') return false;

        for (char c : identifier) {
            if (!isalnum(c) && c != '_') return false;
        }

        return true;
    }

    string extractNetName(const string& connection) {
        return cleanNetName(connection);
    }

    string cleanNetName(const string& netName) {
        string result = netName;

        // Remove whitespace
        result.erase(remove_if(result.begin(), result.end(), ::isspace), result.end());

        // Handle escape characters (Verilog \bus[0] format)
        if (result.find("\\") != string::npos) {
            // Keep escaped bus signal names
            return result;
        }

        // Remove SYNOPSYS_UNCONNECTED etc
        if (result.find("SYNOPSYS_UNCONNECTED") != string::npos) {
            return "UNCONNECTED";
        }

        return result;
    }

    bool isClockSignal(const string& signalName) {
        string lower = signalName;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return (lower.find("clk") != string::npos ||
            lower.find("clock") != string::npos ||
            lower == "ck" || lower == "cp");
    }

    bool isFlipFlopCell(const string& cellType) {
        // Check your flip-flop types: FSDNQ and FSDN
        if (cellType.find("FSDNQ") != string::npos ||
            cellType.find("FSDN") != string::npos) {
            return true;
        }

        // Standard flip-flop patterns
        return (cellType.find("FF") != string::npos ||
            cellType.find("DFF") != string::npos ||
            cellType.find("SDFF") != string::npos ||
            cellType.find("LATCH") != string::npos ||
            cellType.find("_FF_") != string::npos ||
            cellType.find("FLIP") != string::npos);
    }

    string categorizeCell(const string& cellType) {
        if (isFlipFlopCell(cellType)) return "FlipFlop";

        if (cellType.find("OR") != string::npos ||
            cellType.find("AND") != string::npos ||
            cellType.find("AN") != string::npos ||
            cellType.find("NAND") != string::npos ||
            cellType.find("NOR") != string::npos ||
            cellType.find("XOR") != string::npos) return "Logic";

        if (cellType.find("INV") != string::npos ||
            cellType.find("BUF") != string::npos) return "Buffer";

        return "Other";
    }

    int extractBitWidth(const string& cellType) {
        // Extract bit width from component name, e.g. FSDNQ_V3_4 -> 4-bit
        regex endNumberPattern(R"(_(\d+)$)");
        smatch m;
        if (regex_search(cellType, m, endNumberPattern)) {
            return stoi(m[1]);
        }

        // Other patterns
        regex bitPattern(R"((\d+)BIT|(\d+)B|_(\d+)_)");
        if (regex_search(cellType, m, bitPattern)) {
            for (int i = 1; i <= 3; i++) {
                if (m[i].matched) {
                    return stoi(m[i]);
                }
            }
        }

        return 1; // Default single bit
    }

    bool isClockPin(const string& pinName) {
        string upper = pinName;
        transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

        return (upper == "CK" || upper == "CLK" || upper == "CLOCK" ||
            upper == "CP" || upper == "C");
    }

    bool isScanPin(const string& pinName) {
        string upper = pinName;
        transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

        return (upper == "SI" || upper == "SO" ||
            upper == "SE" || upper == "SCAN_EN" ||
            upper == "SCAN_IN" || upper == "SCAN_OUT");
    }

    bool isDataPin(const string& pinName) {
        string upper = pinName;
        transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

        return (upper.find("D") == 0 || upper.find("Q") == 0);
    }
}