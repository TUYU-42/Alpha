#include "Legalizer.h"
#include "ParserDEF.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <limits>

using namespace std;
using namespace DefUtils;

// Constructor
Legalizer::Legalizer(const DefData& defData,
    const std::unordered_map<std::string, LefMacroInfo>& macroMap,
    const std::vector<LefSiteInfo>& lefSites,
    const LibParser* libParser)
    : defData_(defData), macroMap_(macroMap), lefSites_(lefSites),
    libParser_(libParser), siteWidth_(0), rowHeight_(0) {

    // Initialize orientation mapping
    initializeOrientationMap();

    // Get DEF units
    defUnits_ = (defData_.units > 0) ? defData_.units : 1000;
    cout << "DEF units: " << defUnits_ << endl;

    // 新增：從 LibParser 取得所有 FF cell types
    if (libParser_) {
        flipFlopCellTypes_ = libParser_->getFFCellList();
        cout << "Loaded " << flipFlopCellTypes_.size() << " FF cell types from library" << endl;
    }

    // Get site dimensions from LEF
    if (!defData_.rows.empty() && !defData_.rows[0].siteName.empty()) {
        string siteName = defData_.rows[0].siteName;
        for (const auto& site : lefSites_) {
            if (site.name == siteName) {
                siteWidth_ = site.width * defUnits_;
                rowHeight_ = site.height * defUnits_;
                cout << "Site '" << siteName << "': width=" << siteWidth_
                    << ", height=" << rowHeight_ << " DEF units" << endl;
                break;
            }
        }
    }

    // Fallback to row data
    if (siteWidth_ == 0 && !defData_.rows.empty()) {
        siteWidth_ = defData_.rows[0].stepX;
        if (defData_.rows.size() > 1) {
            rowHeight_ = abs(defData_.rows[1].y - defData_.rows[0].y);
        }
        cout << "Using row data: siteWidth=" << siteWidth_
            << ", rowHeight=" << rowHeight_ << endl;
    }
}
// Initialize orientation mapping based on the provided table
void Legalizer::initializeOrientationMap() {
    // 8x8 orientation table: row orient x cell orient = final orient
    std::vector<std::string> orients = { "N", "S", "E", "W", "FN", "FS", "FE", "FW" };

    // Based on LEF/DEF/OpenAccess spec
    // N row
    orientMap_["N"]["N"] = "N";
    orientMap_["N"]["S"] = "S";
    orientMap_["N"]["E"] = "E";
    orientMap_["N"]["W"] = "W";
    orientMap_["N"]["FN"] = "FN";
    orientMap_["N"]["FS"] = "FS";
    orientMap_["N"]["FE"] = "FE";
    orientMap_["N"]["FW"] = "FW";

    // S row (R180)
    orientMap_["S"]["N"] = "S";
    orientMap_["S"]["S"] = "N";
    orientMap_["S"]["E"] = "W";
    orientMap_["S"]["W"] = "E";
    orientMap_["S"]["FN"] = "FS";
    orientMap_["S"]["FS"] = "FN";
    orientMap_["S"]["FE"] = "FW";
    orientMap_["S"]["FW"] = "FE";

    // E row (R270)
    orientMap_["E"]["N"] = "E";
    orientMap_["E"]["S"] = "W";
    orientMap_["E"]["E"] = "S";
    orientMap_["E"]["W"] = "N";
    orientMap_["E"]["FN"] = "FE";
    orientMap_["E"]["FS"] = "FW";
    orientMap_["E"]["FE"] = "FN";
    orientMap_["E"]["FW"] = "FS";

    // W row (R90)
    orientMap_["W"]["N"] = "W";
    orientMap_["W"]["S"] = "E";
    orientMap_["W"]["E"] = "N";
    orientMap_["W"]["W"] = "S";
    orientMap_["W"]["FN"] = "FW";
    orientMap_["W"]["FS"] = "FE";
    orientMap_["W"]["FE"] = "FS";
    orientMap_["W"]["FW"] = "FN";

    // FN row (MY)
    orientMap_["FN"]["N"] = "FN";
    orientMap_["FN"]["S"] = "FS";
    orientMap_["FN"]["E"] = "FE";
    orientMap_["FN"]["W"] = "FW";
    orientMap_["FN"]["FN"] = "N";
    orientMap_["FN"]["FS"] = "S";
    orientMap_["FN"]["FE"] = "E";
    orientMap_["FN"]["FW"] = "W";

    // FS row (MX)
    orientMap_["FS"]["N"] = "FS";
    orientMap_["FS"]["S"] = "FN";
    orientMap_["FS"]["E"] = "FW";
    orientMap_["FS"]["W"] = "FE";
    orientMap_["FS"]["FN"] = "S";
    orientMap_["FS"]["FS"] = "N";
    orientMap_["FS"]["FE"] = "W";
    orientMap_["FS"]["FW"] = "E";

    // FE row (MY90)
    orientMap_["FE"]["N"] = "FE";
    orientMap_["FE"]["S"] = "FW";
    orientMap_["FE"]["E"] = "FN";
    orientMap_["FE"]["W"] = "FS";
    orientMap_["FE"]["FN"] = "E";
    orientMap_["FE"]["FS"] = "W";
    orientMap_["FE"]["FE"] = "N";
    orientMap_["FE"]["FW"] = "S";

    // FW row (MX90)
    orientMap_["FW"]["N"] = "FW";
    orientMap_["FW"]["S"] = "FE";
    orientMap_["FW"]["E"] = "FS";
    orientMap_["FW"]["W"] = "FN";
    orientMap_["FW"]["FN"] = "W";
    orientMap_["FW"]["FS"] = "E";
    orientMap_["FW"]["FE"] = "S";
    orientMap_["FW"]["FW"] = "N";
}

