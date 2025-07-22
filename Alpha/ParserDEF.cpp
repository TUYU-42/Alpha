#include "ParserDEF.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <map>
#include <set>

using namespace std;

DefParser::DefParser() : isLoaded_(false) {
    // Initialize regex patterns
    dieAreaRegex_ = std::regex(R"(^\s*DIEAREA\s*((\(\s*\d+\s+\d+\s*\)\s*)+);)");
    unitsRegex_ = std::regex(R"(^\s*UNITS\s+DISTANCE\s+MICRONS\s+(\d+)\s*;)");
    rowRegex_ = regex(R"(^\s*ROW\s+(\w+)\s+\w+\s+(\d+)\s+(\d+)\s+([A-Z]{1,2})\s+DO\s+(\d+)\s+BY\s+(\d+)\s+STEP\s+(\d+)\s+(\d+)\s*;)");
    trackRegex_ = regex(R"(^\s*TRACKS\s+([XY])\s+(\d+)\s+DO\s+(\d+)\s+STEP\s+(\d+)\s+LAYER\s+(\w+)\s*;)");
    componentRegex_ = regex(R"(^\s*-\s+(\S+)\s+(\S+)\s+\+\s+PLACED\s+\(\s*(\d+)\s+(\d+)\s*\)\s+(\S+)\s*;)");
    pinStartRegex_ = regex(R"(^\s*-\s+(\S+)\s+\+\s+NET\s+(\S+)\s+\+\s+DIRECTION\s+(\w+)\s+\+\s+USE\s+(\w+))");
    pinLayerRegex_ = regex(R"(^\s*\+\s+LAYER\s+(\S+)\s+\(\s*(\d+)\s+(\d+)\s*\)\s+\(\s*(\d+)\s+(\d+)\s*\))");
    pinPlacedRegex_ = regex(R"(^\s*\+\s+PLACED\s+\(\s*(\d+)\s+(\d+)\s*\)\s+(\S+)\s*;)");
    netStartRegex_ = regex(R"(^\s*-\s+(\S+))");
    netPinRegex_ = regex(R"(^\s*\(\s*(\S+)\s+(\S+)\s*\))");
    netUseRegex_ = regex(R"(^\s*\+\s+USE\s+(\S+)\s*;)");

    // Add new regex patterns for scan chains
    scanChainLineRegex_ = regex(R"(^\s*-\s*(\S+)\s+(.*);)");
}

