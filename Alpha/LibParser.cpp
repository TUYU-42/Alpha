#include "LibParser.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>

using namespace std;

// 新增：解析所有 library 檔案，找出所有 FF cells
bool LibParser::parseAllLibraries(const vector<string>& libFiles) {
    cout << "\n=== LibParser: Parsing all libraries for FF cells ===" << endl;

    clear(); // 清空之前的資料

    for (const string& libFile : libFiles) {
        cout << "\nProcessing library file: " << libFile << endl;

        ifstream file(libFile);
        if (!file.is_open()) {
            cerr << "Warning: Cannot open lib file: " << libFile << endl;
            continue;
        }

        string line;
        string currentLibrary;
        bool inLibrary = false;

        while (getline(file, line)) {
            // 移除註解
            size_t commentPos = line.find("//");
            if (commentPos != string::npos) {
                line = line.substr(0, commentPos);
            }

            // 檢查 library 開始
            if (line.find("library") != string::npos && line.find("(") != string::npos) {
                currentLibrary = extractLibraryName(line);
                if (!currentLibrary.empty()) {
                    libraries_[currentLibrary].name = currentLibrary;
                    libraries_[currentLibrary].filename = libFile;
                    inLibrary = true;
                    cout << "  Found library: " << currentLibrary << endl;
                }
            }

            // 檢查 cell 定義
            smatch match;
            regex cellRegex(R"(cell\s*\(\s*([^\s\)]+)\s*\))");

            if (regex_search(line, match, cellRegex)) {
                string cellName = match[1];

                // 解析這個 cell
                LibCell cell;
                cell.name = cellName;
                cell.libraryName = currentLibrary;

                // 快速掃描判斷是否為 FF cell
                if (parseCellForFF(file, cellName, cell, currentLibrary)) {
                    // 只有當 cell 有 single_bit_degenerate 或 ff() 時才加入
                    if (!cell.singleBitDegenerate.empty() || cell.hasFF) {
                        cellLibrary_[cellName] = cell;
                        parsedCells_.insert(cellName);

                        if (inLibrary && !currentLibrary.empty()) {
                            libraries_[currentLibrary].cells.insert(cellName);
                        }

                        cout << "    Found FF cell: " << cellName;
                        if (!cell.singleBitDegenerate.empty()) {
                            cout << " (degenerate: " << cell.singleBitDegenerate << ")";
                        }
                        if (cell.hasFF) {
                            cout << " (has ff block)";
                        }
                        cout << endl;
                    }
                }
            }
        }

        file.close();
    }

    cout << "\n✓ Parsed " << cellLibrary_.size() << " FF cells from "
        << libraries_.size() << " libraries" << endl;

    isLoaded_ = !cellLibrary_.empty();
    return isLoaded_;
}





// 快速解析 cell，專注於找 FF 相關特徵
bool LibParser::parseCellForFF(ifstream& file, const string& cellName, LibCell& cell, const string& libraryName) {
    string line;
    int braceDepth = 1;
    bool foundFF = false;
    bool foundDegenerate = false;

    streampos startPos = file.tellg();

    while (getline(file, line) && braceDepth > 0) {
        // 追蹤大括號深度
        for (char c : line) {
            if (c == '{') braceDepth++;
            else if (c == '}') {
                braceDepth--;
                if (braceDepth == 0) break;
            }
        }

        // 快速檢查關鍵屬性
        if (line.find("area :") != string::npos) {
            cell.area = extractNumericValue(line);
        }
        else if (line.find("cell_leakage_power :") != string::npos) {
            cell.cellLeakagePower = extractNumericValue(line);
        }
        else if (line.find("single_bit_degenerate :") != string::npos) {
            cell.singleBitDegenerate = extractQuotedString(line);
            foundDegenerate = true;
        }
        else if (line.find("ff(") != string::npos || line.find("ff (") != string::npos) {
            cell.hasFF = true;
            foundFF = true;

            // 簡單解析 ff block 以取得基本資訊
            size_t ffStart = line.find("ff");
            size_t parenStart = line.find("(", ffStart);
            size_t parenEnd = line.find(")", parenStart);

            if (parenStart != string::npos && parenEnd != string::npos) {
                string ffPins = line.substr(parenStart + 1, parenEnd - parenStart - 1);
                // 可以進一步解析 ff pins，但現在先標記有 ff 即可
            }
        }

        // 如果已經找到兩個條件之一，可以提早結束
        if (foundFF || foundDegenerate) {
            // 繼續讀到 cell 結束
            while (braceDepth > 0 && getline(file, line)) {
                for (char c : line) {
                    if (c == '{') braceDepth++;
                    else if (c == '}') {
                        braceDepth--;
                        if (braceDepth == 0) break;
                    }
                }
            }
            return true;
        }
    }

    return foundFF || foundDegenerate;
}