// Helper function to check if a cell is a flip-flop
bool Legalizer::isFlipFlopCell(const std::string& cellType) const {
    // 方法1：如果有從 LibParser 載入的 FF cell types，直接查詢
    if (!flipFlopCellTypes_.empty()) {
        return flipFlopCellTypes_.find(cellType) != flipFlopCellTypes_.end();
    }

    // 方法2：如果有 LibParser 實例，動態查詢
    if (libParser_ && libParser_->isLoaded()) {
        const LibCell* cell = libParser_->getCell(cellType);
        if (cell) {
            // 檢查是否有 single_bit_degenerate 或 ff block
            if (!cell->singleBitDegenerate.empty() || cell->hasFF) {
                return true;
            }

            // 檢查是否有 D, Q, CLK pins
            bool hasD = false, hasQ = false, hasCLK = false;
            for (const auto& pinPair : cell->pins) {
                const std::string& pinName = pinPair.first;
                std::string upperPin = pinName;
                std::transform(upperPin.begin(), upperPin.end(), upperPin.begin(), ::toupper);

                if (upperPin == "D" || upperPin.find("D[") == 0 ||
                    upperPin.find("D0") != std::string::npos) {
                    hasD = true;
                }
                if (upperPin == "Q" || upperPin.find("Q[") == 0 ||
                    upperPin.find("Q0") != std::string::npos) {
                    hasQ = true;
                }
                if (upperPin == "CLK" || upperPin == "CK" ||
                    upperPin.find("CLOCK") != std::string::npos) {
                    hasCLK = true;
                }
            }

            if (hasD && hasQ && hasCLK) {
                return true;
            }
        }
    }

    // 方法3：回退到原始的關鍵字檢查（保留作為備用）
    std::string upperType = cellType;
    std::transform(upperType.begin(), upperType.end(), upperType.begin(), ::toupper);

    return (upperType.find("FF") != std::string::npos ||
        upperType.find("DFF") != std::string::npos ||
        upperType.find("SDFF") != std::string::npos ||
        upperType.find("FLIP") != std::string::npos ||
        upperType.find("FLOP") != std::string::npos ||
        upperType.find("LATCH") != std::string::npos ||
        upperType.find("REG") != std::string::npos ||
        upperType.find("FSD") != std::string::npos);
}

// Get combined orientation from row and cell orientations
string Legalizer::getCombinedOrientation(const string& rowOrient,
    const string& cellOrient) const {
    auto rowIt = orientMap_.find(rowOrient);
    if (rowIt != orientMap_.end()) {
        auto cellIt = rowIt->second.find(cellOrient);
        if (cellIt != rowIt->second.end()) {
            return cellIt->second;
        }
    }
    // Default: use row orientation
    return rowOrient;
}

// Get cell dimensions considering orientation
void Legalizer::getCellDimensions(const string& cellType,
    const string& finalOrient,
    double& width, double& height) const {
    auto macroIt = macroMap_.find(cellType);
    if (macroIt == macroMap_.end()) {
        width = height = 0;
        return;
    }

    // Base dimensions from LEF (scaled by DEF units)
    double baseWidth = macroIt->second.sizeX * defUnits_;
    double baseHeight = macroIt->second.sizeY * defUnits_;

    // Apply orientation transformation
    if (finalOrient == "N" || finalOrient == "S" ||
        finalOrient == "FN" || finalOrient == "FS") {
        width = baseWidth;
        height = baseHeight;
    }
    else {
        // 90-degree rotation: swap width and height
        width = baseHeight;
        height = baseWidth;
    }
}

std::vector<std::string> Legalizer::getAllowedCellOrients(const std::string& cellType) const {
    std::vector<std::string> orients;
    auto it = macroMap_.find(cellType);
    if (it == macroMap_.end()) {
        orients.push_back("N");
        return orients;
    }

    const auto& syms = it->second.symmetry; // std::vector<std::string>

    auto icmp = [](const std::string& a, const std::string& b) {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(),
            [](char c1, char c2) { return toupper(c1) == toupper(c2); });
        };
    bool hasX = std::find(syms.begin(), syms.end(), "X") != syms.end();
    bool hasY = std::find(syms.begin(), syms.end(), "Y") != syms.end();
    bool hasR90 = std::find(syms.begin(), syms.end(), "R90") != syms.end();


    // N orientation is always available
    orients.push_back("N");

    // Add orientations based on symmetry capabilities
    if (hasY) {
        orients.push_back("FN");
    }

    if (hasX) {
        orients.push_back("FS");
    }

    // S orientation requires BOTH X and Y symmetry (180-degree rotation)
    if (hasX && hasY) {
        orients.push_back("S");
    }

    // 90-degree rotations require R90 capability
    if (hasR90) {
        orients.push_back("E");
        orients.push_back("W");
        orients.push_back("FE");
        orients.push_back("FW");
    }

    // Remove duplicates (though there shouldn't be any with current logic)
    std::sort(orients.begin(), orients.end());
    orients.erase(std::unique(orients.begin(), orients.end()), orients.end());

    return orients;
}