bool DefParser::parseFile(const string& filename) {
    ifstream in(filename);
    if (!in) {
        addError("Cannot open DEF file: " + filename);
        return false;
    }

    try {
        clear();
        string line;
        int lineCount = 0;
        int componentCount = 0;
        bool inScanChains = false;
        bool inExtension = false;
        bool inScanDef = false;

        cout << "Reading DEF file line by line..." << endl;

        while (getline(in, line)) {
            lineCount++;
            if (line.empty() || line[0] == '#') continue;

            // Check for SCANCHAINS section
            if (line.find("SCANCHAINS") != string::npos && line.find("END SCANCHAINS") == string::npos) {
                inScanChains = true;
                cout << "  Found SCANCHAINS section at line " << lineCount << endl;
                continue;
            }
            if (inScanChains && line.find("END SCANCHAINS") != string::npos) {
                inScanChains = false;
                cout << "  End of SCANCHAINS section at line " << lineCount << endl;
                continue;
            }

            // Check for EXTENSION SCANDEF section
            if (line.find("EXTENSION") != string::npos && line.find("SCANDEF") != string::npos) {
                inExtension = true;
                inScanDef = true;
                cout << "  Found EXTENSION SCANDEF section at line " << lineCount << endl;
                continue;
            }
            if (inScanDef && line.find("END SCANDEF") != string::npos) {
                inScanDef = false;
                cout << "  End of SCANDEF section at line " << lineCount << endl;
                continue;
            }
            if (inExtension && line.find("END EXTENSION") != string::npos) {
                inExtension = false;
                cout << "  End of EXTENSION section at line " << lineCount << endl;
                continue;
            }

            // Parse scan chain data
            if (inScanChains || inScanDef) {
                parseScanChainLine(line);
                continue;
            }

            smatch m;
            if (regex_search(line, m, unitsRegex_)) {
                defData_.units = stoi(m[1]);
                continue;
            }

            // Parse DIEAREA
            if (std::regex_search(line, m, dieAreaRegex_)) {
                std::vector<int> coords;
                static const std::regex coordPattern(R"(\(\s*(\d+)\s+(\d+)\s*\))");
                std::sregex_iterator it(line.begin(), line.end(), coordPattern), end;
                for (; it != end; ++it) {
                    coords.push_back(stoi((*it)[1]));
                    coords.push_back(stoi((*it)[2]));
                }
                int xMin = INT_MAX, xMax = INT_MIN, yMin = INT_MAX, yMax = INT_MIN;
                for (size_t i = 0; i < coords.size(); i += 2) {
                    int x = coords[i], y = coords[i + 1];
                    xMin = std::min(xMin, x); xMax = std::max(xMax, x);
                    yMin = std::min(yMin, y); yMax = std::max(yMax, y);
                }
                defData_.dieArea = { xMin, yMin, xMax, yMax };
                continue;
            }

            // Parse different sections
            if (parseRowInfo(line)) continue;
            if (parseTrackInfo(line)) continue;
            if (parseComponentInfo(line)) {
                componentCount++;
                if (componentCount % 1000 == 0) {
                    cout << "  Parsed " << componentCount << " components..." << endl;
                }
                continue;
            }
            if (parsePinInfo(in, line)) continue;
            if (parseNetInfo(in, line)) continue;
        }

        in.close();
        isLoaded_ = true;

        cout << "DEF parsing completed:" << endl;
        cout << "  Lines processed: " << lineCount << endl;
        cout << "  Components found: " << defData_.components.size() << endl;
        cout << "  Nets found: " << defData_.nets.size() << endl;
        cout << "  Pins found: " << defData_.pins.size() << endl;
        cout << "  Scan chains found: " << defData_.scanChains.size() << endl;

        // Post-processing
        cout << "Starting flip-flop analysis..." << endl;
        analyzeFlipFlops();

        cout << "Final flip-flop count: " << defData_.flipFlops.size() << endl;

        return true;
    }
    catch (const exception& e) {
        addError("Error parsing DEF file: " + string(e.what()));
        in.close();
        return false;
    }
}

bool DefParser::parseScanChainLine(const string& line) {
    smatch m;
    if (regex_search(line, m, scanChainLineRegex_)) {
        ScanChain sc;
        sc.name = m[1];

        // Parse the FF names from the rest of the line
        string ffList = m[2];
        istringstream iss(ffList);
        string ff;

        while (iss >> ff) {
            if (ff != ";" && !ff.empty()) {
                sc.ffNames.push_back(ff);
            }
        }

        if (!sc.ffNames.empty()) {
            defData_.scanChains.push_back(sc);
            cout << "    Parsed scan chain '" << sc.name << "' with "
                << sc.ffNames.size() << " FFs" << endl;
        }

        return true;
    }
    return false;
}

void DefParser::printScanChains() const {
    cout << "\n=== Scan Chains Summary ===" << endl;
    cout << "Found " << defData_.scanChains.size() << " scan chains." << endl;

    for (const auto& sc : defData_.scanChains) {
        cout << "  Chain: " << sc.name << " -> ";
        for (size_t i = 0; i < sc.ffNames.size(); ++i) {
            cout << sc.ffNames[i];
            if (i < sc.ffNames.size() - 1) cout << " -> ";
        }
        cout << " (" << sc.ffNames.size() << " FFs)" << endl;
    }
}

pair<int, int> DefParser::findFFinScanChains(const string& ffName) const {
    for (size_t i = 0; i < defData_.scanChains.size(); ++i) {
        const auto& chain = defData_.scanChains[i].ffNames;
        auto it = find(chain.begin(), chain.end(), ffName);
        if (it != chain.end()) {
            return { i, int(it - chain.begin()) };
        }
    }
    return { -1, -1 }; // Not found
}

