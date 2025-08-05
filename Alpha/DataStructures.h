#ifndef DATASTRUCTURES_H
#define DATASTRUCTURES_H

#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>
#include<set>

// === »щµAЩYБПЅY? ===

// ™аЦШ…ў"µЅY?
struct Weights {
    double Alpha = 0.0;
    double Beta = 0.0;
    double Gamma = 0.0;
    double TNS = 0.0;
    double TPO = 0.0;
    double Area = 0.0;
};

// Instance-Pin-Net УіЙдЅY?
struct InstPinNet {
    std::string inst;   // instance name
    std::string pin;    // pin name (D / Q / CK / Ў­)
    std::string net;    // net name
};

// ROW ЩYБПЅY?
struct RowInfo {
    std::string name;
    std::string siteName;
    int x, y;
    std::string orientation;
    int count, by, stepX, stepY;
    // ©µ¦щҐ[ЎGҐОЁУ¤с№пҐОЄє range
    int xEnd = x + count * stepX;
    int yEnd = y + by * stepY;  // ­Y by > 1
    int rowXWidth = 0;
    int rowYWidth = 0;
};

struct DieArea {
    int xMin = 0, yMin = 0, xMax = 0, yMax = 0;
};

// TRACK ЩYБПЅY?
struct TrackInfo {
    char direction;
    int start;
    int count;
    int step;
    std::string layer;
};

// COMPONENT ЩYБПЅY?
struct ComponentInfo {
    std::string name;
    std::string cellType;
    int x, y;
    std::string orient;
    std::string rowName;
    std::string status;
};
struct DefBlockageInfo {
    enum BlockageType {
        PLACEMENT,
        LAYER
    };

    BlockageType type;
    std::string layer;     // For LAYER type blockages (e.g., "M2", "M3", etc.)
    int spacing = 0;       // For LAYER type blockages
    int x1, y1, x2, y2;   // Rectangle coordinates

    DefBlockageInfo(BlockageType t, int xa, int ya, int xb, int yb)
        : type(t), x1(xa), y1(ya), x2(xb), y2(yb) {
    }

    DefBlockageInfo(BlockageType t, const std::string& l, int s, int xa, int ya, int xb, int yb)
        : type(t), layer(l), spacing(s), x1(xa), y1(ya), x2(xb), y2(yb) {
    }
};
// PIN ЩYБПЅY?
struct PinInfo {
    std::string name;
    std::string netName;
    std::string direction;
    std::string use;
    std::string layer;
    int layerX1, layerY1, layerX2, layerY2;
    int placedX, placedY;
    std::string orient;
    std::string accessDirection;
};

// NET ЩYБПЅY?
struct NetPin {
    std::string instance;
    std::string pin;
};

struct NetInfo {
    std::string name;
    std::string use;
    std::vector<NetPin> connections;
};

// Flip-Flop ПакPЅY?
struct FlipFlopInfo {
    std::string instName;
    std::string cellType;
    int x, y;
    std::string orient;
    std::string orientation;
    std::string clockNet;
    std::vector<std::string> dataPins; // D pins
    std::vector<std::string> outputPins; // Q pins  
    std::string scanIn = "";     // SI pin net (Из№ыУР)
    std::string scanOut = "";    // SO pin net (Из№ыУР)
    std::string dataIn;         // D pin connection
    std::string dataOut;
    std::string scanEnable;
    bool isMultiBit = false;
    int bitWidth = 1;

    FlipFlopInfo() : x(0), y(0) {}
};

// Scan chain structure for DEF parsing
struct ScanChain {
    std::string name;
    std::vector<std::string> ffNames; // FF instance names in scan chain order
};

struct ScanChainNode {
    std::string instanceName;   // FF instance name
    std::string cellType;       // FF cell type

    // Constructor
    ScanChainNode(const std::string& inst, const std::string& type)
        : instanceName(inst), cellType(type) {
    }
};

// This is the ScanChain used in HierarchicalClustering
struct ScanChainClustered {
    std::vector<ScanChainNode> nodes;
    std::string chainId;

    // Helper methods
    size_t length() const { return nodes.size(); }
    bool isEmpty() const { return nodes.empty(); }

    // Add a node to the chain
    void addNode(const std::string& instName, const std::string& cellType) {
        nodes.emplace_back(instName, cellType);
    }
};

// Banking/Debanking єтЯxИєЅM
struct FFCluster {
    std::vector<std::string> ffInstances; // FF instance names
    std::string clockNet;
    int totalBits;
    double estimatedPower;
    double estimatedArea;
    double timingImpact;
    bool canBank = true;    // КЗ·сїЙТФЯMРР banking
};