bool Legalizer::isOrientLegalForMacro(const LefMacroInfo& macro, const std::string& orient) const {
    const auto& syms = macro.symmetry; // std::vector<std::string>

    bool hasX = std::find(syms.begin(), syms.end(), "X") != syms.end();
    bool hasY = std::find(syms.begin(), syms.end(), "Y") != syms.end();
    bool hasR90 = std::find(syms.begin(), syms.end(), "R90") != syms.end();
    bool hasR180 = std::find(syms.begin(), syms.end(), "R180") != syms.end();


    // N orientation is always legal (identity)
    if (orient == "N") return true;

    // Mirror Y (flip around Y-axis)
    if (orient == "FN") return hasY;

    // Mirror X (flip around X-axis) 
    if (orient == "FS") return hasX;

    // 180-degree rotation (requires both X and Y symmetry OR explicit R180)
    if (orient == "S") return (hasX && hasY) || hasR180;

    // 90-degree rotations (requires R90 capability)
    if (orient == "E" || orient == "W" || orient == "FE" || orient == "FW") {
        return hasR90;
    }

    return false;
}

// Modified legalizeCellInRow method with final orientation validation:
bool Legalizer::legalizeCellInRow(CellToLegalize& cell, int rowIdx) {
    if (rowIdx < 0 || rowIdx >= rows_.size()) return false;
    LegalizerRow& row = rows_[rowIdx];

    // Get macro info
    auto macroIt = macroMap_.find(cell.cellType);
    if (macroIt == macroMap_.end()) return false;

    // 取得 cell 的 symmetry 屬性
    const auto& syms = macroIt->second.symmetry;
    bool hasX = std::find(syms.begin(), syms.end(), "X") != syms.end();
    bool hasY = std::find(syms.begin(), syms.end(), "Y") != syms.end();
    bool hasR90 = std::find(syms.begin(), syms.end(), "R90") != syms.end();

    // 根據 LEF/DEF 規格和 row orientation 決定可用的 cell orientations
    std::vector<std::string> allowedOrients;

    if (row.orientation == "N") {
        // === N row 的規則 ===
        // 永遠可以使用 N orientation
        allowedOrients.push_back("N");

        // 如果有 Y symmetry，可以使用 FN (MY - mirror around Y axis)
        if (hasY) {
            allowedOrients.push_back("FN");
        }

        // 重要：即使有 X symmetry，在 N row 也不能使用 FS 或 S
        // FS 和 S 只能在 FS row 中使用
    }
    else if (row.orientation == "FS") {
        // === FS row 的規則 ===
        // 根據 symmetry 決定可用的 orientations

        if (hasX && hasY) {
            // 有 X 和 Y symmetry：可以使用所有四種基本 orientations
            allowedOrients.push_back("FS");  // 優先
            allowedOrients.push_back("S");   // 次選
            allowedOrients.push_back("N");   // 
            allowedOrients.push_back("FN");  // 
        }
        else if (hasX && !hasY) {
            // 只有 X symmetry：可以使用 FS 和 S
            allowedOrients.push_back("FS");
            allowedOrients.push_back("S");
        }
        else if (!hasX && hasY) {
            // 只有 Y symmetry：可以使用 N 和 FN
            allowedOrients.push_back("N");
            allowedOrients.push_back("FN");
        }
        else {
            // 沒有 symmetry：只能使用 N
            allowedOrients.push_back("N");
        }
    }
    else {
        // 其他 row orientations (E, W, FE, FW, FN, S)
        // 暫時只處理 N
        allowedOrients.push_back("N");
        cerr << "Warning: Row orientation " << row.orientation
            << " not fully implemented, using N" << endl;
    }

    // 偵錯輸出
    static int debugCount = 0;
    bool showDebug = (debugCount++ < 20);

    if (showDebug) {
        cout << "\n  Legalizing " << cell.instName
            << " (type=" << cell.cellType << ")" << endl;
        cout << "    Symmetry: ";
        if (hasX) cout << "X ";
        if (hasY) cout << "Y ";
        if (hasR90) cout << "R90 ";
        if (!hasX && !hasY && !hasR90) cout << "none";
        cout << endl;
        cout << "    Row " << row.name << " orientation: " << row.orientation << endl;
        cout << "    Allowed orientations: ";
        for (const auto& o : allowedOrients) cout << o << " ";
        cout << endl;
    }

    // 嘗試每個允許的 orientation
    for (const auto& orient : allowedOrients) {
        // 對於單列宏，最終 orientation 就是選擇的 orientation
        // 不需要組合 row 和 cell orientation
        string finalOrient = orient;

        // 取得在此 orientation 下的 cell 尺寸
        double width, height;
        getCellDimensions(cell.cellType, finalOrient, width, height);

        // 計算需要的 rows 和 sites
        int needRows = (height > 1.5 * rowHeight_) ? 2 : 1;
        int needSites = (int)ceil(width / siteWidth_);

        if (showDebug) {
            cout << "    Trying orientation " << orient
                << ": width=" << width << ", height=" << height
                << ", needSites=" << needSites << ", needRows=" << needRows << endl;
        }

        // 處理單列 cell
        if (needRows == 1) {
            int startSite;
            if (findAvailableSites(row, needSites, startSite, cell.origX)) {
                // 找到可用位置，放置 cell
                cell.newX = row.getSiteX(startSite);
                cell.newY = row.y;
                cell.newOrient = finalOrient;
                cell.cellOrient = orient;  // 儲存選擇的 orientation
                cell.startSite = startSite;
                cell.legalized = true;

                // 標記 sites 為已佔用
                markSitesOccupied(row, startSite, needSites, cell.instName);

                if (showDebug) {
                    cout << "    → SUCCESS: Placed at site " << startSite
                        << " with orientation " << finalOrient << endl;
                }

                return true;
            }
        }
        // 處理雙列 cell (tall macro)
        else if (needRows == 2) {
            if (rowIdx + 1 >= rows_.size()) {
                if (showDebug) {
                    cout << "    → Cannot place tall macro: no next row" << endl;
                }
                continue;
            }

            LegalizerRow& nextRow = rows_[rowIdx + 1];
            int startSiteTop, startSiteBot;

            // 檢查兩個相鄰 row 是否都有空間
            if (findAvailableSites(row, needSites, startSiteTop, cell.origX) &&
                findAvailableSites(nextRow, needSites, startSiteBot, cell.origX)) {

                // 確保兩個 row 使用相同的起始 site
                if (startSiteTop != startSiteBot) {
                    // 嘗試對齊到相同位置
                    int alignedSite = min(startSiteTop, startSiteBot);
                    bool topOk = true, botOk = true;

                    // 檢查對齊位置是否可用
                    for (int s = alignedSite; s < alignedSite + needSites; ++s) {
                        if (s >= row.siteCount || row.sites[s].occupied) topOk = false;
                        if (s >= nextRow.siteCount || nextRow.sites[s].occupied) botOk = false;
                    }

                    if (!topOk || !botOk) {
                        if (showDebug) {
                            cout << "    → Cannot align tall macro sites" << endl;
                        }
                        continue;
                    }
                    startSiteTop = startSiteBot = alignedSite;
                }

                // 放置雙列 cell
                cell.newX = row.getSiteX(startSiteTop);
                cell.newY = row.y;
                cell.newOrient = finalOrient;
                cell.cellOrient = orient;
                cell.startSite = startSiteTop;
                cell.legalized = true;

                // 標記兩個 row 的 sites
                markSitesOccupied(row, startSiteTop, needSites, cell.instName);
                markSitesOccupied(nextRow, startSiteBot, needSites, cell.instName);

                if (showDebug) {
                    cout << "    → SUCCESS: Placed tall macro at site " << startSiteTop
                        << " with orientation " << finalOrient << endl;
                }

                return true;
            }
        }
    }

    if (showDebug) {
        cout << "    → FAILED: No valid placement found" << endl;
    }

    return false;
}