// Keep all existing methods from original file...
bool DefParser::parseFromString(const string& content) {
    try {
        clear();
        istringstream iss(content);
        string line;

        while (getline(iss, line)) {
            if (line.empty() || line[0] == '#') continue;

            // Parse different sections
            parseRowInfo(line);
            parseTrackInfo(line);
            parseComponentInfo(line);
            // Note: Pin and Net parsing need file stream for multi-line parsing
            // This is a simplified version for string parsing
        }

        isLoaded_ = true;
        analyzeFlipFlops();
        return true;
    }
    catch (const exception& e) {
        addError("Error parsing DEF string: " + string(e.what()));
        return false;
    }
}

bool DefParser::parseRowInfo(const string& line) {
    smatch m;
    if (regex_search(line, m, rowRegex_)) {
        RowInfo row;
        row.name = m[1];
        row.x = stoi(m[2]);
        row.y = stoi(m[3]);
        row.orientation = m[4];
        row.count = stoi(m[5]);
        row.by = stoi(m[6]);
        row.stepX = stoi(m[7]);
        row.stepY = stoi(m[8]);
        defData_.rows.push_back(row);
        return true;
    }
    return false;
}

bool DefParser::parseTrackInfo(const string& line) {
    smatch m;
    if (regex_search(line, m, trackRegex_)) {
        TrackInfo track;
        track.direction = m[1].str()[0];
        track.start = stoi(m[2]);
        track.count = stoi(m[3]);
        track.step = stoi(m[4]);
        track.layer = m[5];
        defData_.tracks.push_back(track);
        return true;
    }
    return false;
}

bool DefParser::parseComponentInfo(const string& line) {
    smatch m;
    if (regex_search(line, m, componentRegex_)) {
        ComponentInfo comp;
        comp.name = m[1];
        comp.cellType = m[2];
        comp.x = stoi(m[3]);
        comp.y = stoi(m[4]);
        comp.orient = m[5];

        // Update existing component or add new
        auto* existing = findComponent(comp.name);
        if (existing) {
            existing->x = comp.x;
            existing->y = comp.y;
            existing->orient = comp.orient;
            if (existing->cellType.empty()) {
                existing->cellType = comp.cellType;
            }
        }
        else {
            defData_.components.push_back(comp);
        }
        return true;
    }
    return false;
}

bool DefParser::parsePinInfo(ifstream& file, const string& line) {
    smatch m;
    if (regex_search(line, m, pinStartRegex_)) {
        PinInfo pin;
        pin.name = m[1];
        pin.netName = m[2];
        pin.direction = m[3];
        pin.use = m[4];

        string nextLine;

        // Parse layer information
        if (getline(file, nextLine)) {
            smatch layerMatch;
            if (regex_search(nextLine, layerMatch, pinLayerRegex_)) {
                pin.layer = layerMatch[1];
                pin.layerX1 = stoi(layerMatch[2]);
                pin.layerY1 = stoi(layerMatch[3]);
                pin.layerX2 = stoi(layerMatch[4]);
                pin.layerY2 = stoi(layerMatch[5]);
            }
        }

        // Parse placement information
        if (getline(file, nextLine)) {
            smatch placedMatch;
            if (regex_search(nextLine, placedMatch, pinPlacedRegex_)) {
                pin.placedX = stoi(placedMatch[1]);
                pin.placedY = stoi(placedMatch[2]);
                pin.orient = placedMatch[3];
            }
        }

        defData_.pins.push_back(pin);
        return true;
    }
    return false;
}

bool DefParser::parseNetInfo(ifstream& file, const string& line) {
    smatch m;
    if (regex_search(line, m, netStartRegex_)) {
        NetInfo net;
        net.name = m[1];

        string nextLine;
        while (getline(file, nextLine)) {
            smatch pinMatch;
            if (regex_search(nextLine, pinMatch, netPinRegex_)) {
                NetPin netPin;
                netPin.instance = pinMatch[1];
                netPin.pin = pinMatch[2];
                net.connections.push_back(netPin);
            }
            else {
                smatch useMatch;
                if (regex_search(nextLine, useMatch, netUseRegex_)) {
                    net.use = useMatch[1];
                    break;
                }
            }
        }

        defData_.nets.push_back(net);
        return true;
    }
    return false;
}


