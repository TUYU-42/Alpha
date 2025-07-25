#ifndef DATASTRUCTURES_H
#define DATASTRUCTURES_H

#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>

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
    std::map<std::string, std::vector<std::string>> clockDomains; // clock -> FF instances
    int units = 1000;      // №wі]1000Ў]micronsЎ^
    DieArea dieArea;       // ґ№¤щ°П°м
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
    std::string symmetry;
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
    std::string symmetry;
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

#endif // DATASTRUCTURES_H