// Build row structures from DEF data
void Legalizer::buildRows() {
    rows_.clear();

    cout << "\nBuilding row structures..." << endl;

    for (const auto& row : defData_.rows) {
        double x0 = row.x;
        double y = row.y;
        double stepX = (row.stepX > 0) ? row.stepX : siteWidth_;

        rows_.emplace_back(row.name, x0, y, row.count, stepX, row.orientation);

        if (rows_.size() <= 5) {
            cout << "  Row " << row.name << ": x=" << x0 << ", y=" << y
                << ", sites=" << row.count << ", step=" << stepX
                << ", orient=" << row.orientation << endl;
        }
    }

    cout << "Total rows: " << rows_.size() << endl;
}
void Legalizer::detectInitialOverlaps() {
    cout << "\nDetecting initial overlaps..." << endl;

    // 建立位置到 cells 的映射
    std::map<std::pair<double, double>, std::vector<int>> positionMap;

    for (int i = 0; i < cellsToLegalize_.size(); ++i) {
        auto& cell = cellsToLegalize_[i];
        auto pos = std::make_pair(cell.origX, cell.origY);
        positionMap[pos].push_back(i);
    }

    // 找出重疊的 cells
    int overlapCount = 0;
    for (const auto& [pos, indices] : positionMap) {
        if (indices.size() > 1) {
            overlapCount++;
            cout << "  Overlap at (" << pos.first << ", " << pos.second
                << "): " << indices.size() << " cells" << endl;

            // 標記這些 cells 需要特殊處理
            for (int i = 1; i < indices.size(); ++i) {
                cellsToLegalize_[indices[i]].hasInitialOverlap = true;
            }
        }
    }

    cout << "  Found " << overlapCount << " overlap positions" << endl;
}
// Identify blockages (macros, blockages, combinational cells)
void Legalizer::identifyBlockages() {
    blockages_.clear();

    cout << "\nIdentifying blockages..." << endl;
    int debugCount = 0;

    // 首先添加 DEF 文件中定義的 BLOCKAGES
    cout << "  Adding DEF BLOCKAGES..." << endl;
    for (const auto& defBlk : defData_.blockages) {
        // 只處理 PLACEMENT blockages（不處理 LAYER blockages）
        if (defBlk.type == DefBlockageInfo::PLACEMENT) {
            double x1 = defBlk.x1;
            double y1 = defBlk.y1;
            double x2 = defBlk.x2;
            double y2 = defBlk.y2;

            blockages_.emplace_back(x1, y1, x2, y2, "DEF_BLOCKAGE");

            if (debugCount++ < 10) {
                cout << "  DEF Blockage: "
                    << "(" << x1 << "," << y1 << ") to "
                    << "(" << x2 << "," << y2 << ")" << endl;
            }
        }
    }
    cout << "  Added " << blockages_.size() << " DEF blockages" << endl;

    // 然後添加 non-FF cells 作為 blockages（現有代碼）
    int nonFFBlockages = 0;
    for (const auto& comp : defData_.components) {
        // Skip flip-flops (they will be legalized)
        if (isFlipFlopCell(comp.cellType)) {
            continue;
        }

        // Get cell dimensions
        auto macroIt = macroMap_.find(comp.cellType);
        if (macroIt != macroMap_.end()) {
            // 直接使用 component 的 orientation，不需要組合
            string cellOrient = comp.orient;

            // 計算實際尺寸
            double width, height;
            getCellDimensions(comp.cellType, cellOrient, width, height);

            double x1 = comp.x;
            double y1 = comp.y;
            double x2 = x1 + width;
            double y2 = y1 + height;

            blockages_.emplace_back(x1, y1, x2, y2, comp.cellType);
            nonFFBlockages++;

            // 偵錯輸出
            if (debugCount++ < 10) {
                cout << "  Component Blockage: " << comp.name
                    << " (" << comp.cellType << ")"
                    << " at (" << x1 << "," << y1 << ")"
                    << " size " << width << "x" << height
                    << " orient=" << cellOrient << endl;
            }
        }
    }

    cout << "  Found " << blockages_.size() << " total blockages "
        << "(" << (blockages_.size() - nonFFBlockages) << " DEF + "
        << nonFFBlockages << " components)" << endl;
}
// Mark blocked sites in rows
void Legalizer::markBlockedSites() {
    cout << "\nMarking blocked sites..." << endl;
    int totalBlockedSites = 0;
    int debugCount = 0;

    for (auto& row : rows_) {
        int blockedInRow = 0;

        for (const auto& blockage : blockages_) {
            // 檢查 blockage 是否與這個 row 重疊
            // blockage 必須在 row 的 Y 範圍內
            if (blockage.y1 < row.y + rowHeight_ && blockage.y2 > row.y) {
                // 計算受影響的 site 範圍
                // 使用 floor 來找起始 site（包含部分重疊）
                int startSite = (int)floor((blockage.x1 - row.x0) / row.siteWidth);
                // 使用 ceil 來找結束 site（包含部分重疊）
                int endSite = (int)ceil((blockage.x2 - row.x0) / row.siteWidth);

                // 確保在有效範圍內
                startSite = max(0, startSite);
                endSite = min(row.siteCount, endSite);

                // 偵錯輸出
                if (debugCount++ < 20 && endSite > startSite) {
                    cout << "  Row " << row.name << " (y=" << row.y
                        << "): blockage from site " << startSite
                        << " to " << endSite - 1
                        << " (x: " << blockage.x1 << "-" << blockage.x2 << ")"
                        << " type: " << blockage.type << endl;
                }

                // 標記 sites 為已佔用
                for (int s = startSite; s < endSite; ++s) {
                    if (!row.sites[s].occupied) {
                        row.sites[s].occupied = true;
                        row.sites[s].instanceName = blockage.type;
                        blockedInRow++;
                    }
                }
            }
        }

        if (blockedInRow > 0) {
            totalBlockedSites += blockedInRow;
        }
    }

    cout << "  Total blocked sites: " << totalBlockedSites << endl;
}
bool Legalizer::findEmergencyPosition(CellToLegalize& cell) {
    cout << "  Finding emergency position for " << cell.instName << endl;

    // 獲取 cell 的 macro 資訊
    auto macroIt = macroMap_.find(cell.cellType);
    if (macroIt == macroMap_.end()) return false;

    // 取得 symmetry
    const auto& syms = macroIt->second.symmetry;
    bool hasX = std::find(syms.begin(), syms.end(), "X") != syms.end();
    bool hasY = std::find(syms.begin(), syms.end(), "Y") != syms.end();

    // 按距離排序所有 rows
    std::vector<std::pair<int, double>> rowDistances;
    for (int i = 0; i < rows_.size(); ++i) {
        double dist = abs(cell.origY - rows_[i].y);
        rowDistances.push_back({ i, dist });
    }

    std::sort(rowDistances.begin(), rowDistances.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });

    // 嘗試每個 row
    for (const auto& [rowIdx, dist] : rowDistances) {
        auto& row = rows_[rowIdx];

        // 根據 row orientation 決定可用的 orientations
        std::vector<std::string> tryOrients;

        if (row.orientation == "N") {
            tryOrients.push_back("N");
            if (hasY) {
                tryOrients.push_back("FN");
            }
        }
        else if (row.orientation == "FS") {
            if (hasX && hasY) {
                tryOrients = { "FS", "S", "N", "FN" };
            }
            else if (hasX) {
                tryOrients = { "FS", "S" };
            }
            else if (hasY) {
                tryOrients = { "N", "FN" };
            }
            else {
                tryOrients = { "N" };
            }
        }
        else {
            tryOrients = { "N" };  // 其他 row 暫時只用 N
        }

        // 嘗試每個 orientation
        for (const auto& orient : tryOrients) {
            string finalOrient = orient;

            double width, height;
            getCellDimensions(cell.cellType, finalOrient, width, height);

            int needRows = (height > 1.5 * rowHeight_) ? 2 : 1;
            int needSites = (int)ceil(width / siteWidth_);

            // 只處理單列 cells (緊急放置通常不處理 tall macros)
            if (needRows > 1) continue;

            // 掃描所有可能的位置
            for (int startSite = 0; startSite <= row.siteCount - needSites; ++startSite) {
                bool canPlace = true;

                for (int s = startSite; s < startSite + needSites; ++s) {
                    if (row.sites[s].occupied) {
                        canPlace = false;
                        break;
                    }
                }

                if (canPlace) {
                    cell.newX = row.getSiteX(startSite);
                    cell.newY = row.y;
                    cell.newOrient = finalOrient;
                    cell.cellOrient = orient;
                    cell.startSite = startSite;
                    cell.legalized = true;
                    cell.assignedRow = rowIdx;

                    markSitesOccupied(row, startSite, needSites, cell.instName);

                    cout << "    Emergency placement at row " << row.name
                        << " (orient=" << row.orientation << ")"
                        << ", site " << startSite
                        << ", cell orient=" << finalOrient << endl;
                    return true;
                }
            }
        }
    }

    return false;
}