void DefParser::analyzeFlipFlops() {
    cout << "\n=== Analyzing Flip-Flops (Debug Version) ===" << endl;

    defData_.flipFlops.clear();
    defData_.clockDomains.clear();

    // Debug information
    cout << "Debug Info:" << endl;
    cout << "  Total components: " << defData_.components.size() << endl;
    cout << "  InstPinNets size: " << defData_.instPinNets.size() << endl;
    cout << "  Nets size: " << defData_.nets.size() << endl;

    // Component statistics
    map<string, int> componentStats;
    int totalFlipFlops = 0;
    int debugCheckedComponents = 0;

    cout << "\nScanning components for flip-flops..." << endl;

    for (const auto& comp : defData_.components) {
        debugCheckedComponents++;

        // Debug: Show first 10 components being checked
        if (debugCheckedComponents <= 10) {
            cout << "  Checking component " << debugCheckedComponents << ": "
                << comp.name << " (" << comp.cellType << ") -> ";
        }

        string category = DefUtils::getCellCategory(comp.cellType);
        componentStats[category]++;

        bool isFF = DefUtils::isFlipFlopCell(comp.cellType);

        if (debugCheckedComponents <= 10) {
            cout << (isFF ? "FLIP-FLOP" : category) << endl;
        }

        if (isFF) {
            totalFlipFlops++;
            cout << "*** FOUND FLIP-FLOP " << totalFlipFlops << ": "
                << comp.name << " (" << comp.cellType << ")" << endl;

            FlipFlopInfo ff;
            ff.instName = comp.name;
            ff.cellType = comp.cellType;
            ff.x = comp.x;
            ff.y = comp.y;
            ff.orient = comp.orient;
            ff.bitWidth = DefUtils::getBitWidth(comp.cellType);
            ff.isMultiBit = (ff.bitWidth > 1);

            cout << "    Bit width: " << ff.bitWidth << endl;
            cout << "    Position: (" << ff.x << ", " << ff.y << ")" << endl;

            // Find pin connections from InstPinNets
            int pinConnectionsFound = 0;
            for (const auto& ipn : defData_.instPinNets) {
                if (ipn.inst == comp.name) {

                    pinConnectionsFound++;
                    if (ipn.pin == "CK" || ipn.pin == "CLK" || ipn.pin == "CP") {
                        ff.clockNet = ipn.net;
                        cout << "    Clock: " << ipn.pin << " -> " << ipn.net << endl;
                    }
                    else if (ipn.pin.find("D") == 0) {
                        ff.dataPins.push_back(ipn.net);
                        cout << "    Data: " << ipn.pin << " -> " << ipn.net << endl;
                    }
                    else if (ipn.pin.find("Q") == 0) {
                        ff.outputPins.push_back(ipn.net);
                        cout << "    Output: " << ipn.pin << " -> " << ipn.net << endl;
                    }
                    else if (ipn.pin == "SI") {
                        ff.scanIn = ipn.net;
                        cout << "    Scan Input: " << ipn.pin << " -> " << ipn.net << endl;
                    }
                    else if (ipn.pin == "SO") {
                        ff.scanOut = ipn.net;
                        cout << "    Scan Output: " << ipn.pin << " -> " << ipn.net << endl;
                    }

                }
            }

            // Find connections from nets (alternative method)
            int netConnectionsFound = 0;
            for (const auto& net : defData_.nets) {
                for (const auto& conn : net.connections) {
                    if (conn.instance == comp.name) {
                        netConnectionsFound++;
                        string upperPin = conn.pin;
                        transform(upperPin.begin(), upperPin.end(), upperPin.begin(), ::toupper);

                        if ((upperPin == "CK" || upperPin == "CLK") && ff.clockNet.empty()) {
                            ff.clockNet = net.name;
                            cout << "    Clock (from nets): " << conn.pin << " -> " << net.name << endl;
                        }
                    }
                }
            }

            cout << "    Pin connections from InstPinNets: " << pinConnectionsFound << endl;
            cout << "    Pin connections from Nets: " << netConnectionsFound << endl;

            defData_.flipFlops.push_back(ff);

            // Update clock domains
            if (!ff.clockNet.empty()) {
                defData_.clockDomains[ff.clockNet].push_back(ff.instName);
            }
            else {
                defData_.clockDomains["NO_CLOCK"].push_back(ff.instName);
            }
        }
    }

    cout << "\n=== Component Statistics ===" << endl;
    cout << "Components checked: " << debugCheckedComponents << endl;
    cout << "Total components: " << defData_.components.size() << endl;
    for (const auto& stat : componentStats) {
        cout << stat.first << ": " << stat.second << " instances" << endl;
    }

    cout << "\n=== Flip-Flop Analysis Results ===" << endl;
    cout << "Total flip-flops found: " << totalFlipFlops << endl;
    cout << "Flip-flops stored in defData_: " << defData_.flipFlops.size() << endl;

    // Show sample flip-flops
    if (!defData_.flipFlops.empty()) {
        cout << "\nSample flip-flops (first 5):" << endl;
        for (size_t i = 0; i < min(defData_.flipFlops.size(), size_t(5)); ++i) {
            const auto& ff = defData_.flipFlops[i];
            cout << "  " << (i + 1) << ". " << ff.instName << " (" << ff.cellType << ") "
                << ff.bitWidth << "-bit" << endl;
        }

        // Group statistics by type
        map<string, int> ffTypeStats;
        for (const auto& ff : defData_.flipFlops) {
            if (ff.cellType.find("FSDNQ") != string::npos) {
                ffTypeStats["FSDNQ"]++;
            }
            else if (ff.cellType.find("FSDN") != string::npos) {
                ffTypeStats["FSDN"]++;
            }
            else {
                ffTypeStats["OTHER_FF"]++;
            }
        }

        cout << "\nFlip-flop type statistics:" << endl;
        for (const auto& stat : ffTypeStats) {
            cout << "  " << stat.first << " type: " << stat.second << " instances" << endl;
        }
    }
    else {
        cout << "\n*** WARNING: No flip-flops found! ***" << endl;
        cout << "This could be due to:" << endl;
        cout << "1. Flip-flop detection patterns not matching your cell types" << endl;
        cout << "2. No components parsed from DEF file" << endl;
        cout << "3. Different naming convention than expected" << endl;

        // Show some sample component types for debugging
        cout << "\nSample component types found (first 10):" << endl;
        int count = 0;
        set<string> uniqueTypes;
        for (const auto& comp : defData_.components) {
            if (uniqueTypes.insert(comp.cellType).second && count < 10) {
                cout << "  " << comp.cellType << " -> "
                    << (DefUtils::isFlipFlopCell(comp.cellType) ? "FF" : "NOT FF") << endl;
                count++;
            }
        }
    }

    cout << "\nClock domain analysis:" << endl;
    if (defData_.clockDomains.empty()) {
        cout << "  No clock domains found" << endl;
    }
    else {
        for (const auto& domain : defData_.clockDomains) {
            cout << "  Clock '" << domain.first << "': " << domain.second.size() << " flip-flops" << endl;
        }
    }

    // Final verification
    cout << "\n=== Final Verification ===" << endl;
    cout << "defData_.flipFlops.size() = " << defData_.flipFlops.size() << endl;
    cout << "getFlipFlopCount() will return: " << defData_.flipFlops.size() << endl;
}

