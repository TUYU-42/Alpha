#ifndef DATASTRUCTURES_H
#define DATASTRUCTURES_H

#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>

// === 基礎資料結? ===

// 權重參數結?
struct Weights {
    double Alpha = 0.0;
    double Beta = 0.0;
    double Gamma = 0.0;
    double TNS = 0.0;
    double TPO = 0.0;
    double Area = 0.0;
};

// Instance-Pin-Net 映射結?
struct InstPinNet {
    std::string inst;   // instance name
    std::string pin;    // pin name (D / Q / CK / …)
    std::string net;    // net name
};

// ROW 資料結?
struct RowInfo {
    std::string name;
    int x, y;
    std::string orientation;
    int count, by, stepX, stepY;
    // ┑ノㄓゑ癸ノ range
    int xEnd= x + count * stepX; 
    int yEnd= y + by * stepY;  // 璝 by > 1
    int rowXWidth = 0;
    int rowYWidth = 0;
    
};

// TRACK 資料結?
struct TrackInfo {
    char direction;
    int start;
    int count;
    int step;
    std::string layer;
};

// COMPONENT 資料結?
struct ComponentInfo {
    std::string name;
    std::string cellType;
    int x, y;
    std::string orient;
    std::string rowName;
};

// PIN 資料結?
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

// NET 資料結?
struct NetPin {
    std::string instance;
    std::string pin;
};

struct NetInfo {
    std::string name;
    std::string use;
    std::vector<NetPin> connections;
};

// Flip-Flop 相關結?
struct FlipFlopInfo {
    std::string instName;
    std::string cellType;
    int x, y;
    std::string orient;
    std::string clockNet;        // 時脈網路
    std::vector<std::string> dataPins; // D pins
    std::vector<std::string> outputPins; // Q pins  
    std::string scanIn = "";     // SI pin net (如果有)
    std::string scanOut = "";    // SO pin net (如果有)
    bool isMultiBit = false; // 是否為 multibit FF
    int bitWidth = 1;       // bit ?度
};

// Banking/Debanking 候選群組
struct FFCluster {
    std::vector<std::string> ffInstances; // FF instance names
    std::string clockNet;
    int totalBits;
    double estimatedPower;
    double estimatedArea;
    double timingImpact;
    bool canBank = true;    // 是否可以進行 banking
};

// 成本?算結?
struct CostMetrics {
    double tns = 0.0;       // Total Negative Slack
    double totalPower = 0.0;
    double totalArea = 0.0;
    double totalCost = 0.0;
};

// DEF 檔案資料統整
struct DefData {
    std::vector<RowInfo> rows;
    std::vector<TrackInfo> tracks;
    std::vector<ComponentInfo> components;
    std::vector<PinInfo> pins;
    std::vector<NetInfo> nets;
    std::vector<InstPinNet> instPinNets;
    std::vector<FlipFlopInfo> flipFlops;    // FF 專用資料
    std::map<std::string, std::vector<std::string>> clockDomains; // clock -> FF instances
};

// SDC 指令結?
struct SdcCommand {
    std::string command_type;
    std::map<std::string, std::string> parameters;
    std::string raw_line;
};

// Technology File 結?
struct Technology {
    std::map<std::string, std::string> parameters;
};

struct Color {
    int id;
    std::map<std::string, std::string> parameters;
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

// LEF 相關資料結?
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