// Collect cells that need legalization
void Legalizer::collectCellsToLegalize() {
    cellsToLegalize_.clear();

    cout << "\nCollecting cells to legalize..." << endl;

    // 偵錯：顯示 FF cell types 資訊
    if (!flipFlopCellTypes_.empty()) {
        cout << "  Using " << flipFlopCellTypes_.size() << " FF cell types from library" << endl;
        // 顯示前幾個 FF cell types
        int count = 0;
        for (const auto& ffType : flipFlopCellTypes_) {
            if (count++ < 5) {
                cout << "    - " << ffType << endl;
            }
            else {
                cout << "    ... and " << (flipFlopCellTypes_.size() - 5) << " more" << endl;
                break;
            }
        }
    }

    // 偵錯：列出前幾個 components
    cout << "  Checking components:" << endl;
    int debugCount = 0;
    for (const auto& comp : defData_.components) {
        if (debugCount++ < 10) {
            bool isFF = isFlipFlopCell(comp.cellType);
            cout << "    " << comp.name << " (" << comp.cellType << ") - "
                << (isFF ? "IS FF" : "NOT FF") << endl;
        }
    }

    // 收集需要 legalize 的 cells
    for (const auto& comp : defData_.components) {
        if (!isFlipFlopCell(comp.cellType)) {
            continue;
        }

        CellToLegalize cell;
        cell.instName = comp.name;
        cell.cellType = comp.cellType;
        cell.origX = comp.x;
        cell.origY = comp.y;
        cell.origOrient = comp.orient;
        cell.hasInitialOverlap = false;

        auto macroIt = macroMap_.find(comp.cellType);
        if (macroIt != macroMap_.end()) {
            double baseWidth = macroIt->second.sizeX * defUnits_;
            cell.width = baseWidth;
            cell.height = macroIt->second.sizeY * defUnits_;
            cell.needSites = (int)ceil(cell.width / siteWidth_);

            cellsToLegalize_.push_back(cell);
        }
    }

    cout << "  Found " << cellsToLegalize_.size() << " cells to legalize" << endl;

    // 偵測初始重疊
    detectInitialOverlaps();
}
void Legalizer::updateDefComponents(DefData& defData) {
    cout << "\nUpdating DEF components with legalized positions..." << endl;

    // Create a map for quick lookup
    unordered_map<string, const CellToLegalize*> cellMap;
    for (const auto& cell : cellsToLegalize_) {
        cellMap[cell.instName] = &cell;
    }

    // Update components
    int updateCount = 0;
    int notLegalizedCount = 0;

    for (auto& comp : defData.components) {
        if (!isFlipFlopCell(comp.cellType)) {
            continue;
        }

        auto it = cellMap.find(comp.name);
        if (it != cellMap.end()) {
            const CellToLegalize* cell = it->second;

            if (cell->legalized) {
                if (updateCount < 5) {
                    cout << "  Updating " << comp.name << ": "
                        << "(" << comp.x << "," << comp.y << ") -> "
                        << "(" << cell->newX << "," << cell->newY << "), "
                        << "orient=" << cell->newOrient << endl;
                }
                comp.x = cell->newX;
                comp.y = cell->newY;
                comp.orient = cell->newOrient;
                comp.status = "PLACED";
                updateCount++;
            }
            else {
                // 對於未能 legalize 的 cell，至少確保它們不重疊
                notLegalizedCount++;
                cerr << "Error: " << comp.name << " was not legalized!" << endl;

                // 可以選擇：保持原位但標記為 UNPLACED
                // comp.status = "UNPLACED";

                // 或者：移到一個安全的預設位置
                // comp.x = defData_.dieArea.xMin + notLegalizedCount * siteWidth_;
                // comp.y = defData_.dieArea.yMin;
            }
        }
    }

    cout << "Updated " << updateCount << " flip-flop positions" << endl;
    if (notLegalizedCount > 0) {
        cerr << "WARNING: " << notLegalizedCount << " flip-flops were not legalized!" << endl;
    }
}

