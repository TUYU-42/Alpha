// LibParser.cpp - Enhanced implementation with signal_type and bundle support

#include "LibParser.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>

using namespace std;

// 增強的 parsePin 方法，支援 signal_type
bool LibParser::parsePin(ifstream& file, const string& pinName, LibPin& pin) {
    string line;
    int braceDepth = 1;

    while (getline(file, line)) {
        // 追蹤大括號深度
        for (char c : line) {
            if (c == '{') braceDepth++;
            else if (c == '}') {
                braceDepth--;
                if (braceDepth == 0) {
                    return true;  // pin 解析完成
                }
            }
        }

        // 解析 pin 屬性
        if (line.find("direction :") != string::npos) {
            pin.direction = extractQuotedString(line);
            if (pin.direction.empty()) {
                // 嘗試不帶引號的格式
                size_t pos = line.find(":") + 1;
                istringstream iss(line.substr(pos));
                iss >> pin.direction;
                // 移除分號
                if (!pin.direction.empty() && pin.direction.back() == ';') {
                    pin.direction.pop_back();
                }
            }
        }
        else if (line.find("function :") != string::npos) {
            pin.function = extractQuotedString(line);
        }
        else if (line.find("capacitance :") != string::npos) {
            pin.capacitance = extractNumericValue(line);
        }
        else if (line.find("signal_type :") != string::npos) {
            // 新增：解析 signal_type
            pin.signalType = extractQuotedString(line);
            if (pin.signalType.empty()) {
                size_t pos = line.find(":") + 1;
                istringstream iss(line.substr(pos));
                iss >> pin.signalType;
                if (!pin.signalType.empty() && pin.signalType.back() == ';') {
                    pin.signalType.pop_back();
                }
            }
        }
        else if (line.find("max_capacitance :") != string::npos) {
            pin.maxCapacitance = extractNumericValue(line);
        }
        else if (line.find("min_capacitance :") != string::npos) {
            pin.minCapacitance = extractNumericValue(line);
        }
        else if (line.find("max_transition :") != string::npos) {
            pin.maxTransition = extractNumericValue(line);
        }
    }

    return false;
}

// 新增：parseBundle 方法
bool LibParser::parseBundle(ifstream& file, const string& bundleName, LibBundle& bundle) {
    string line;
    int braceDepth = 1;

    bundle.name = bundleName;

    while (getline(file, line)) {
        // 追蹤大括號深度
        for (char c : line) {
            if (c == '{') braceDepth++;
            else if (c == '}') {
                braceDepth--;
                if (braceDepth == 0) {
                    return true;  // bundle 解析完成
                }
            }
        }

        // Parse members
        if (line.find("members(") != string::npos || line.find("members (") != string::npos) {
            size_t start = line.find("(");
            size_t end = line.find(")");
            if (start != string::npos && end != string::npos && end > start) {
                string membersStr = line.substr(start + 1, end - start - 1);

                // 處理可能跨行的 members
                while (end == string::npos && getline(file, line)) {
                    membersStr += " " + line;
                    end = line.find(")");
                }

                // 解析 members
                stringstream ss(membersStr);
                string token;
                while (getline(ss, token, ',')) {
                    // 移除空白和引號
                    token.erase(remove_if(token.begin(), token.end(), ::isspace), token.end());
                    token.erase(remove(token.begin(), token.end(), '"'), token.end());
                    if (!token.empty()) {
                        bundle.members.push_back(token);
                    }
                }
            }
        }
        // Parse direction
        else if (line.find("direction :") != string::npos) {
            bundle.direction = extractQuotedString(line);
            if (bundle.direction.empty()) {
                size_t pos = line.find(":") + 1;
                istringstream iss(line.substr(pos));
                iss >> bundle.direction;
                if (!bundle.direction.empty() && bundle.direction.back() == ';') {
                    bundle.direction.pop_back();
                }
            }
        }
        // Parse signal_type
        else if (line.find("signal_type :") != string::npos) {
            bundle.signalType = extractQuotedString(line);
            if (bundle.signalType.empty()) {
                size_t pos = line.find(":") + 1;
                istringstream iss(line.substr(pos));
                iss >> bundle.signalType;
                if (!bundle.signalType.empty() && bundle.signalType.back() == ';') {
                    bundle.signalType.pop_back();
                }
            }
        }
    }

    return false;
}

