#pragma once
#ifndef LEGALIZER_H
#define LEGALIZER_H

#include "DataStructures.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <set>

// Site occupancy info
struct SiteOccupancy {
    bool occupied;
    std::string instanceName;  // Instance occupying this site (if any)

    SiteOccupancy() : occupied(false) {}
};

// Row structure for legalization
struct LegalizerRow {
    std::string name;
    double x0;              // Starting X coordinate
    double y;               // Y coordinate
    int siteCount;          // Number of sites
    double siteWidth;       // Width of each site
    std::string orientation;
    std::vector<SiteOccupancy> sites;  // Site occupancy map

    // Constructor
    LegalizerRow(const std::string& n, double x, double y_pos, int count,
        double width, const std::string& orient)
        : name(n), x0(x), y(y_pos), siteCount(count),
        siteWidth(width), orientation(orient), sites(count) {
    }

    // Get X coordinate of a specific site
    double getSiteX(int siteIdx) const {
        return x0 + siteIdx * siteWidth;
    }
};

// Cell info for legalization
struct CellToLegalize {
    std::string instName;
    std::string cellType;
    double origX, origY;        // Original position
    double width, height;       // Cell dimensions (considering orientation)
    int needSites;              // Number of sites needed
    std::string origOrient;     // Original orientation

    // Legalized position
    double newX, newY;
    std::string newOrient;
    int assignedRow;            // Row index where placed
    int startSite;              // Starting site index in row
    bool legalized;

    CellToLegalize() : origX(0), origY(0), width(0), height(0),
        needSites(0), newX(0), newY(0),
        assignedRow(-1), startSite(-1), legalized(false) {
    }
};

// Blockage/Macro info
struct BlockageInfo {
    double x1, y1, x2, y2;  // Bounding box
    std::string type;       // "MACRO" or "BLOCKAGE"

    BlockageInfo(double xa, double ya, double xb, double yb, const std::string& t)
        : x1(xa), y1(ya), x2(xb), y2(yb), type(t) {
    }
};

class Legalizer {
private:
    const DefData& defData_;
    const std::unordered_map<std::string, LefMacroInfo>& macroMap_;
    const std::vector<LefSiteInfo>& lefSites_;

    std::vector<LegalizerRow> rows_;
    std::vector<CellToLegalize> cellsToLegalize_;
    std::vector<BlockageInfo> blockages_;

    double siteWidth_;
    double rowHeight_;
    int defUnits_;

    // Orientation mapping table
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> orientMap_;

    // Helper methods
    void initializeOrientationMap();
    std::string getCombinedOrientation(const std::string& rowOrient,
        const std::string& cellOrient) const;
    void getCellDimensions(const std::string& cellType,
        const std::string& finalOrient,
        double& width, double& height) const;

    void buildRows();
    void identifyBlockages();
    void markBlockedSites();
    void collectCellsToLegalize();
    void assignCellsToRows();
    bool findAvailableSites(LegalizerRow& row, int needSites,
        int& startSite, double targetX);
    bool legalizeCellInRow(CellToLegalize& cell, int rowIdx);
    bool spillToNearbyRow(CellToLegalize& cell, int origRowIdx);
    void markSitesOccupied(LegalizerRow& row, int startSite,
        int numSites, const std::string& instName);
    std::vector<std::string> getAllowedCellOrients(const std::string& cellType) const;
public:
    // Constructor
    Legalizer(const DefData& defData,
        const std::unordered_map<std::string, LefMacroInfo>& macroMap,
        const std::vector<LefSiteInfo>& lefSites);

    // Main legalization method
    bool legalizeAll();

    // Update DEF data with legalized positions
    void updateDefComponents(DefData& defData);

    // Statistics and reporting
    void printLegalizationSummary() const;
    void exportLegalizationReport(const std::string& filename) const;

    // Getters
    int getLegalizedCount() const;
    int getTotalCellCount() const { return cellsToLegalize_.size(); }
    const std::vector<CellToLegalize>& getLegalizedCells() const {
        return cellsToLegalize_;
    }
};

#endif // LEGALIZER_H