// іЙ±ѕ?ЛгЅY?
struct CostMetrics {
    double tns = 0.0;       // Total Negative Slack
    double totalPower = 0.0;
    double totalArea = 0.0;
    double totalCost = 0.0;
};

// DEF ™n°ёЩYБПЅyХы
struct DefData {
    std::vector<RowInfo> rows;
    std::vector<TrackInfo> tracks;
    std::vector<ComponentInfo> components;
    std::vector<PinInfo> pins;
    std::vector<NetInfo> nets;
    std::vector<InstPinNet> instPinNets;
    std::vector<FlipFlopInfo> flipFlops;    // FF ЊЈУГЩYБП
    std::vector<ScanChain> scanChains;      // Scan chains from DEF
    std::vector<DefBlockageInfo> blockages; // 添加 BLOCKAGES
    std::map<std::string, std::vector<std::string>> clockDomains; // clock -> FF instances
    int units = 1000;      // №wі]1000Ў]micronsЎ^
    DieArea dieArea;       // ґ№¤щ°П°м
    std::vector<std::string> originalDefLines;
    std::vector<std::string> defHeaderLines;
    bool defHeaderLinesComplete = false;
};

// SDC ЦёБоЅY?
struct SdcCommand {
    std::string command_type;
    std::map<std::string, std::string> parameters;
    std::string raw_line;
};

// Technology File ЅY?
struct Technology {
    std::map<std::string, std::string> parameters;
};

struct Color {
    int id;
    std::map<std::string, std::string> parameters;
};

struct ClusteringStatistics {
    int totalFlipFlops;
    int totalClockDomains;
    int totalScanChains;
    std::map<std::string, int> ffPerClockDomain;
    std::map<std::string, int> chainsPerClockDomain;
    std::map<std::string, std::vector<int>> chainLengthsPerDomain;

    // Constructor
    ClusteringStatistics() : totalFlipFlops(0), totalClockDomains(0), totalScanChains(0) {}
};

// Banking candidate structure (for future use)
struct BankingCandidate {
    std::vector<std::string> flipFlops;  // List of FFs to be banked together
    std::string targetMBFF;               // Target multi-bit FF type
    double costReduction;                 // Estimated cost reduction

    BankingCandidate() : costReduction(0.0) {}
};

struct Layer {
    std::string name;
    std::map<std::string, std::string> parameters;
};

struct ContactCode {
    std::string name;
    std::map<std::string, std::string> parameters;
};

struct DesignRule {
    std::map<std::string, std::string> parameters;
};

struct PRRule {
    std::map<std::string, std::string> parameters;
};

struct DensityRule {
    std::map<std::string, std::string> parameters;
};

struct LayerDataType {
    std::string name;
    std::map<std::string, std::string> parameters;
};

struct TechData {
    Technology tech;
    std::vector<Color> colors;
    std::vector<Layer> layers;
    std::vector<ContactCode> contacts;
    std::vector<DesignRule> designRules;
    std::vector<PRRule> prRules;
    std::vector<DensityRule> densityRules;
    std::vector<LayerDataType> layerDataTypes;
};

// LEF ПакPЩYБПЅY?
struct LefSiteInfo {
    std::string name;
    std::string siteClass;
    std::vector<std::string> symmetry;
    double width = 0.0;
    double height = 0.0;
};

struct LefPinPortInfo {
    std::string layer;
    std::vector<std::pair<double, double>> points; // for POLYGON
    double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0; // for RECT
    bool isPolygon = false;
};

struct LefPinInfo {
    std::string name;
    std::string direction;
    std::string use;
    std::vector<LefPinPortInfo> ports;
    double antennadiffarea = 0.0;
};

struct LefObstructionInfo {
    std::string layer;
    std::vector<std::pair<double, double>> points; // for POLYGON
    double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0; // for RECT
    bool isPolygon = false;
};

struct LefMacroInfo {
    std::string name;
    std::string macroClass;
    double originX = 0.0;
    double originY = 0.0;
    double sizeX = 0.0;
    double sizeY = 0.0;
    std::vector<std::string> symmetry;
    std::string site;
    std::vector<LefPinInfo> pins;
    std::vector<LefObstructionInfo> obstructions;
    bool isAntennaCell = false;
};

struct LefData {
    double version = 0.0;
    std::string busBitChars;
    double units = 0.0;
    double manufacturingGrid = 0.0;
    std::vector<LefSiteInfo> sites;
    std::vector<LefMacroInfo> macros;
};


struct MergedFF {
    std::string newInstanceName;        // 新元件名稱
    std::string mbffType;               // 使用的 MBFF 型號（如 2_xxx, 4_xxx）