// 增強的 parseCell 方法，支援 bundle
bool LibParser::parseCell(ifstream& file, const string& cellName, LibCell& cell, const string& libraryName) {
    string line;
    int braceDepth = 1;  // 已經在 cell 區塊內

    while (getline(file, line)) {
        // 追蹤大括號深度
        for (char c : line) {
            if (c == '{') braceDepth++;
            else if (c == '}') {
                braceDepth--;
                if (braceDepth == 0) {
                    updateCellBitWidth(cell);
                    updateCellBitWidthFromBundles(cell);  // 新增：從 bundles 更新 bitWidth
                    return true;  // cell 解析完成
                }
            }
        }

        // 解析 cell 屬性
        if (line.find("area :") != string::npos) {
            cell.area = extractNumericValue(line);
        }
        else if (line.find("cell_leakage_power :") != string::npos) {
            cell.cellLeakagePower = extractNumericValue(line);
        }
        else if (line.find("single_bit_degenerate :") != string::npos) {
            cell.singleBitDegenerate = extractQuotedString(line);
        }
        else if (line.find("ff (") != string::npos || line.find("ff(") != string::npos) {
            cell.ffType = "ff";
            cell.hasFF = true;
            parseFF(file, cell);
        }
        else if (line.find("pin(") != string::npos || line.find("pin (") != string::npos) {
            smatch match;
            regex pinRegex(R"(pin\s*\(\s*([^\s\)]+)\s*\))");

            if (regex_search(line, match, pinRegex)) {
                string pinName = match[1];
                LibPin pin;
                pin.name = pinName;

                if (parsePin(file, pinName, pin)) {
                    cell.pins[pinName] = pin;
                }
            }
        }
        else if (line.find("bundle(") != string::npos || line.find("bundle (") != string::npos) {
            // 新增：解析 bundle
            smatch match;
            regex bundleRegex(R"(bundle\s*\(\s*([^\s\)]+)\s*\))");

            if (regex_search(line, match, bundleRegex)) {
                string bundleName = match[1];
                LibBundle bundle;

                if (parseBundle(file, bundleName, bundle)) {
                    cell.bundles[bundleName] = bundle;

                    // 將 bundle members 也加入 pins（方便查詢）
                    for (const string& member : bundle.members) {
                        if (cell.pins.find(member) == cell.pins.end()) {
                            LibPin memberPin;
                            memberPin.name = member;
                            memberPin.direction = bundle.direction;
                            memberPin.signalType = bundle.signalType;
                            cell.pins[member] = memberPin;
                        }
                    }
                }
            }
        }
    }

    return false;  // 未預期的檔案結束
}

// 新增：從 bundles 更新 bitWidth
void LibParser::updateCellBitWidthFromBundles(LibCell& cell) const {
    // 檢查 D bundle 或 Q bundle 的大小
    if (cell.hasBundle("D")) {
        int dCount = cell.getBundleMembers("D").size();
        if (dCount > cell.bitWidth) {
            cell.bitWidth = dCount;
        }
    }

    if (cell.hasBundle("Q")) {
        int qCount = cell.getBundleMembers("Q").size();
        if (qCount > cell.bitWidth) {
            cell.bitWidth = qCount;
        }
    }
}

// 新增：取得 cell 的 scan pins
vector<string> LibParser::getCellScanPins(const string& cellName) const {
    vector<string> scanPins;

    auto it = cellLibrary_.find(cellName);
    if (it == cellLibrary_.end()) {
        return scanPins;
    }

    const LibCell& cell = it->second;

    // 檢查所有 pins
    for (const auto& pinPair : cell.pins) {
        const LibPin& pin = pinPair.second;

        // 檢查 signal_type
        if (pin.signalType.find("test_scan") != string::npos ||
            pin.signalType == "scan_enable" ||
            pin.signalType == "scan_in" ||
            pin.signalType == "scan_out") {
            scanPins.push_back(pin.name);
        }
        // 也檢查 pin 名稱
        else if (pin.name == "SI" || pin.name == "SO" ||
            pin.name == "SE" || pin.name == "SEN" ||
            pin.name.find("SCAN") != string::npos) {
            scanPins.push_back(pin.name);
        }
    }

    return scanPins;
}

