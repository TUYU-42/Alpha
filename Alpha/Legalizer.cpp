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
    const std::vector<LefSiteInfo>& lefSites)
    : defData_(defData), macroMap_(macroMap), lefSites_(lefSites),
    siteWidth_(0), rowHeight_(0) {

    // Initialize orientation mapping
    initializeOrientationMap();

    // Get DEF units
    defUnits_ = (defData_.units > 0) ? defData_.units : 1000;
    cout << "DEF units: " << defUnits_ << endl;

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
    // Check if cell type contains FF-related keywords
    std::string upperType = cellType;
    std::transform(upperType.begin(), upperType.end(), upperType.begin(), ::toupper);

    return (upperType.find("FF") != std::string::npos ||
        upperType.find("DFF") != std::string::npos ||
        upperType.find("SDFF") != std::string::npos ||
        upperType.find("FLIP") != std::string::npos ||
        upperType.find("FLOP") != std::string::npos ||
        upperType.find("LATCH") != std::string::npos ||
        upperType.find("REG") != std::string::npos);
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

    std::string sym = it->second.symmetry;
    std::transform(sym.begin(), sym.end(), sym.begin(), ::toupper);

    bool hasX = (sym.find('X') != std::string::npos);
    bool hasY = (sym.find('Y') != std::string::npos);
    bool hasR90 = (sym.find("R90") != std::string::npos);

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
    std::string sym = macro.symmetry;
    std::transform(sym.begin(), sym.end(), sym.begin(), ::toupper);

    bool hasX = (sym.find('X') != std::string::npos);
    bool hasY = (sym.find('Y') != std::string::npos);
    bool hasR90 = (sym.find("R90") != std::string::npos);
    bool hasR180 = (sym.find("R180") != std::string::npos) ||
        (sym.find("180") != std::string::npos);

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

    // For FS (flipped) rows, we need to use specific orientations
    // According to LEF/DEF spec, in FS rows:
    // - Cells with symmetry Y: can use N or FN
    // - Cells with symmetry X: can use FS or S
    // - Cells with symmetry X Y: can use N, FN, FS, or S

    std::vector<std::string> allowedOrients;
    if (row.orientation == "FS") {
        // Special handling for FS rows
        std::string sym = macroIt->second.symmetry;
        std::transform(sym.begin(), sym.end(), sym.begin(), ::toupper);

        bool hasX = (sym.find('X') != std::string::npos);
        bool hasY = (sym.find('Y') != std::string::npos);

        if (hasX && hasY) {
            // Can use all four orientations in FS row
            allowedOrients = { "N", "FN", "FS", "S" };
        }
        else if (hasX) {
            // Only FS and S allowed
            allowedOrients = { "FS", "S" };
        }
        else if (hasY) {
            // Only N and FN allowed
            allowedOrients = { "N", "FN" };
        }
        else {
            // No symmetry, only N allowed
            allowedOrients = { "N" };
        }
    }
    else if (row.orientation == "N") {
        // For N rows, use standard allowed orientations
        allowedOrients = getAllowedCellOrients(cell.cellType);
    }
    else {
        // For other row orientations, get allowed orientations
        allowedOrients = getAllowedCellOrients(cell.cellType);
    }

    for (const auto& cellOrient : allowedOrients) {
        // For FS rows, use the cell orientation directly as final orientation
        string finalOrient;
        if (row.orientation == "FS") {
            finalOrient = cellOrient;
        }
        else {
            finalOrient = getCombinedOrientation(row.orientation, cellOrient);
        }

        // Validate that the final orientation is legal for this macro
        if (!isOrientLegalForMacro(macroIt->second, finalOrient)) {
            continue; // Skip this orientation combination
        }

        double width, height;
        getCellDimensions(cell.cellType, finalOrient, width, height);

        // Improved tall macro detection
        int needRows = (height > 1.5 * rowHeight_) ? 2 : 1;
        int needSites = (int)ceil(width / siteWidth_);

        // Single-row cell
        if (needRows == 1) {
            int startSite;
            if (findAvailableSites(row, needSites, startSite, cell.origX)) {
                cell.newX = row.getSiteX(startSite);
                cell.newY = row.y;
                cell.newOrient = finalOrient;
                cell.cellOrient = cellOrient;  // Store the chosen cell orientation
                cell.startSite = startSite;
                cell.legalized = true;
                markSitesOccupied(row, startSite, needSites, cell.instName);
                return true;
            }
        }
        // Double-row cell (tall macro)
        else {
            if (rowIdx + 1 >= rows_.size()) continue;
            LegalizerRow& nextRow = rows_[rowIdx + 1];
            int startSiteTop, startSiteBot;
            if (findAvailableSites(row, needSites, startSiteTop, cell.origX) &&
                findAvailableSites(nextRow, needSites, startSiteBot, cell.origX)) {
                // Ensure both rows use the same starting site
                if (startSiteTop != startSiteBot) {
                    // Try to align to the same position
                    int alignedSite = min(startSiteTop, startSiteBot);
                    bool topOk = true, botOk = true;

                    // Check if aligned position works for both rows
                    for (int s = alignedSite; s < alignedSite + needSites; ++s) {
                        if (s >= row.siteCount || row.sites[s].occupied) topOk = false;
                        if (s >= nextRow.siteCount || nextRow.sites[s].occupied) botOk = false;
                    }

                    if (!topOk || !botOk) continue; // Can't align, try next orientation
                    startSiteTop = startSiteBot = alignedSite;
                }

                cell.newX = row.getSiteX(startSiteTop);
                cell.newY = row.y;
                cell.newOrient = finalOrient;
                cell.cellOrient = cellOrient;  // Store the chosen cell orientation
                cell.startSite = startSiteTop;
                cell.legalized = true;
                markSitesOccupied(row, startSiteTop, needSites, cell.instName);
                markSitesOccupied(nextRow, startSiteBot, needSites, cell.instName);
                return true;
            }
        }
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

// Identify blockages (macros, blockages, combinational cells)
void Legalizer::identifyBlockages() {
    blockages_.clear();

    cout << "\nIdentifying blockages..." << endl;

    // Add macros and fixed cells as blockages
    for (const auto& comp : defData_.components) {
        // Skip flip-flops (they will be legalized)
        if (isFlipFlopCell(comp.cellType)) {
            continue;
        }

        // Get cell dimensions
        auto macroIt = macroMap_.find(comp.cellType);
        if (macroIt != macroMap_.end()) {
            // Get the row where this component is placed
            double compY = comp.y;
            string rowOrient = "N";  // Default

            // Find the row orientation
            for (const auto& row : defData_.rows) {
                if (abs(row.y - compY) < rowHeight_ / 2) {
                    rowOrient = row.orientation;
                    break;
                }
            }

            // Calculate dimensions based on combined orientation
            double width, height;
            string finalOrient = getCombinedOrientation(rowOrient, comp.orient);
            getCellDimensions(comp.cellType, finalOrient, width, height);

            double x1 = comp.x;
            double y1 = comp.y;
            double x2 = x1 + width;
            double y2 = y1 + height;

            blockages_.emplace_back(x1, y1, x2, y2, "CELL");
        }
    }

    cout << "  Found " << blockages_.size() << " blockages" << endl;
}

// Mark blocked sites in rows
void Legalizer::markBlockedSites() {
    cout << "\nMarking blocked sites..." << endl;

    for (auto& row : rows_) {
        for (const auto& blockage : blockages_) {
            // Check if blockage overlaps with this row
            if (blockage.y1 <= row.y + rowHeight_ && blockage.y2 >= row.y) {
                // Calculate site range affected by blockage
                int startSite = max(0, (int)floor((blockage.x1 - row.x0) / row.siteWidth));
                int endSite = min(row.siteCount - 1,
                    (int)ceil((blockage.x2 - row.x0) / row.siteWidth));

                // Mark sites as occupied
                for (int s = startSite; s <= endSite && s < row.siteCount; ++s) {
                    if (s >= 0) {
                        row.sites[s].occupied = true;
                        row.sites[s].instanceName = blockage.type;
                    }
                }
            }
        }
    }
}

// Collect cells that need legalization
void Legalizer::collectCellsToLegalize() {
    cellsToLegalize_.clear();

    cout << "\nCollecting cells to legalize..." << endl;

    for (const auto& comp : defData_.components) {
        // Only legalize flip-flops
        if (!isFlipFlopCell(comp.cellType)) {
            continue;
        }

        CellToLegalize cell;
        cell.instName = comp.name;
        cell.cellType = comp.cellType;
        cell.origX = comp.x;
        cell.origY = comp.y;
        cell.origOrient = comp.orient;

        // Get cell dimensions
        auto macroIt = macroMap_.find(comp.cellType);
        if (macroIt != macroMap_.end()) {
            // Will need to consider row orientation later
            double baseWidth = macroIt->second.sizeX * defUnits_;
            cell.width = baseWidth;
            cell.height = macroIt->second.sizeY * defUnits_;
            cell.needSites = (int)ceil(cell.width / siteWidth_);

            cellsToLegalize_.push_back(cell);
        }
    }

    cout << "  Found " << cellsToLegalize_.size() << " cells to legalize" << endl;
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

// Find available consecutive sites in a row
bool Legalizer::findAvailableSites(LegalizerRow& row, int needSites,
    int& startSite, double targetX) {
    // Calculate ideal starting site based on target X
    int idealSite = (int)round((targetX - row.x0) / row.siteWidth);
    idealSite = max(0, min(idealSite, row.siteCount - needSites));

    // Search strategy: start from ideal position and expand outward
    for (int offset = 0; offset < row.siteCount; ++offset) {
        // Try left side
        int leftSite = idealSite - offset;
        if (leftSite >= 0 && leftSite + needSites <= row.siteCount) {
            bool available = true;
            for (int s = leftSite; s < leftSite + needSites; ++s) {
                if (row.sites[s].occupied) {
                    available = false;
                    break;
                }
            }
            if (available) {
                startSite = leftSite;
                return true;
            }
        }

        // Try right side
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
                startSite = rightSite;
                return true;
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

    // Step 1: Build row structures
    buildRows();
    if (rows_.empty()) {
        cerr << "Error: No rows found in DEF" << endl;
        return false;
    }

    // Step 2: Identify blockages
    identifyBlockages();

    // Step 3: Mark blocked sites
    markBlockedSites();

    // Step 4: Collect cells to legalize
    collectCellsToLegalize();
    if (cellsToLegalize_.empty()) {
        cout << "No cells to legalize" << endl;
        return true;
    }

    // Step 5: Assign cells to rows
    assignCellsToRows();

    // Step 6: Legalize cells row by row
    int legalizedCount = 0;
    int spilledCount = 0;
    int failedCount = 0;

    cout << "\nLegalizing cells..." << endl;

    for (auto& cell : cellsToLegalize_) {
        // Try to place in assigned row first
        if (legalizeCellInRow(cell, cell.assignedRow)) {
            legalizedCount++;
        }
        else {
            // Try spilling to nearby rows
            if (spillToNearbyRow(cell, cell.assignedRow)) {
                legalizedCount++;
                spilledCount++;
            }
            else {
                failedCount++;
                cerr << "Warning: Failed to legalize " << cell.instName
                    << " (type: " << cell.cellType << ")" << endl;
            }
        }

        if (legalizedCount % 1000 == 0) {
            cout << "  Legalized " << legalizedCount << " cells..." << endl;
        }
    }

    cout << "\n=== Legalization Summary ===" << endl;
    cout << "Total cells: " << cellsToLegalize_.size() << endl;
    cout << "Successfully legalized: " << legalizedCount << endl;
    cout << "Spilled to other rows: " << spilledCount << endl;
    cout << "Failed to legalize: " << failedCount << endl;

    return failedCount == 0;
}

void Legalizer::updateDefComponents(DefData& defData) {
    cout << "\nUpdating DEF components with legalized positions..." << endl;

    // Create a map for quick lookup
    unordered_map<string, const CellToLegalize*> cellMap;
    for (const auto& cell : cellsToLegalize_) {
        if (cell.legalized) {
            cellMap[cell.instName] = &cell;
        }
    }

    // Update components
    int updateCount = 0;
    for (auto& comp : defData.components) {
        // Only update flip-flops that were legalized
        if (!isFlipFlopCell(comp.cellType)) {
            continue; // Skip non-flip-flop cells
        }

        auto it = cellMap.find(comp.name);
        if (it != cellMap.end()) {
            const CellToLegalize* cell = it->second;
            if (updateCount < 5) {
                cout << "  Updating " << comp.name << ": "
                    << "(" << comp.x << "," << comp.y << ") -> "
                    << "(" << cell->newX << "," << cell->newY << "), orient=" << cell->newOrient << endl;
            }
            comp.x = cell->newX;
            comp.y = cell->newY;
            comp.orient = cell->newOrient;
            comp.status = "PLACED";
            updateCount++;
        }
    }

    cout << "Updated " << updateCount << " flip-flop positions" << endl;
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