void DefParser::updateComponentFromVerilog(const vector<InstPinNet>& verilogData) {
    for (const auto& ipn : verilogData) {
        defData_.instPinNets.push_back(ipn);

        // Update nets
        auto* net = findNet(ipn.net);
        if (!net) {
            NetInfo newNet;
            newNet.name = ipn.net;
            newNet.use = "SIGNAL";
            newNet.connections.push_back({ ipn.inst, ipn.pin });
            defData_.nets.push_back(newNet);
        }
        else {
            // Check if connection already exists
            bool exists = false;
            for (const auto& conn : net->connections) {
                if (conn.instance == ipn.inst && conn.pin == ipn.pin) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                net->connections.push_back({ ipn.inst, ipn.pin });
            }
        }
    }
}

ComponentInfo* DefParser::findComponent(const string& name) {
    auto it = find_if(defData_.components.begin(), defData_.components.end(),
        [&name](const ComponentInfo& comp) { return comp.name == name; });
    return (it != defData_.components.end()) ? &(*it) : nullptr;
}

NetInfo* DefParser::findNet(const string& name) {
    auto it = find_if(defData_.nets.begin(), defData_.nets.end(),
        [&name](const NetInfo& net) { return net.name == name; });
    return (it != defData_.nets.end()) ? &(*it) : nullptr;
}