// 新增：判斷 pin 是否為 scan pin
bool LibParser::isScanPin(const string& cellName, const string& pinName) const {
    auto it = cellLibrary_.find(cellName);
    if (it == cellLibrary_.end()) {
        return false;
    }

    const LibCell& cell = it->second;
    auto pinIt = cell.pins.find(pinName);
    if (pinIt == cell.pins.end()) {
        return false;
    }

    const LibPin& pin = pinIt->second;

    // 檢查 signal_type
    if (!pin.signalType.empty()) {
        if (pin.signalType.find("test_scan") != string::npos ||
            pin.signalType == "scan_enable" ||
            pin.signalType == "scan_in" ||
            pin.signalType == "scan_out") {
            return true;
        }
    }

    // 檢查 pin 名稱
    string upperName = pinName;
    transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);

    return (upperName == "SI" || upperName == "SO" ||
        upperName == "SE" || upperName == "SEN" ||
        upperName.find("SCAN") != string::npos);
}

// 增強的 printCellDetails，顯示 bundles
void LibParser::printCellDetails(const string& cellName) const {
    auto it = cellLibrary_.find(cellName);
    if (it == cellLibrary_.end()) {
        cout << "Cell " << cellName << " not found" << endl;
        return;
    }

    const LibCell& cell = it->second;

    cout << "\n=== Cell: " << cell.name << " ===" << endl;
    cout << "Library: " << cell.libraryName << endl;
    cout << "Area: " << cell.area << endl;
    cout << "Leakage Power: " << cell.cellLeakagePower << endl;
    cout << "Bit Width: " << cell.bitWidth << endl;

    if (!cell.singleBitDegenerate.empty()) {
        cout << "Single-bit Degenerate: " << cell.singleBitDegenerate << endl;
    }

    if (cell.hasFF) {
        cout << "Has FF block: Yes" << endl;
    }

    // 顯示 bundles
    if (!cell.bundles.empty()) {
        cout << "\nBundles (" << cell.bundles.size() << "):" << endl;
        for (const auto& bundlePair : cell.bundles) {
            const LibBundle& bundle = bundlePair.second;
            cout << "  Bundle " << bundle.name << ":" << endl;
            cout << "    Direction: " << bundle.direction << endl;
            if (!bundle.signalType.empty()) {
                cout << "    Signal Type: " << bundle.signalType << endl;
            }
            cout << "    Members: ";
            for (size_t i = 0; i < bundle.members.size(); ++i) {
                if (i > 0) cout << ", ";
                cout << bundle.members[i];
            }
            cout << endl;
        }
    }

    // 顯示 pins
    cout << "\nPins (" << cell.pins.size() << "):" << endl;
    for (const auto& pinPair : cell.pins) {
        const LibPin& pin = pinPair.second;
        cout << "  " << pin.name << " (" << pin.direction << ")";
        if (!pin.function.empty()) {
            cout << " function: " << pin.function;
        }
        if (!pin.signalType.empty()) {
            cout << " signal_type: " << pin.signalType;
        }
        cout << endl;
    }
}

// 增強的 isMultibitFF，使用 bundle 資訊
bool LibParser::isMultibitFF(const std::string& cellName, int bitWidth) const {
    auto it = cellLibrary_.find(cellName);
    if (it == cellLibrary_.end()) {
        return false;
    }

    const LibCell& cell = it->second;

    // 首先檢查 cell 的 bitWidth
    if (cell.bitWidth == bitWidth) {
        return true;
    }

    // 檢查 D bundle 或 Q bundle
    if (cell.hasBundle("D")) {
        if (cell.getBundleMembers("D").size() == bitWidth) {
            return true;
        }
    }

    if (cell.hasBundle("Q")) {
        if (cell.getBundleMembers("Q").size() == bitWidth) {
            return true;
        }
    }

    // 檢查名稱模式
    std::string bitStr = std::to_string(bitWidth);

    if (cellName.find(bitStr + "_") != std::string::npos ||
        cellName.find("_" + bitStr) != std::string::npos ||
        cellName.find(bitStr + "BIT") != std::string::npos ||
        cellName.find("MBFF" + bitStr) != std::string::npos) {
        return true;
    }

    return false;
}