// 取得所有 FF cell 列表
set<string> LibParser::getFFCellList() const {
    set<string> ffCells;

    for (const auto& pair : cellLibrary_) {
        const LibCell& cell = pair.second;
        // 包含有 single_bit_degenerate 或 ff() 的 cells
        if (!cell.singleBitDegenerate.empty() || cell.hasFF) {
            ffCells.insert(cell.name);
        }
    }

    return ffCells;
}

// 從 library(...) 行提取 library 名稱
string LibParser::extractLibraryName(const string& line) {
    // 尋找 library( 或 library (
    size_t start = line.find("library");
    if (start == string::npos) return "";

    start = line.find("(", start);
    if (start == string::npos) return "";

    size_t end = line.find(")", start);
    if (end == string::npos) return "";

    string name = line.substr(start + 1, end - start - 1);

    // 移除空白
    name.erase(0, name.find_first_not_of(" \t"));
    name.erase(name.find_last_not_of(" \t") + 1);

    return name;
}

// 保留原有的 parseWithCellList 方法（競賽用）
bool LibParser::parseWithCellList(const vector<string>& libFiles,
    const vector<string>& initialCellList,
    set<string>& finalCellList) {
    cout << "\n=== LibParser: Contest-Specific Parsing ===" << endl;

    // Step 1: 檢查每個初始元件的 single_bit_degenerate
    cout << "Step 1: Checking single_bit_degenerate attributes..." << endl;

    map<string, string> cellToDegenerateMap;

    for (const string& cellName : initialCellList) {
        bool foundCell = false;
        string degenerateCell;

        // 在所有 lib 檔案中搜尋
        for (const string& libFile : libFiles) {
            ifstream file(libFile);
            if (!file.is_open()) {
                cerr << "Warning: Cannot open lib file: " << libFile << endl;
                continue;
            }

            string line;
            bool inTargetCell = false;
            int braceDepth = 0;

            while (getline(file, line)) {
                // 檢查是否進入目標 cell
                if (line.find("cell(" + cellName + ")") != string::npos ||
                    line.find("cell (" + cellName + ")") != string::npos ||
                    line.find("cell(" + cellName + " )") != string::npos) {
                    inTargetCell = true;
                    foundCell = true;
                    braceDepth = 0;
                }

                if (inTargetCell) {
                    // 追蹤大括號深度
                    for (char c : line) {
                        if (c == '{') braceDepth++;
                        else if (c == '}') {
                            braceDepth--;
                            if (braceDepth == 0) {
                                inTargetCell = false;
                                break;
                            }
                        }
                    }

                    // 尋找 single_bit_degenerate
                    if (line.find("single_bit_degenerate") != string::npos) {
                        degenerateCell = extractQuotedString(line);
                        if (!degenerateCell.empty()) {
                            cellToDegenerateMap[cellName] = degenerateCell;
                            break;
                        }
                    }
                }
            }

            file.close();
            if (!degenerateCell.empty()) break;
        }

        // 加入適當的元件到最終列表
        if (!degenerateCell.empty()) {
            finalCellList.insert(degenerateCell);
            cout << "  " << cellName << " → " << degenerateCell << endl;
        }
        else {
            finalCellList.insert(cellName);
            if (!foundCell) {
                cout << "  " << cellName << " (not found in libs)" << endl;
            }
            else {
                cout << "  " << cellName << " (no degenerate)" << endl;
            }
        }
    }

    cout << "\nFinal cell list contains " << finalCellList.size() << " cells" << endl;

    // Step 2: 解析最終列表中元件的完整資訊
    cout << "\nStep 2: Parsing complete cell information..." << endl;

    int parsedCount = 0;
    for (const string& libFile : libFiles) {
        ifstream file(libFile);
        if (!file.is_open()) {
            cerr << "Warning: Cannot open lib file: " << libFile << endl;
            continue;
        }

        cout << "  Processing: " << libFile << endl;

        string line;
        string currentLibrary;

        while (getline(file, line)) {
            // 檢查 library
            if (line.find("library") != string::npos && line.find("(") != string::npos) {
                currentLibrary = extractLibraryName(line);
            }

            // 尋找 cell 定義
            smatch match;
            regex cellRegex(R"(cell\s*\(\s*([^\s\)]+)\s*\))");

            if (regex_search(line, match, cellRegex)) {
                string cellName = match[1];

                // 只解析最終列表中的元件
                if (finalCellList.find(cellName) != finalCellList.end() &&
                    parsedCells_.find(cellName) == parsedCells_.end()) {

                    LibCell cell;
                    cell.name = cellName;
                    cell.libraryName = currentLibrary;

                    if (parseCell(file, cellName, cell, currentLibrary)) {
                        cellLibrary_[cellName] = cell;
                        parsedCells_.insert(cellName);
                        parsedCount++;

                        if (parsedCount % 10 == 0) {
                            cout << "    Parsed " << parsedCount << " cells..." << endl;
                        }
                    }
                }
            }
        }

        file.close();
    }

    cout << "✓ Parsed " << cellLibrary_.size() << " cells from .lib files" << endl;

    // 顯示統計資訊
    int ffCount = 0;
    int mbCount = 0;
    double totalArea = 0.0;

    for (const auto& pair : cellLibrary_) {
        const LibCell& cell = pair.second;

        if (cell.name.find("FF") != string::npos ||
            cell.name.find("FSD") != string::npos) {
            ffCount++;
        }

        if (cell.name.find("2_") != string::npos ||
            cell.name.find("4_") != string::npos ||
            cell.name.find("8_") != string::npos) {
            mbCount++;
        }

        totalArea += cell.area;
    }

    cout << "\nCell statistics:" << endl;
    cout << "  Flip-flop cells: " << ffCount << endl;
    cout << "  Multi-bit cells: " << mbCount << endl;
    cout << "  Average area: " << (cellLibrary_.empty() ? 0.0 : totalArea / cellLibrary_.size()) << endl;

    isLoaded_ = true;
    return true;
}

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
        else if (line.find("ff (") != string::npos ||
            line.find("ff(") != string::npos) {
            cell.ffType = "ff";
            cell.hasFF = true;
            parseFF(file, cell);
        }
        else if (line.find("pin(") != string::npos ||
            line.find("pin (") != string::npos) {
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
    }

    return false;  // 未預期的檔案結束
}