void DefParser::clear() {
    defData_.rows.clear();
    defData_.tracks.clear();
    defData_.components.clear();
    defData_.pins.clear();
    defData_.nets.clear();
    defData_.instPinNets.clear();
    defData_.flipFlops.clear();
    defData_.clockDomains.clear();
    defData_.scanChains.clear();  // Clear scan chains
    errors_.clear();
    warnings_.clear();
    isLoaded_ = false;
}

void DefParser::printSummary() const {
    cout << "\n=== DEF Parser Summary ===" << endl;
    cout << "Rows: " << defData_.rows.size() << endl;
    cout << "Tracks: " << defData_.tracks.size() << endl;
    cout << "Components: " << defData_.components.size() << endl;
    cout << "Pins: " << defData_.pins.size() << endl;
    cout << "Nets: " << defData_.nets.size() << endl;
    cout << "Flip-Flops: " << defData_.flipFlops.size() << endl;
    cout << "Scan Chains: " << defData_.scanChains.size() << endl;  // Add scan chains

    if (!defData_.flipFlops.empty()) {
        int totalBits = 0;
        for (const auto& ff : defData_.flipFlops) {
            totalBits += ff.bitWidth;
        }
        cout << "Total flip-flop bits: " << totalBits << endl;
    }
}

// Include all other methods from original file...

// Utility functions
namespace DefUtils {
    bool isFlipFlopCell(const string& cellType) {
        // Add debug output to see what's being checked
        static int debugCount = 0;
        debugCount++;

        bool result = false;
        string reason = "No match";

        // Check for your specific flip-flop types: FSDNQ and FSDN
        if (cellType.find("FSDNQ") != string::npos) {
            result = true;
            reason = "Found FSDNQ";
        }
        else if (cellType.find("FSDN") != string::npos) {
            result = true;
            reason = "Found FSDN";
        }
        // Keep original checks as backup
        else if (cellType.find("FF") != string::npos) {
            result = true;
            reason = "Found FF";
        }
        else if (cellType.find("DFF") != string::npos) {
            result = true;
            reason = "Found DFF";
        }
        else if (cellType.find("SDFF") != string::npos) {
            result = true;
            reason = "Found SDFF";
        }
        else if (cellType.find("LATCH") != string::npos) {
            result = true;
            reason = "Found LATCH";
        }
        else if (cellType.find("_FF_") != string::npos) {
            result = true;
            reason = "Found _FF_";
        }
        else if (cellType.find("FLIP") != string::npos) {
            result = true;
            reason = "Found FLIP";
        }

        // Debug output for first 20 calls or all flip-flops found
        if (debugCount <= 20 || result) {
            cout << "DEBUG: isFlipFlopCell(\"" << cellType << "\") -> "
                << (result ? "TRUE" : "FALSE") << " (" << reason << ")" << endl;
        }

        return result;
    }

    int getBitWidth(const string& cellType) {
        // First try to extract number from end (your naming convention)
        regex endNumberPattern(R"(_(\d+)$)");
        smatch m;
        if (regex_search(cellType, m, endNumberPattern)) {
            int width = stoi(m[1]);
            cout << "DEBUG: getBitWidth(\"" << cellType << "\") -> " << width << " (from end)" << endl;
            return width;
        }

        // Original bit width detection patterns as backup
        regex bitRx(R"((\d+)BIT|(\d+)B|_(\d+)_)");
        if (regex_search(cellType, m, bitRx)) {
            for (int i = 1; i <= 3; i++) {
                if (m[i].matched) {
                    int width = stoi(m[i]);
                    cout << "DEBUG: getBitWidth(\"" << cellType << "\") -> " << width << " (pattern " << i << ")" << endl;
                    return width;
                }
            }
        }

        cout << "DEBUG: getBitWidth(\"" << cellType << "\") -> 1 (default)" << endl;
        return 1;
    }

