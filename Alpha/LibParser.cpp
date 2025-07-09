#include "LibParser.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>

using namespace std;

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
        while (getline(file, line)) {
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

                    if (parseCell(file, cellName, cell)) {
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

bool LibParser::parseCell(ifstream& file, const string& cellName, LibCell& cell) {
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
            !cell.ffType.empty()) {
            ffCells.push_back(cell.name);
        }
    }

    return ffCells;
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
    isLoaded_ = false;
}

void LibParser::printSummary() const {
    cout << "\n=== LibParser Summary ===" << endl;
    cout << "Total cells parsed: " << cellLibrary_.size() << endl;

    // 統計
    int ffCount = 0;
    int mbCount = 0;
    int withDegenerate = 0;

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
    }

    cout << "Flip-flop cells: " << ffCount << endl;
    cout << "Multi-bit cells: " << mbCount << endl;
    cout << "Cells with single_bit_degenerate: " << withDegenerate << endl;
}

void LibParser::printCellDetails(const string& cellName) const {
    auto it = cellLibrary_.find(cellName);
    if (it == cellLibrary_.end()) {
        cout << "Cell " << cellName << " not found" << endl;
        return;
    }

    const LibCell& cell = it->second;

    cout << "\n=== Cell: " << cell.name << " ===" << endl;
    cout << "Area: " << cell.area << endl;
    cout << "Leakage Power: " << cell.cellLeakagePower << endl;

    if (!cell.singleBitDegenerate.empty()) {
        cout << "Single-bit Degenerate: " << cell.singleBitDegenerate << endl;
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