bool LibParser::parseFF(ifstream& file, LibCell& cell) {
    string line;
    int braceDepth = 1;

    while (getline(file, line)) {
        // 追蹤大括號深度
        for (char c : line) {
            if (c == '{') braceDepth++;
            else if (c == '}') {
                braceDepth--;
                if (braceDepth == 0) {
                    return true;  // ff block 解析完成
                }
            }
        }

        // 可以在這裡解析 ff block 的詳細內容
        // 例如 clocked_on, next_state 等
    }

    return false;
}

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
    }

    return false;
}

string LibParser::extractQuotedString(const string& line) {
    size_t start = line.find('"');
    if (start == string::npos) return "";

    size_t end = line.find('"', start + 1);
    if (end == string::npos) return "";

    return line.substr(start + 1, end - start - 1);
}

double LibParser::extractNumericValue(const string& line) {
    size_t pos = line.find(':');
    if (pos == string::npos) return 0.0;

    string valueStr = line.substr(pos + 1);

    // 移除分號
    size_t semicolon = valueStr.find(';');
    if (semicolon != string::npos) {
        valueStr = valueStr.substr(0, semicolon);
    }

    // 移除空白
    valueStr.erase(0, valueStr.find_first_not_of(" \t"));
    valueStr.erase(valueStr.find_last_not_of(" \t") + 1);

    try {
        return stod(valueStr);
    }
    catch (...) {
        return 0.0;
    }
}

void LibParser::skipToEndOfBlock(ifstream& file, int depth) {
    string line;

    while (getline(file, line) && depth > 0) {
        for (char c : line) {
            if (c == '{') depth++;
            else if (c == '}') {
                depth--;
                if (depth == 0) return;
            }
        }
    }
}

const LibCell* LibParser::getCell(const string& cellName) const {
    auto it = cellLibrary_.find(cellName);
    return (it != cellLibrary_.end()) ? &it->second : nullptr;
}

bool LibParser::hasCell(const string& cellName) const {
    return cellLibrary_.find(cellName) != cellLibrary_.end();
}