// Assign cells to rows based on Y coordinate
void Legalizer::assignCellsToRows() {
    cout << "\nAssigning cells to rows..." << endl;

    // Sort rows by Y coordinate
    sort(rows_.begin(), rows_.end(),
        [](const LegalizerRow& a, const LegalizerRow& b) {
            return a.y < b.y;
        });

    // For each cell, find closest row
    for (auto& cell : cellsToLegalize_) {
        double minDist = numeric_limits<double>::max();
        int bestRow = 0;

        for (int i = 0; i < rows_.size(); ++i) {
            double dist = abs(cell.origY - rows_[i].y);
            if (dist < minDist) {
                minDist = dist;
                bestRow = i;
            }
        }

        cell.assignedRow = bestRow;
    }

    // Sort cells by assigned row, then by X coordinate
    sort(cellsToLegalize_.begin(), cellsToLegalize_.end(),
        [](const CellToLegalize& a, const CellToLegalize& b) {
            if (a.assignedRow != b.assignedRow) {
                return a.assignedRow < b.assignedRow;
            }
            return a.origX < b.origX;
        });
}
// 在 Legalizer 類中加入新方法
bool Legalizer::checkCellOverlap(double x1, double y1, double x2, double y2) const {
    // 檢查是否與任何 blockage 重疊
    for (const auto& blockage : blockages_) {
        // 檢查矩形重疊
        if (!(x2 <= blockage.x1 || x1 >= blockage.x2 ||
            y2 <= blockage.y1 || y1 >= blockage.y2)) {
            return true;  // 有重疊
        }
    }
    return false;
}
// Find available consecutive sites in a row
bool Legalizer::findAvailableSites(LegalizerRow& row, int needSites,
    int& startSite, double targetX) {
    // Calculate ideal starting site based on target X
    int idealSite = (int)round((targetX - row.x0) / row.siteWidth);
    idealSite = max(0, min(idealSite, row.siteCount - needSites));

    // 偵錯用
    static int debugCount = 0;
    bool showDebug = (debugCount++ < 20);

    // Search strategy: start from ideal position and expand outward
    for (int offset = 0; offset < row.siteCount; ++offset) {
        // Try left side
        int leftSite = idealSite - offset;
        if (leftSite >= 0 && leftSite + needSites <= row.siteCount) {
            bool available = true;

            // 檢查每個 site 是否可用
            for (int s = leftSite; s < leftSite + needSites; ++s) {
                if (row.sites[s].occupied) {
                    available = false;
                    break;
                }
            }

            if (available) {
                // 額外檢查：確認不會與 blockages 重疊
                double cellX1 = row.getSiteX(leftSite);
                double cellY1 = row.y;
                double cellX2 = cellX1 + needSites * row.siteWidth;
                double cellY2 = cellY1 + rowHeight_;

                if (!checkCellOverlap(cellX1, cellY1, cellX2, cellY2)) {
                    startSite = leftSite;
                    return true;
                }
                else if (showDebug) {
                    cout << "    Site " << leftSite << " available but overlaps with blockage" << endl;
                }
            }
        }

        // Try right side (相同的檢查邏輯)
        int rightSite = idealSite + offset;
        if (rightSite >= 0 && rightSite + needSites <= row.siteCount) {
            bool available = true;

            for (int s = rightSite; s < rightSite + needSites; ++s) {
                if (row.sites[s].occupied) {
                    available = false;
                    break;
                }
            }

            if (available) {
                double cellX1 = row.getSiteX(rightSite);
                double cellY1 = row.y;
                double cellX2 = cellX1 + needSites * row.siteWidth;
                double cellY2 = cellY1 + rowHeight_;

                if (!checkCellOverlap(cellX1, cellY1, cellX2, cellY2)) {
                    startSite = rightSite;
                    return true;
                }
                else if (showDebug) {
                    cout << "    Site " << rightSite << " available but overlaps with blockage" << endl;
                }
            }
        }
    }

    return false;
}