    string getCellCategory(const string& cellType) {
        if (cellType.find("OR2") != string::npos) return "OR_Gate";
        if (cellType.find("AN2") != string::npos) return "AND_Gate";
        if (cellType.find("INV") != string::npos) return "Inverter";
        if (cellType.find("BUF") != string::npos) return "Buffer";
        if (cellType.find("FSDNQ") != string::npos) return "FSDNQ_FlipFlop";
        if (cellType.find("FSDN") != string::npos) return "FSDN_FlipFlop";
        return "Unknown";
    }

    string extractOrientation(const string& orientStr) {
        return orientStr;
    }

    bool validateCoordinate(int x, int y) {
        const int MAX_COORD = 1000000;
        const int MIN_COORD = -1000000;
        return (x >= MIN_COORD && x <= MAX_COORD &&
            y >= MIN_COORD && y <= MAX_COORD);
    }

    bool isClockSignal(const string& netName) {
        string upperNet = netName;
        transform(upperNet.begin(), upperNet.end(), upperNet.begin(), ::toupper);

        return (upperNet.find("CLK") != string::npos ||
            upperNet.find("CLOCK") != string::npos ||
            upperNet.find("CK") != string::npos);
    }

    bool isScanSignal(const string& pinName) {
        string upperPin = pinName;
        transform(upperPin.begin(), upperPin.end(), upperPin.begin(), ::toupper);

        return (upperPin == "SI" || upperPin == "SO" ||
            upperPin == "SCAN_IN" || upperPin == "SCAN_OUT" ||
            upperPin == "SE" || upperPin == "SCAN_EN");
    }
}

void DefParser::printDefData() const {
    cout << "\n=== DEF File Data ===" << endl;

    cout << "\nFound " << defData_.rows.size() << " ROW records:" << endl;
    for (const auto& r : defData_.rows) {
        cout << "  " << r.name << " @(" << r.x << "," << r.y << ") "
            << r.orientation << " DO " << r.count
            << " BY " << r.by << " STEP " << r.stepX << "," << r.stepY << endl;
    }

    cout << "\nFound " << defData_.tracks.size() << " TRACKS records:" << endl;
    for (const auto& t : defData_.tracks) {
        cout << "  Direction: " << t.direction << " Start: " << t.start
            << " Count: " << t.count << " Step: " << t.step
            << " Layer: " << t.layer << endl;
    }

    cout << "\nFound " << defData_.components.size() << " COMPONENT records:" << endl;
    for (const auto& c : defData_.components) {
        cout << "  " << c.name << " (" << c.cellType << ") "
            << "@(" << c.x << "," << c.y << ") "
            << "Orient: " << c.orient << endl;
    }
}

bool DefParser::validateData() const {
    // Basic validation
    bool isValid = true;

    if (defData_.components.empty()) {
        const_cast<DefParser*>(this)->addWarning("No components found in DEF file");
    }

    // Validate coordinates
    for (const auto& comp : defData_.components) {
        if (!DefUtils::validateCoordinate(comp.x, comp.y)) {
            const_cast<DefParser*>(this)->addWarning("Invalid coordinate for component: " + comp.name);
            isValid = false;
        }
    }

    return isValid;
}