string LibParser::getSingleBitDegenerate(const string& cellName) const {
    auto it = cellLibrary_.find(cellName);
    if (it != cellLibrary_.end()) {
        return it->second.singleBitDegenerate;
    }
    return "";
}


vector<string> LibParser::getFlipFlopCells() const {
    vector<string> ffCells;

    for (const auto& pair : cellLibrary_) {
        const LibCell& cell = pair.second;

        // 檢查是否為 flip-flop
        if (cell.name.find("FF") != string::npos ||
            cell.name.find("DFF") != string::npos ||
            cell.name.find("SDFF") != string::npos ||
            cell.name.find("FSD") != string::npos ||
            cell.hasFF ||
            !cell.ffType.empty()) {
            ffCells.push_back(cell.name);
        }
    }

    return ffCells;
}
// 在 LibParser.cpp 中添加以下兩個方法的實現

std::string LibParser::getmultibitff2(const std::string& cellName) const {
    // 首先檢查該 cell 是否存在
    auto it = cellLibrary_.find(cellName);
    if (it == cellLibrary_.end()) {
        return "";
    }

    const LibCell& cell = it->second;

    // 方法1: 如果當前 cell 是單位元 FF，查找對應的 2-bit 多位元版本
    if (cell.singleBitDegenerate.empty()) {
        // 嘗試根據命名規則找到 2-bit 版本
        std::string multibitName = findMultibitVariant(cellName, 2);
        if (!multibitName.empty() && hasCell(multibitName)) {
            return multibitName;
        }
    }

    // 方法2: 如果當前 cell 是多位元 FF，查找其對應的 2-bit 版本
    else {
        // 檢查當前 cell 是否本身就是 2-bit FF
        if (isMultibitFF(cellName, 2)) {
            return cellName;
        }

        // 如果是其他位數的多位元 FF，查找對應的 2-bit 版本
        std::string baseType = extractBaseFFType(cellName);
        if (!baseType.empty()) {
            std::string multibit2Name = generateMultibitName(baseType, 2);
            if (!multibit2Name.empty() && hasCell(multibit2Name)) {
                return multibit2Name;
            }
        }
    }

    // 方法3: 遍歷所有 cell 查找合適的 2-bit 多位元 FF
    for (const auto& pair : cellLibrary_) {
        const LibCell& candidateCell = pair.second;

        // 檢查是否為 2-bit FF 且與輸入 cell 兼容
        if (isMultibitFF(candidateCell.name, 2) &&
            isCompatibleFF(cellName, candidateCell.name)) {
            return candidateCell.name;
        }

        // 檢查 single_bit_degenerate 關係
        if (!candidateCell.singleBitDegenerate.empty() &&
            candidateCell.singleBitDegenerate == cellName &&
            isMultibitFF(candidateCell.name, 2)) {
            return candidateCell.name;
        }
    }

    return "";
}

std::string LibParser::getmultibitff4(const std::string& cellName) const {
    // 首先檢查該 cell 是否存在
    auto it = cellLibrary_.find(cellName);
    if (it == cellLibrary_.end()) {
        return "";
    }

    const LibCell& cell = it->second;

    // 方法1: 如果當前 cell 是單位元 FF，查找對應的 4-bit 多位元版本
    if (cell.singleBitDegenerate.empty()) {
        // 嘗試根據命名規則找到 4-bit 版本
        std::string multibitName = findMultibitVariant(cellName, 4);
        if (!multibitName.empty() && hasCell(multibitName)) {
            return multibitName;
        }
    }

    // 方法2: 如果當前 cell 是多位元 FF，查找其對應的 4-bit 版本
    else {
        // 檢查當前 cell 是否本身就是 4-bit FF
        if (isMultibitFF(cellName, 4)) {
            return cellName;
        }

        // 如果是其他位數的多位元 FF，查找對應的 4-bit 版本
        std::string baseType = extractBaseFFType(cellName);
        if (!baseType.empty()) {
            std::string multibit4Name = generateMultibitName(baseType, 4);
            if (!multibit4Name.empty() && hasCell(multibit4Name)) {
                return multibit4Name;
            }
        }
    }

    // 方法3: 遍歷所有 cell 查找合適的 4-bit 多位元 FF
    for (const auto& pair : cellLibrary_) {
        const LibCell& candidateCell = pair.second;

        // 檢查是否為 4-bit FF 且與輸入 cell 兼容
        if (isMultibitFF(candidateCell.name, 4) &&
            isCompatibleFF(cellName, candidateCell.name)) {
            return candidateCell.name;
        }

        // 檢查 single_bit_degenerate 關係
        if (!candidateCell.singleBitDegenerate.empty() &&
            candidateCell.singleBitDegenerate == cellName &&
            isMultibitFF(candidateCell.name, 4)) {
            return candidateCell.name;
        }
    }

    return "";
}