    std::vector<std::string> mergedFFs; // 被合併的 SBFF instance 名稱
    int newX = 0;                       // 新的 X 座標
    int newY = 0;                       // 新的 Y 座標
    int bitwidth;                       // 合併後的位元寬度
    float power = 0.0f;                 // 合併後元件的功耗
    float area = 0.0f;                  // 合併後元件的面積
    std::string orientation = "N";

};

struct PlacedComponent {
    std::string instanceName;
    std::string cellType;
    int x = 0;
    int y = 0;
    std::string orientation;
    int width = 0;
    int height = 0;
    bool isMergedFF = false;
    bool isFF = false;

    // 新增：放置後實際佔據的右上角
    int xEnd = 0;
    int yEnd = 0;

    PlacedComponent() = default;

    PlacedComponent(const std::string& name,
        const std::string& type,
        int xPos, int yPos,
        const std::string& orient,
        int w, int h,
        bool merged, bool isFF)
        : instanceName(name), cellType(type),
        x(xPos), y(yPos), orientation(orient),
        width(w), height(h), isMergedFF(merged), isFF(isFF) {

        // 自動計算右上角：根據方向
        if (orientation == "N" || orientation == "S" ||
            orientation == "FN" || orientation == "FS") {
            xEnd = x + width;
            yEnd = y + height;
        }
        else if (orientation == "E" || orientation == "W" ||
            orientation == "FE" || orientation == "FW") {
            xEnd = x + height;
            yEnd = y + width;
        }
        else {
            // 預設情況（如無效方向）
            xEnd = x + width;
            yEnd = y + height;
        }
    }
};





struct MergeMapping {
    // 單一 bit FF 名稱 → 合併後多 bit FF 名稱
    std::unordered_map<std::string, std::string> singleToMultiBitName;

    // 多 bit FF 名稱 → 該 FF 所包含的所有 single-bit FF 名稱
    std::unordered_map<std::string, std::vector<std::string>> multiBitToSingles;
    void addMapping(const std::string&, const std::string&);
    bool isMerged(const std::string&) const;
    std::string getMergedName(const std::string&) const;
    std::vector<std::string> getSingleBits(const std::string&) const;
    void removeMapping(const std::string&);
    void clear();
    // 印出所有映射結果
    void printMappings() const;
    std::vector<std::pair<int, std::string>> getBitIndexedPairs(const std::string& mbffName) const;
    int getBitIndex(const std::string& mbff, const std::string& singleName) const {
        auto it = multiBitToSingles.find(mbff);
        if (it == multiBitToSingles.end()) return -1;
        const auto& vec = it->second;
        for (size_t i = 0; i < vec.size(); ++i) {
            if (vec[i] == singleName) return static_cast<int>(i);
        }
        return -1;
    }

};



struct NewFlipFlopInfo {
    std::string instName;
    std::string cellType;
    int x = 0, y = 0;
    std::string orient;
    std::string orientation;
    std::string clockNet;
    std::vector<std::string> dataPins;    // 可省略
    std::vector<std::string> outputPins;  // 可省略
    std::string scanIn;
    std::string scanOut;
    std::string dataIn;
    std::string dataOut;
    std::string scanEnable;
    bool isMultiBit = false;
    int bitWidth = 1;
    int width = 0;
    int height = 0;
};

// 每個 site 的資訊：是否被佔用、誰佔用
struct LegalizerSite {
    bool occupied = false;
    std::string instanceName;  // 被誰佔用（optional，可省略）
};



// 合併後的 MBFF 結構
struct MBFFInstance {
    std::string mbffCellType;                 // 合併後的 cell type (2bit/4bit MBFF名)
    std::vector<std::string> mergedFFs;       // 原來的 instance names
    double x = 0, y = 0;                      // 放置的質心
    int bitWidth;                             // 2 or 4 (或 1, 單顆)
    double width = 0, height = 0;             // MBFF cell 長寬（由 lib 取得，方便placement）
    double cellLeakagePower = 0;              // MBFF cell leakage（由 lib 取得，方便功耗統計）
    double totalOrigArea = 0;                 // 合併前原本的總面積
    double totalOrigLeakage = 0;              // 合併前原本的總leakage
    std::string newInstanceName;
    std::string orientation = "N";            // <--- 新增，預設為 "N"
    std::map<std::string, std::string> mbffPinToOrigPin; // 例如 D[0] -> foo1__100/D
    std::map<std::string, std::string> mbffPinToOrigFF;  // 例如 D[0] -> foo1__100
};
#endif // DATASTRUCTURES_H