// Mark sites as occupied
void Legalizer::markSitesOccupied(LegalizerRow& row, int startSite,
    int numSites, const string& instName) {
    for (int s = startSite; s < startSite + numSites; ++s) {
        row.sites[s].occupied = true;
        row.sites[s].instanceName = instName;
    }
}

// Try to spill cell to nearby rows
bool Legalizer::spillToNearbyRow(CellToLegalize& cell, int origRowIdx) {
    // Try adjacent rows (alternating up and down)
    for (int offset = 1; offset < rows_.size(); ++offset) {
        // Try row above
        int upRow = origRowIdx - offset;
        if (legalizeCellInRow(cell, upRow)) {
            cell.assignedRow = upRow;
            return true;
        }

        // Try row below
        int downRow = origRowIdx + offset;
        if (legalizeCellInRow(cell, downRow)) {
            cell.assignedRow = downRow;
            return true;
        }
    }

    return false;
}

// Main legalization method
bool Legalizer::legalizeAll() {
    cout << "\n=== Starting Row-Based Greedy Legalization ===" << endl;

    // Step 1-5: 原有步驟
    buildRows();
    if (rows_.empty()) {
        cerr << "Error: No rows found in DEF" << endl;
        return false;
    }

    identifyBlockages();
    markBlockedSites();
    collectCellsToLegalize();

    if (cellsToLegalize_.empty()) {
        cout << "No cells to legalize" << endl;
        return true;
    }

    assignCellsToRows();

    // Step 6: Legalize cells
    int legalizedCount = 0;
    int spilledCount = 0;
    int emergencyCount = 0;
    int failedCount = 0;

    std::vector<int> failedIndices;  // 記錄失敗的 cells

    cout << "\nLegalizing cells..." << endl;

    // 優先處理有初始重疊的 cells
    std::sort(cellsToLegalize_.begin(), cellsToLegalize_.end(),
        [](const CellToLegalize& a, const CellToLegalize& b) {
            if (a.hasInitialOverlap != b.hasInitialOverlap) {
                return !a.hasInitialOverlap;  // 非重疊的先處理
            }
            if (a.assignedRow != b.assignedRow) {
                return a.assignedRow < b.assignedRow;
            }
            return a.origX < b.origX;
        });

    for (int i = 0; i < cellsToLegalize_.size(); ++i) {
        auto& cell = cellsToLegalize_[i];

        // 如果有初始重疊，稍微調整目標位置
        if (cell.hasInitialOverlap) {
            cell.origX += siteWidth_;  // 往右移一個 site 作為初始嘗試
        }

        // 嘗試正常 legalization
        if (legalizeCellInRow(cell, cell.assignedRow)) {
            legalizedCount++;
        }
        else if (spillToNearbyRow(cell, cell.assignedRow)) {
            legalizedCount++;
            spilledCount++;
        }
        else if (findEmergencyPosition(cell)) {
            legalizedCount++;
            emergencyCount++;
        }
        else {
            failedIndices.push_back(i);
            failedCount++;
            cerr << "Warning: Failed to legalize " << cell.instName
                << " (type: " << cell.cellType << ")" << endl;
        }

        if (legalizedCount % 1000 == 0) {
            cout << "  Legalized " << legalizedCount << " cells..." << endl;
        }
    }

    // 最後嘗試：為失敗的 cells 尋找任何可用空間
    if (!failedIndices.empty()) {
        cout << "\nAttempting final placement for " << failedIndices.size()
            << " failed cells..." << endl;

        for (int idx : failedIndices) {
            auto& cell = cellsToLegalize_[idx];

            // 釋放一些限制，再試一次
            cell.origX = 0;  // 不考慮原始位置
            if (findEmergencyPosition(cell)) {
                failedCount--;
                emergencyCount++;
                cout << "  Successfully placed " << cell.instName
                    << " in final attempt" << endl;
            }
        }
    }

    cout << "\n=== Legalization Summary ===" << endl;
    cout << "Total cells: " << cellsToLegalize_.size() << endl;
    cout << "Successfully legalized: " << legalizedCount << endl;
    cout << "Spilled to other rows: " << spilledCount << endl;
    cout << "Emergency placements: " << emergencyCount << endl;
    cout << "Failed to legalize: " << failedCount << endl;
    checkFinalOverlaps();
    return failedCount == 0;
}