// 輔助方法實現
std::string LibParser::findMultibitVariant(const std::string& cellName, int bitWidth) const {
    // 根據命名規則生成多位元版本名稱
    // 例如：SNPSHOPT25_FSDN_V2_1 -> SNPSHOPT25_FSDN2_V2_1 (2-bit)
    //      SNPSHOPT25_FSDN_V2_1 -> SNPSHOPT25_FSDN4_V2_1 (4-bit)

    std::string result = cellName;

    // 查找可能的位置插入位數標識
    size_t pos = result.find("_V2");
    if (pos != std::string::npos) {
        // 在 _V2 前插入位數
        result.insert(pos, std::to_string(bitWidth));
        return result;
    }

    // 另一種命名模式：在最後添加位數
    pos = result.find_last_of("_");
    if (pos != std::string::npos) {
        std::string base = result.substr(0, pos);
        std::string suffix = result.substr(pos);
        result = base + std::to_string(bitWidth) + suffix;
        return result;
    }

    // 直接在名稱後添加位數標識
    result += std::to_string(bitWidth);
    return result;
}

bool LibParser::isMultibitFF(const std::string& cellName, int bitWidth) const {
    // 檢查 cell 名稱是否表示指定位數的多位元 FF
    std::string bitStr = std::to_string(bitWidth);

    // 檢查各種可能的命名模式
    if (cellName.find(bitStr + "_") != std::string::npos ||
        cellName.find("_" + bitStr) != std::string::npos ||
        cellName.find(bitStr + "BIT") != std::string::npos ||
        cellName.find("MBFF" + bitStr) != std::string::npos) {
        return true;
    }

    // 檢查是否為多位元 FF（通過 pin 數量判斷）
    auto it = cellLibrary_.find(cellName);
    if (it != cellLibrary_.end()) {
        const LibCell& cell = it->second;

        // 計算 D 和 Q pin 的數量
        int dPinCount = 0, qPinCount = 0;
        for (const auto& pinPair : cell.pins) {
            const std::string& pinName = pinPair.first;
            if (pinName.find("D") == 0 && pinName.length() > 1) dPinCount++;
            if (pinName.find("Q") == 0 && pinName.length() > 1) qPinCount++;
        }

        return (dPinCount == bitWidth && qPinCount == bitWidth);
    }

    return false;
}

bool LibParser::isCompatibleFF(const std::string& cell1, const std::string& cell2) const {
    // 檢查兩個 FF 是否兼容（同一個 library family）

    // 提取 library family 前綴
    auto extractLibFamily = [](const std::string& name) -> std::string {
        size_t pos = name.find("_");
        if (pos != std::string::npos) {
            return name.substr(0, pos);
        }
        return name;
        };

    std::string family1 = extractLibFamily(cell1);
    std::string family2 = extractLibFamily(cell2);

    // 同一個 family 視為兼容
    if (family1 == family2) {
        return true;
    }

    // 檢查 single_bit_degenerate 關係
    auto it1 = cellLibrary_.find(cell1);
    auto it2 = cellLibrary_.find(cell2);

    if (it1 != cellLibrary_.end() && it2 != cellLibrary_.end()) {
        const LibCell& libCell1 = it1->second;
        const LibCell& libCell2 = it2->second;

        // 檢查雙向兼容性
        if ((!libCell1.singleBitDegenerate.empty() && libCell1.singleBitDegenerate == cell2) ||
            (!libCell2.singleBitDegenerate.empty() && libCell2.singleBitDegenerate == cell1)) {
            return true;
        }
    }

    return false;
}

std::string LibParser::extractBaseFFType(const std::string& cellName) const {
    // 從多位元 FF 名稱中提取基礎類型
    // 例如：SNPSHOPT25_FSDN4_V2_1 -> SNPSHOPT25_FSDN_V2_1

    std::string result = cellName;

    // 移除數字標識
    std::regex numberRegex(R"(\d+)");
    result = std::regex_replace(result, numberRegex, "");

    // 清理多餘的下劃線
    std::regex underscoreRegex(R"(__+)");
    result = std::regex_replace(result, underscoreRegex, "_");

    return result;
}