bool DefParser::writeDefFile(const string& filename) const {
    ofstream defFile(filename);
    if (!defFile.is_open()) {
        return false;
    }

    try {
        defFile << "VERSION 5.8 ;" << endl;
        defFile << "DESIGN " << filename << " ;" << endl;
        defFile << "UNITS DISTANCE MICRONS " << defData_.units << " ;" << endl;
        defFile << "DIEAREA ( " << defData_.dieArea.xMin << " " << defData_.dieArea.yMin << " )"
            << " ( " << defData_.dieArea.xMax << " " << defData_.dieArea.yMax << " ) ;" << endl;


        // Write ROWS
        if (!defData_.rows.empty()) {
            for (const auto& row : defData_.rows) {
                defFile << "ROW " << row.name << " core " << row.x << " " << row.y
                    << " " << row.orientation << " DO " << row.count
                    << " BY " << row.by << " STEP " << row.stepX << " " << row.stepY << " ;" << endl;
            }
        }

        // Write TRACKS
        if (!defData_.tracks.empty()) {
            for (const auto& track : defData_.tracks) {
                defFile << "TRACKS " << track.direction << " " << track.start
                    << " DO " << track.count << " STEP " << track.step
                    << " LAYER " << track.layer << " ;" << endl;
            }
        }

        // Write COMPONENTS
        defFile << "COMPONENTS " << defData_.components.size() << " ;" << endl;
        for (const auto& comp : defData_.components) {
            defFile << "- " << comp.name << " " << comp.cellType
                << " + PLACED ( " << comp.x << " " << comp.y << " ) " << comp.orient << " ;" << endl;
        }
        defFile << "END COMPONENTS" << endl;

        defFile << "END DESIGN" << endl;
        defFile.close();
        return true;
    }
    catch (const exception& e) {
        defFile.close();
        return false;
    }
}

string DefParser::toString() const {
    ostringstream oss;
    oss << "DefParser Summary:" << endl;
    oss << "  Rows: " << defData_.rows.size() << endl;
    oss << "  Tracks: " << defData_.tracks.size() << endl;
    oss << "  Components: " << defData_.components.size() << endl;
    oss << "  Pins: " << defData_.pins.size() << endl;
    oss << "  Nets: " << defData_.nets.size() << endl;
    oss << "  Flip-Flops: " << defData_.flipFlops.size() << endl;
    oss << "  Scan Chains: " << defData_.scanChains.size() << endl;
    return oss.str();
}

void DefParser::addError(const string& error) {
    errors_.push_back(error);
    cerr << "DEF Error: " << error << endl;
}

void DefParser::addWarning(const string& warning) {
    warnings_.push_back(warning);
    cout << "DEF Warning: " << warning << endl;
}

void DefParser::computeRowDimensions() {
    if (defData_.rows.size() < 2) return;

    // Sort rows by y coordinate
    std::sort(defData_.rows.begin(), defData_.rows.end(),
        [](const RowInfo& a, const RowInfo& b) {
            return a.y < b.y;
        });

    // Compute row_y_width
    int rowYStep = defData_.rows[1].y - defData_.rows[0].y;

    // Compute row_x_width
    int rowXStep = defData_.rows[0].stepX * defData_.rows[0].count;

    std::cout << "[Info] Computed row_x_width = " << rowXStep
        << ", row_y_width = " << rowYStep << std::endl;

    // Write these values to each row
    for (auto& row : defData_.rows) {
        row.rowXWidth = row.stepX * row.count;
        row.rowYWidth = rowYStep;
    }
}

void DefParser::assignComponentsToRows() {
    std::ofstream fout("component_rowinfo.txt");
    std::map<std::string, int> rowCountMap;
    rowToComponentsMap_.clear();
    rowComponentCount_.clear();

    for (auto& comp : defData_.components) {
        int cx = comp.x;
        int cy = comp.y;
        bool matched = false;

        for (const auto& row : defData_.rows) {
            int rowBottom = row.y;
            int rowTop = row.y + row.rowYWidth;

            if (cy >= rowBottom && cy < rowTop) {
                comp.rowName = row.name;
                matched = true;

                // Update counts
                rowCountMap[row.name]++;
                rowToComponentsMap_[row.name].push_back(&comp);
                break;
            }
        }

        if (!matched) {
            comp.rowName = "UNPLACED";
            rowCountMap["UNPLACED"]++;
            rowToComponentsMap_["UNPLACED"].push_back(&comp);
        }

        fout << comp.name << " : " << comp.rowName << std::endl;
    }

    fout.close();

    // Write row:component count file
    std::ofstream countOut("row_component_count.txt");
    for (const auto& entry : rowCountMap) {
        const std::string& rowName = entry.first;
        int count = entry.second;
        countOut << rowName << " : " << count << std::endl;
        rowComponentCount_[rowName] = count;
    }
    countOut.close();
}