// Get count of successfully legalized cells
int Legalizer::getLegalizedCount() const {
    int count = 0;
    for (const auto& cell : cellsToLegalize_) {
        if (cell.legalized) count++;
    }
    return count;
}

// Print legalization summary
void Legalizer::printLegalizationSummary() const {
    cout << "\n=== Legalization Report ===" << endl;
    cout << "Total cells to legalize: " << cellsToLegalize_.size() << endl;
    cout << "Successfully legalized: " << getLegalizedCount() << endl;
    cout << "Site width: " << siteWidth_ << " DEF units" << endl;
    cout << "Row height: " << rowHeight_ << " DEF units" << endl;

    // Analyze row utilization
    cout << "\nRow Utilization:" << endl;
    for (size_t i = 0; i < min(rows_.size(), size_t(10)); ++i) {
        const auto& row = rows_[i];
        int usedSites = 0;
        for (const auto& site : row.sites) {
            if (site.occupied) usedSites++;
        }
        double utilization = 100.0 * usedSites / row.siteCount;
        cout << "  Row " << row.name << ": "
            << usedSites << "/" << row.siteCount
            << " sites (" << fixed << setprecision(1)
            << utilization << "%)" << endl;
    }
    if (rows_.size() > 10) {
        cout << "  ... (showing first 10 rows)" << endl;
    }
}

// Export detailed legalization report
void Legalizer::exportLegalizationReport(const string& filename) const {
    ofstream ofs(filename);
    if (!ofs.is_open()) {
        cerr << "Error: Cannot create legalization report file: " << filename << endl;
        return;
    }

    ofs << "# Row-Based Greedy Legalization Report" << endl;
    ofs << "# Generated by Legalizer" << endl;
    ofs << "# Site width: " << siteWidth_ << " DEF units" << endl;
    ofs << "# Row height: " << rowHeight_ << " DEF units" << endl;
    ofs << "# Total cells: " << cellsToLegalize_.size() << endl;
    ofs << "# Successfully legalized: " << getLegalizedCount() << endl;
    ofs << "#" << endl;
    ofs << "# Format: InstName CellType Width Sites OrigX OrigY OrigOrient CellOrient NewX NewY FinalOrient Status" << endl;

    for (const auto& cell : cellsToLegalize_) {
        ofs << cell.instName << " "
            << cell.cellType << " "
            << fixed << setprecision(3) << cell.width << " "
            << cell.needSites << " "
            << cell.origX << " " << cell.origY << " "
            << cell.origOrient << " "
            << cell.cellOrient << " "
            << cell.newX << " " << cell.newY << " "
            << cell.newOrient << " "
            << (cell.legalized ? "LEGALIZED" : "FAILED") << endl;
    }

    ofs.close();
    cout << "Legalization report exported to: " << filename << endl;
}

// Set banking list
void Legalizer::setBankingList(const std::vector<MBFFInstance>& bankingList) {
    bankingList_ = bankingList;
}
void Legalizer::checkFinalOverlaps() const {
    cout << "\nChecking for overlaps..." << endl;
    int overlapCount = 0;

    for (const auto& cell : cellsToLegalize_) {
        if (!cell.legalized) continue;

        // 計算 cell 的邊界
        double width, height;
        getCellDimensions(cell.cellType, cell.newOrient, width, height);

        double x1 = cell.newX;
        double y1 = cell.newY;
        double x2 = x1 + width;
        double y2 = y1 + height;

        // 檢查是否與 blockages 重疊
        for (const auto& blockage : blockages_) {
            if (!(x2 <= blockage.x1 || x1 >= blockage.x2 ||
                y2 <= blockage.y1 || y1 >= blockage.y2)) {
                overlapCount++;
                if (overlapCount <= 10) {
                    cout << "  OVERLAP: " << cell.instName
                        << " at (" << x1 << "," << y1 << ")"
                        << " overlaps with blockage at ("
                        << blockage.x1 << "," << blockage.y1 << ")" << endl;
                }
                break;
            }
        }
    }

    if (overlapCount > 0) {
        cout << "WARNING: Found " << overlapCount << " cells overlapping with blockages!" << endl;
    }
    else {
        cout << "✓ No overlaps detected" << endl;
    }
}