std::string LibParser::generateMultibitName(const std::string& baseType, int bitWidth) const {
    // 從基礎類型生成指定位數的多位元 FF 名稱
    std::string result = baseType;
    std::string bitStr = std::to_string(bitWidth);

    // 在適當位置插入位數標識
    size_t pos = result.find("_V2");
    if (pos != std::string::npos) {
        result.insert(pos, bitStr);
        return result;
    }

    // 在最後添加位數標識
    pos = result.find_last_of("_");
    if (pos != std::string::npos) {
        result.insert(pos, bitStr);
        return result;
    }

    result += bitStr;
    return result;
}
vector<string> LibParser::getMultiBitCells() const {
    vector<string> mbCells;

    for (const auto& pair : cellLibrary_) {
        const LibCell& cell = pair.second;

        // 檢查多位元模式
        if (cell.name.find("2_") != string::npos ||
            cell.name.find("4_") != string::npos ||
            cell.name.find("8_") != string::npos ||
            cell.name.find("16_") != string::npos) {
            mbCells.push_back(cell.name);
        }
    }

    return mbCells;
}

void LibParser::clear() {
    cellLibrary_.clear();
    parsedCells_.clear();
    libraries_.clear();
    isLoaded_ = false;
}

void LibParser::printSummary() const {
    cout << "\n=== LibParser Summary ===" << endl;
    cout << "Total cells parsed: " << cellLibrary_.size() << endl;
    cout << "Total libraries: " << libraries_.size() << endl;

    // 統計
    int ffCount = 0;
    int mbCount = 0;
    int withDegenerate = 0;
    int withFFBlock = 0;

    for (const auto& pair : cellLibrary_) {
        const LibCell& cell = pair.second;

        if (cell.name.find("FF") != string::npos ||
            cell.name.find("FSD") != string::npos) {
            ffCount++;
        }

        if (cell.name.find("2_") != string::npos ||
            cell.name.find("4_") != string::npos) {
            mbCount++;
        }

        if (!cell.singleBitDegenerate.empty()) {
            withDegenerate++;
        }

        if (cell.hasFF) {
            withFFBlock++;
        }
    }

    cout << "Flip-flop cells: " << ffCount << endl;
    cout << "Multi-bit cells: " << mbCount << endl;
    cout << "Cells with single_bit_degenerate: " << withDegenerate << endl;
    cout << "Cells with ff() block: " << withFFBlock << endl;

    // 顯示每個 library 的統計
    cout << "\nLibrary breakdown:" << endl;
    for (const auto& libPair : libraries_) {
        cout << "  " << libPair.first << ": " << libPair.second.cells.size() << " cells" << endl;
    }
}

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

    if (!cell.singleBitDegenerate.empty()) {
        cout << "Single-bit Degenerate: " << cell.singleBitDegenerate << endl;
    }

    if (cell.hasFF) {
        cout << "Has FF block: Yes" << endl;
    }

    cout << "Pins (" << cell.pins.size() << "):" << endl;
    for (const auto& pinPair : cell.pins) {
        const LibPin& pin = pinPair.second;
        cout << "  " << pin.name << " (" << pin.direction << ")";
        if (!pin.function.empty()) {
            cout << " function: " << pin.function;
        }
        cout << endl;
    }
}

void LibParser::printFFCellList() const {
    cout << "\n=== FF Cell List ===" << endl;
    cout << "Total FF cells: " << cellLibrary_.size() << endl;

    // 按 library 分組顯示
    map<string, vector<string>> cellsByLibrary;

    for (const auto& pair : cellLibrary_) {
        const LibCell& cell = pair.second;
        cellsByLibrary[cell.libraryName].push_back(cell.name);
    }

    for (const auto& libPair : cellsByLibrary) {
        cout << "\nLibrary: " << libPair.first << endl;
        cout << "  Cells (" << libPair.second.size() << "):" << endl;

        int count = 0;
        for (const string& cellName : libPair.second) {
            const LibCell* cell = getCell(cellName);
            if (cell) {
                cout << "    " << cellName;
                if (!cell->singleBitDegenerate.empty()) {
                    cout << " [degenerate: " << cell->singleBitDegenerate << "]";
                }
                if (cell->hasFF) {
                    cout << " [has ff]";
                }
                cout << endl;

                if (++count >= 10 && libPair.second.size() > 10) {
                    cout << "    ... and " << (libPair.second.size() - 10) << " more" << endl;
                    break;
                }
            }
        }
    }
}