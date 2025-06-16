#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <regex>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <memory>
#include <stack>
#include <stdexcept>
#include <utility>
#include <algorithm>
#include <iomanip>

using namespace std;

// === 基礎資料結構 ===

// 權重參數結構
struct Weights {
    double Alpha = 0.0;
    double Beta = 0.0;
    double Gamma = 0.0;
    double TNS = 0.0;
    double TPO = 0.0;
    double Area = 0.0;
};

// Instance-Pin-Net 映射結構
struct InstPinNet {
    string inst;   // instance name
    string pin;    // pin name (D / Q / CK / …)
    string net;    // net name
};

// ROW 資料結構
struct RowInfo {
    string name;
    int x, y;
    string orientation;
    int count, by, stepX, stepY;
};

// TRACK 資料結構
struct TrackInfo {
    char direction;
    int start;
    int count;
    int step;
    string layer;
};

// COMPONENT 資料結構
struct ComponentInfo {
    string name;
    string cellType;
    int x, y;
    string orient;
};

// PIN 資料結構
struct PinInfo {
    string name;
    string netName;
    string direction;
    string use;
    string layer;
    int layerX1, layerY1, layerX2, layerY2;
    int placedX, placedY;
    string orient;
    string accessDirection;
};

// NET 資料結構
struct NetPin {
    string instance;
    string pin;
};

struct NetInfo {
    string name;
    string use;
    vector<NetPin> connections;
};

// Flip-Flop 相關結構
struct FlipFlopInfo {
    string instName;
    string cellType;
    int x, y;
    string orient;
    string clockNet;        // 時脈網路
    vector<string> dataPins; // D pins
    vector<string> outputPins; // Q pins  
    string scanIn = "";     // SI pin net (如果有)
    string scanOut = "";    // SO pin net (如果有)
    bool isMultiBit = false; // 是否為 multibit FF
    int bitWidth = 1;       // bit 寬度
};

// Banking/Debanking 候選群組
struct FFCluster {
    vector<string> ffInstances; // FF instance names
    string clockNet;
    int totalBits;
    double estimatedPower;
    double estimatedArea;
    double timingImpact;
    bool canBank = true;    // 是否可以進行 banking
};

// 成本計算結構
struct CostMetrics {
    double tns = 0.0;       // Total Negative Slack
    double totalPower = 0.0;
    double totalArea = 0.0;
    double totalCost = 0.0;
};

// DEF 檔案資料統整 (更新)
struct DefData {
    vector<RowInfo> rows;
    vector<TrackInfo> tracks;
    vector<ComponentInfo> components;
    vector<PinInfo> pins;
    vector<NetInfo> nets;
    vector<InstPinNet> instPinNets;
    vector<FlipFlopInfo> flipFlops;    // 新增：FF 專用資料
    map<string, vector<string>> clockDomains; // clock -> FF instances
};

// SDC 指令結構
struct SdcCommand {
    string command_type;
    map<string, string> parameters;
    string raw_line;
};

// Technology File 結構
struct Technology {
    map<string, string> parameters;
};

struct Color {
    int id;
    map<string, string> parameters;
};

struct Layer {
    string name;
    map<string, string> parameters;
};

struct ContactCode {
    string name;
    map<string, string> parameters;
};

struct DesignRule {
    map<string, string> parameters;
};

struct PRRule {
    map<string, string> parameters;
};

struct DensityRule {
    map<string, string> parameters;
};

struct LayerDataType {
    string name;
    map<string, string> parameters;
};

struct TechData {
    Technology tech;
    vector<Color> colors;
    vector<Layer> layers;
    vector<ContactCode> contacts;
    vector<DesignRule> designRules;
    vector<PRRule> prRules;
    vector<DensityRule> densityRules;
    vector<LayerDataType> layerDataTypes;
};

// === 新版 LEF 解析器 (來自第二個文件) ===

// Geometry helpers
struct Rect { double x0, y0, x1, y1; };
struct Poly { vector<pair<double, double>> pts; };
struct LayerDef {
    string name;
    vector<Rect> rects;
    vector<Poly> polys;
    explicit LayerDef(string n) :name(move(n)) {}
};

// Forward declarations
struct Statement;
using StmtUPtr = unique_ptr<Statement>;
enum class ParseRes { kContinue, kDone, kError, kChild };

// Base class
struct Statement {
    virtual ~Statement() = default;
    virtual ParseRes parseNext(const vector<string>&, Statement*& child) = 0;
};

// Forward actual blocks
struct LefMacro; struct LefPin; struct LefPort; struct LefObs; struct LefLayer; struct LefVia;

// Port / Obs (same behaviour)
struct LefPort : Statement {
    vector<unique_ptr<LayerDef>> layers;
    ParseRes parseNext(const vector<string>& tk, Statement*& child) override {
        child = nullptr;
        if (tk.empty()) return ParseRes::kContinue;
        if (tk[0] == "END") return ParseRes::kDone;
        if (tk[0] == "LAYER" && tk.size() >= 2) layers.emplace_back(make_unique<LayerDef>(tk[1]));
        else if (tk[0] == "RECT" && layers.size())
            layers.back()->rects.push_back({ stod(tk[1]),stod(tk[2]),stod(tk[3]),stod(tk[4]) });
        else if (tk[0] == "POLYGON" && layers.size()) {
            Poly poly; for (size_t i = 1;i + 1 < tk.size();i += 2) poly.pts.emplace_back(stod(tk[i]), stod(tk[i + 1]));
            layers.back()->polys.push_back(move(poly));
        }
        return ParseRes::kContinue;
    }
};
struct LefObs : LefPort {};  // identical logic

// Pin
struct LefPin : Statement {
    string name;
    unordered_map<string, string> info;
    unique_ptr<LefPort> port;
    explicit LefPin(string n) :name(move(n)) {}
    ParseRes parseNext(const vector<string>& tk, Statement*& child) override {
        child = nullptr;
        if (tk.empty()) return ParseRes::kContinue;
        if (tk[0] == "DIRECTION" && tk.size() >= 2) info["DIRECTION"] = tk[1];
        else if (tk[0] == "USE" && tk.size() >= 2)  info["USE"] = tk[1];
        else if (tk[0] == "ANTENNADIFFAREA" && tk.size() >= 2) info["ANTENNADIFFAREA"] = tk[1];
        else if (tk[0] == "PORT") { port = make_unique<LefPort>(); child = port.get(); return ParseRes::kChild; }
        else if (tk[0] == "END" && tk.size() == 2 && tk[1] == name) return ParseRes::kDone;
        return ParseRes::kContinue;
    }
};

// Macro
struct LefMacro : Statement {
    string name;
    unordered_map<string, string> info;
    pair<double, double> size{ 0,0 };
    pair<double, double> origin{ 0,0 };
    vector<unique_ptr<LefPin>> pins;
    unique_ptr<LefObs> obs;
    bool isAntennaCell = false;

    explicit LefMacro(string n) :name(move(n)) {}
    ParseRes parseNext(const vector<string>& tk, Statement*& child) override {
        child = nullptr;
        if (tk.empty()) return ParseRes::kContinue;
        const string& kw = tk[0];
        if (kw == "CLASS" && tk.size() >= 2) info["CLASS"] = tk[1];
        else if (kw == "ORIGIN" && tk.size() >= 3) origin = { stod(tk[1]), stod(tk[2]) };
        else if (kw == "SIZE" && tk.size() >= 4) size = { stod(tk[1]),stod(tk[3]) };
        else if (kw == "SYMMETRY" && tk.size() >= 2) info["SYMMETRY"] = tk[1];
        else if (kw == "SITE" && tk.size() >= 2) info["SITE"] = tk[1];
        else if (kw == "PIN" && tk.size() >= 2) { pins.emplace_back(make_unique<LefPin>(tk[1])); child = pins.back().get(); return ParseRes::kChild; }
        else if (kw == "OBS") { obs = make_unique<LefObs>(); child = obs.get(); return ParseRes::kChild; }
        else if (kw == "END" && tk.size() == 2 && tk[1] == name) return ParseRes::kDone;
        return ParseRes::kContinue;
    }
};

// Layer block
struct LefLayer : Statement {
    string name;
    unordered_map<string, string> info;
    explicit LefLayer(string n) :name(move(n)) {}
    ParseRes parseNext(const vector<string>& tk, Statement*& child) override {
        child = nullptr;
        if (tk.empty()) return ParseRes::kContinue;
        if (tk[0] == "TYPE" && tk.size() >= 2) info["TYPE"] = tk[1];
        else if (tk[0] == "DIRECTION" && tk.size() >= 2) info["DIRECTION"] = tk[1];
        else if (tk[0] == "PITCH" && tk.size() >= 2) info["PITCH"] = tk[1];
        else if (tk[0] == "WIDTH" && tk.size() >= 2) info["WIDTH"] = tk[1];
        else if (tk[0] == "SPACING" && tk.size() >= 2) info["SPACING"] = tk[1];
        else if (tk[0] == "END" && tk.size() == 2 && tk[1] == name) return ParseRes::kDone;
        return ParseRes::kContinue;
    }
};

// Via block
struct LefVia : Statement {
    string name;
    vector<unique_ptr<LayerDef>> layers;
    explicit LefVia(string n) :name(move(n)) {}
    ParseRes parseNext(const vector<string>& tk, Statement*& child) override {
        child = nullptr;
        if (tk.empty()) return ParseRes::kContinue;
        if (tk[0] == "END") return ParseRes::kDone;
        if (tk[0] == "LAYER" && tk.size() >= 2) layers.emplace_back(make_unique<LayerDef>(tk[1]));
        else if (tk[0] == "RECT" && layers.size()) layers.back()->rects.push_back({ stod(tk[1]),stod(tk[2]),stod(tk[3]),stod(tk[4]) });
        return ParseRes::kContinue;
    }
};

// Site definition
struct LefSite : Statement {
    string name;
    unordered_map<string, string> info;
    pair<double, double> size{ 0,0 };

    explicit LefSite(string n) :name(move(n)) {}
    ParseRes parseNext(const vector<string>& tk, Statement*& child) override {
        child = nullptr;
        if (tk.empty()) return ParseRes::kContinue;
        if (tk[0] == "CLASS" && tk.size() >= 2) info["CLASS"] = tk[1];
        else if (tk[0] == "SYMMETRY" && tk.size() >= 2) info["SYMMETRY"] = tk[1];
        else if (tk[0] == "SIZE" && tk.size() >= 4) size = { stod(tk[1]), stod(tk[3]) };
        else if (tk[0] == "END" && tk.size() == 2 && tk[1] == name) return ParseRes::kDone;
        return ParseRes::kContinue;
    }
};

// util – split a line into tokens
static vector<string> tokenize(const string& line)
{
    vector<string> tok;
    string word;
    istringstream iss(line);
    while (iss >> word) {
        if (!word.empty() && word.back() == ';') word.pop_back();
        tok.push_back(word);
    }
    return tok;
}

// LEF Parser core
class LefParser {
public:
    unordered_map<string, LefMacro*> macroDict;
    unordered_map<string, LefLayer*> layerDict;
    unordered_map<string, LefVia*> viaDict;
    unordered_map<string, LefSite*> siteDict;

    double version = 0.0;
    string busBitChars;
    double units = 0.0;
    double manufacturingGrid = 0.0;

    void parse(const string& path) {
        ifstream fin(path);
        if (!fin) throw runtime_error("cannot open " + path);
        string line;
        vector<Statement*> stack;            // raw‑ptr stack (non‑owning)

        while (getline(fin, line)) {
            auto tk = tokenize(line);
            if (tk.empty()) continue;

            // Global settings (outside any block)
            if (stack.empty()) {
                if (tk[0] == "VERSION" && tk.size() >= 2) {
                    version = stod(tk[1]);
                    continue;
                }
                else if (tk[0] == "BUSBITCHARS" && tk.size() >= 2) {
                    busBitChars = tk[1];
                    if (busBitChars.front() == '"' && busBitChars.back() == '"') {
                        busBitChars = busBitChars.substr(1, busBitChars.size() - 2);
                    }
                    continue;
                }
                else if (tk[0] == "UNITS" && tk.size() >= 5 && tk[1] == "DATABASE" && tk[2] == "MICRONS") {
                    units = stod(tk[3]);
                    continue;
                }
                else if (tk[0] == "MANUFACTURINGGRID" && tk.size() >= 2) {
                    manufacturingGrid = stod(tk[1]);
                    continue;
                }

                // top‑level block detection
                StmtUPtr obj;
                if (tk[0] == "MACRO" && tk.size() >= 2) {
                    auto macro = make_unique<LefMacro>(tk[1]);
                    // Check if ANTENNACELL in the same line
                    for (const auto& token : tk) {
                        if (token == "ANTENNACELL") {
                            macro->isAntennaCell = true;
                            break;
                        }
                    }
                    obj = move(macro);
                }
                else if (tk[0] == "LAYER" && tk.size() >= 2) obj = make_unique<LefLayer>(tk[1]);
                else if (tk[0] == "VIA" && tk.size() >= 2)   obj = make_unique<LefVia>(tk[1]);
                else if (tk[0] == "SITE" && tk.size() >= 2)  obj = make_unique<LefSite>(tk[1]);

                if (!obj) continue;                       // skip other lines
                Statement* raw = obj.get();
                allObjs.emplace_back(move(obj));    // take ownership
                stack.push_back(raw);
                continue;                                // read next line
            }

            // inside a block
            Statement* cur = stack.back();
            Statement* child = nullptr;
            ParseRes res = cur->parseNext(tk, child);

            if (res == ParseRes::kChild && child) {
                stack.push_back(child);                  // push observational ptr
            }
            else if (res == ParseRes::kDone) {
                // pop finished block
                Statement* finished = stack.back();
                stack.pop_back();
                if (auto m = dynamic_cast<LefMacro*>(finished)) macroDict[m->name] = m;
                else if (auto l = dynamic_cast<LefLayer*>(finished)) layerDict[l->name] = l;
                else if (auto v = dynamic_cast<LefVia*>(finished))   viaDict[v->name] = v;
                else if (auto s = dynamic_cast<LefSite*>(finished))  siteDict[s->name] = s;
            }
        }
    }

private:
    vector<StmtUPtr> allObjs;   // own ALL top‑level objects; children owned hierarchically
};

// 將新版 LEF 資料轉換為舊版格式的轉換函數
struct LefData {
    double version = 0.0;
    string busBitChars;
    double units = 0.0;
    double manufacturingGrid = 0.0;
    vector<struct LefSiteInfo> sites;
    vector<struct LefMacroInfo> macros;
};

struct LefSiteInfo {
    string name;
    string siteClass;
    string symmetry;
    double width = 0.0;
    double height = 0.0;
};

struct LefPinPortInfo {
    string layer;
    vector<pair<double, double>> points; // for POLYGON
    double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0; // for RECT
    bool isPolygon = false;
};

struct LefPinInfo {
    string name;
    string direction;
    string use;
    vector<LefPinPortInfo> ports;
    double antennadiffarea = 0.0;
};

struct LefObstructionInfo {
    string layer;
    vector<pair<double, double>> points; // for POLYGON
    double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0; // for RECT
    bool isPolygon = false;
};

struct LefMacroInfo {
    string name;
    string macroClass;
    double originX = 0.0;
    double originY = 0.0;
    double sizeX = 0.0;
    double sizeY = 0.0;
    string symmetry;
    string site;
    vector<LefPinInfo> pins;
    vector<LefObstructionInfo> obstructions;
    bool isAntennaCell = false;
};

// 轉換函數 (C++17 compatible)
LefData convertLefParserToLefData(const LefParser& parser) {
    LefData lefData;
    lefData.version = parser.version;
    lefData.busBitChars = parser.busBitChars;
    lefData.units = parser.units;
    lefData.manufacturingGrid = parser.manufacturingGrid;

    // 轉換 Sites (C++17 compatible iteration)
    for (const auto& sitePair : parser.siteDict) {
        const string& name = sitePair.first;
        const LefSite* site = sitePair.second;

        LefSiteInfo siteInfo;
        siteInfo.name = site->name;

        auto classIt = site->info.find("CLASS");
        siteInfo.siteClass = (classIt != site->info.end()) ? classIt->second : "";

        auto symmetryIt = site->info.find("SYMMETRY");
        siteInfo.symmetry = (symmetryIt != site->info.end()) ? symmetryIt->second : "";

        siteInfo.width = site->size.first;
        siteInfo.height = site->size.second;
        lefData.sites.push_back(siteInfo);
    }

    // 轉換 Macros (C++17 compatible iteration)
    for (const auto& macroPair : parser.macroDict) {
        const string& name = macroPair.first;
        const LefMacro* macro = macroPair.second;

        LefMacroInfo macroInfo;
        macroInfo.name = macro->name;

        auto classIt = macro->info.find("CLASS");
        macroInfo.macroClass = (classIt != macro->info.end()) ? classIt->second : "";

        macroInfo.originX = macro->origin.first;
        macroInfo.originY = macro->origin.second;
        macroInfo.sizeX = macro->size.first;
        macroInfo.sizeY = macro->size.second;

        auto symmetryIt = macro->info.find("SYMMETRY");
        macroInfo.symmetry = (symmetryIt != macro->info.end()) ? symmetryIt->second : "";

        auto siteIt = macro->info.find("SITE");
        macroInfo.site = (siteIt != macro->info.end()) ? siteIt->second : "";

        macroInfo.isAntennaCell = macro->isAntennaCell;

        // 轉換 Pins
        for (const auto& pin : macro->pins) {
            LefPinInfo pinInfo;
            pinInfo.name = pin->name;

            auto directionIt = pin->info.find("DIRECTION");
            pinInfo.direction = (directionIt != pin->info.end()) ? directionIt->second : "";

            auto useIt = pin->info.find("USE");
            pinInfo.use = (useIt != pin->info.end()) ? useIt->second : "";

            auto antennaIt = pin->info.find("ANTENNADIFFAREA");
            if (antennaIt != pin->info.end()) {
                pinInfo.antennadiffarea = stod(antennaIt->second);
            }

            // 轉換 Ports
            if (pin->port) {
                for (const auto& layer : pin->port->layers) {
                    for (const auto& rect : layer->rects) {
                        LefPinPortInfo portInfo;
                        portInfo.layer = layer->name;
                        portInfo.x1 = rect.x0;
                        portInfo.y1 = rect.y0;
                        portInfo.x2 = rect.x1;
                        portInfo.y2 = rect.y1;
                        portInfo.isPolygon = false;
                        pinInfo.ports.push_back(portInfo);
                    }
                    for (const auto& poly : layer->polys) {
                        LefPinPortInfo portInfo;
                        portInfo.layer = layer->name;
                        portInfo.points = poly.pts;
                        portInfo.isPolygon = true;
                        pinInfo.ports.push_back(portInfo);
                    }
                }
            }
            macroInfo.pins.push_back(pinInfo);
        }

        // 轉換 Obstructions
        if (macro->obs) {
            for (const auto& layer : macro->obs->layers) {
                for (const auto& rect : layer->rects) {
                    LefObstructionInfo obsInfo;
                    obsInfo.layer = layer->name;
                    obsInfo.x1 = rect.x0;
                    obsInfo.y1 = rect.y0;
                    obsInfo.x2 = rect.x1;
                    obsInfo.y2 = rect.y1;
                    obsInfo.isPolygon = false;
                    macroInfo.obstructions.push_back(obsInfo);
                }
                for (const auto& poly : layer->polys) {
                    LefObstructionInfo obsInfo;
                    obsInfo.layer = layer->name;
                    obsInfo.points = poly.pts;
                    obsInfo.isPolygon = true;
                    macroInfo.obstructions.push_back(obsInfo);
                }
            }
        }

        lefData.macros.push_back(macroInfo);
    }

    return lefData;
}

// === 新增：Flip-Flop 分析函數 ===

// 判斷是否為 flip-flop cell
bool isFlipFlopCell(const string& cellType) {
    // 根據競賽資料，FF cell 通常包含這些關鍵字
    return (cellType.find("FF") != string::npos ||
        cellType.find("DFF") != string::npos ||
        cellType.find("SDFF") != string::npos ||
        cellType.find("LATCH") != string::npos ||
        cellType.find("_FF_") != string::npos ||
        cellType.find("FLIP") != string::npos);
}

// 從 cell type 判斷 bit width
int getBitWidth(const string& cellType) {
    // 尋找數字來判斷 bit width
    regex bitRx(R"((\d+)BIT|(\d+)B|_(\d+)_)");
    smatch m;
    if (regex_search(cellType, m, bitRx)) {
        for (int i = 1; i <= 3; i++) {
            if (m[i].matched) {
                return stoi(m[i]);
            }
        }
    }
    return 1; // 預設為 1-bit
}

// 分析並建立 flip-flop 資訊
void analyzeFlipFlops(DefData& defData) {
    cout << "\n=== Analyzing Flip-Flops ===" << endl;

    for (const auto& comp : defData.components) {
        if (isFlipFlopCell(comp.cellType)) {
            FlipFlopInfo ff;
            ff.instName = comp.name;
            ff.cellType = comp.cellType;
            ff.x = comp.x;
            ff.y = comp.y;
            ff.orient = comp.orient;
            ff.bitWidth = getBitWidth(comp.cellType);
            ff.isMultiBit = (ff.bitWidth > 1);

            // 找出這個 FF 的所有 pin connections
            for (const auto& ipn : defData.instPinNets) {
                if (ipn.inst == comp.name) {
                    if (ipn.pin == "CK" || ipn.pin == "CLK" || ipn.pin == "CP") {
                        ff.clockNet = ipn.net;
                    }
                    else if (ipn.pin.find("D") == 0) { // D, D0, D1, etc.
                        ff.dataPins.push_back(ipn.net);
                    }
                    else if (ipn.pin.find("Q") == 0) { // Q, Q0, Q1, etc.
                        ff.outputPins.push_back(ipn.net);
                    }
                    else if (ipn.pin == "SI") {
                        ff.scanIn = ipn.net;
                    }
                    else if (ipn.pin == "SO") {
                        ff.scanOut = ipn.net;
                    }
                }
            }

            defData.flipFlops.push_back(ff);
            defData.clockDomains[ff.clockNet].push_back(ff.instName);
        }
    }

    cout << "Found " << defData.flipFlops.size() << " flip-flops:" << endl;
    for (const auto& ff : defData.flipFlops) {
        cout << "  " << ff.instName << " (" << ff.cellType << ") "
            << ff.bitWidth << "-bit, clock=" << ff.clockNet << endl;
    }

    cout << "Clock domains:" << endl;
    for (const auto& domain : defData.clockDomains) {
        cout << "  " << domain.first << ": " << domain.second.size() << " FFs" << endl;
    }
}

// 解析 Verilog 檔案
bool parseVerilogFile(const string& vfile, DefData& defData) {
    ifstream in(vfile);
    if (!in) {
        cerr << "Error: cannot open Verilog file " << vfile << endl;
        return false;
    }

    string line, buf;
    /* 例：SNPSHOPT25_FSDNQ_V3_1  foo1__1  ( .D(q0) , .CK(clk) , .Q(q1) ); */
    static const regex instRx(
        R"(^\s*([A-Za-z0-9_\\]+)\s+([A-Za-z0-9_\\]+)\s*\((.*)\)\s*;)",
        regex::ECMAScript | regex::optimize);
    static const regex pinRx(
        R"(\.\s*([A-Za-z0-9_]+)\s*\(\s*([^)]+?)\s*\))",
        regex::ECMAScript | regex::optimize);

    while (getline(in, line)) {
        if (line.find('(') == string::npos) continue; // 跳過不是 instance 的行
        buf += line + ' ';
        if (line.find(");") == string::npos) continue; // 尚未結束，續行

        smatch m;
        if (regex_search(buf, m, instRx)) {
            string cell = m[1];
            string inst = m[2];
            string pinBlock = m[3];

            /* 若 components 內還沒有這顆 instance，可先加一筆佔位
               (x,y,orient 之後由 DEF 補) */
            bool exist = false;
            for (auto& c : defData.components) {
                if (c.name == inst) {
                    c.cellType = cell;  // 更新 cell type
                    exist = true;
                    break;
                }
            }
            if (!exist) {
                ComponentInfo dummy{ inst, cell, 0, 0, "" };
                defData.components.push_back(dummy);
            }

            /* 拆 pin list */
            auto it = sregex_iterator(pinBlock.begin(), pinBlock.end(), pinRx);
            for (; it != sregex_iterator(); ++it) {
                string pin = (*it)[1];
                string net = (*it)[2];
                defData.instPinNets.push_back({ inst, pin, net });

                /* 把 net → inst/pin 也填進 defData.nets，方便查 */
                bool netExist = false;
                for (auto& n : defData.nets) {
                    if (n.name == net) {
                        netExist = true;
                        // 檢查是否已存在相同的連接
                        bool connExist = false;
                        for (const auto& conn : n.connections) {
                            if (conn.instance == inst && conn.pin == pin) {
                                connExist = true;
                                break;
                            }
                        }
                        if (!connExist) {
                            n.connections.push_back({ inst, pin });
                        }
                        break;
                    }
                }
                if (!netExist) {
                    NetInfo n;
                    n.name = net;
                    n.use = "SIGNAL";
                    n.connections.push_back({ inst, pin });
                    defData.nets.push_back(n);
                }
            }
        }
        buf.clear();
    }
    in.close();
    return true;
}

// 解析權重檔案
bool parseWeightFile(const string& filename, Weights& weights) {
    ifstream infile(filename);
    if (!infile.is_open()) {
        cerr << "Error: Cannot open weight file: " << filename << endl;
        return false;
    }

    string line;
    while (getline(infile, line)) {
        istringstream iss(line);
        string key;
        double value;
        if (!(iss >> key >> value)) {
            continue;
        }

        if (key == "Alpha") weights.Alpha = value;
        else if (key == "Beta") weights.Beta = value;
        else if (key == "Gamma") weights.Gamma = value;
        else if (key == "TNS") weights.TNS = value;
        else if (key == "TPO") weights.TPO = value;
        else if (key == "Area") weights.Area = value;
    }

    infile.close();
    return true;
}

// 解析 DEF 檔案
bool parseDefFile(const string& filename, DefData& defData) {
    ifstream in(filename);
    if (!in) {
        cerr << "Error: Cannot open DEF file: " << filename << endl;
        return false;
    }

    string line;

    // 各種 regex 模式
    regex version_rx(R"(^\s*VERSION\s+([\d.]+)\s*;)");
    regex divider_rx(R"(^\s*DIVIDERCHAR\s+"(.+)\"s*;)");
    regex busbits_rx(R"(^\s*BUSBITCHARS\s+"(.+)\"s*;)");
    regex design_rx(R"(^\s*DESIGN\s+(\w+)\s*;)");
    regex units_rx(R"(^\s*UNITS\s+DISTANCE\s+(\w+)\s+(\d+)\s*;)");
    regex diearea_rx(R"(^\s*DIEAREA\s*\(\s*(\d+)\s+(\d+)\s*\)\s*\(\s*(\d+)\s+(\d+)\s*\)\s*\(\s*(\d+)\s+(\d+)\s*\)\s*\(\s*(\d+)\s+(\d+)\s*\)\s*;)");
    regex row_rx(R"(^\s*ROW\s+(\w+)\s+\w+\s+(\d+)\s+(\d+)\s+([A-Z]{1,2})\s+DO\s+(\d+)\s+BY\s+(\d+)\s+STEP\s+(\d+)\s+(\d+)\s*;)");
    regex tracks_rx(R"(^\s*TRACKS\s+([XY])\s+(\d+)\s+DO\s+(\d+)\s+STEP\s+(\d+)\s+LAYER\s+(\w+)\s*;)");
    regex comp_rx(R"(^\s*-\s+(\S+)\s+(\S+)\s+\+\s+PLACED\s+\(\s*(\d+)\s+(\d+)\s*\)\s+(\S+)\s*;)");
    regex pin_start_rx(R"(^\s*-\s+(\S+)\s+\+\s+NET\s+(\S+)\s+\+\s+DIRECTION\s+(\w+)\s+\+\s+USE\s+(\w+))");
    regex pin_layer_rx(R"(^\s*\+\s+LAYER\s+(\S+)\s+\(\s*(\d+)\s+(\d+)\s*\)\s+\(\s*(\d+)\s+(\d+)\s*\))");
    regex pin_placed_rx(R"(^\s*\+\s+PLACED\s+\(\s*(\d+)\s+(\d+)\s*\)\s+(\S+)\s*;)");
    regex pinprop_rx(R"(^\s*-\s+PIN\s+(\S+))");
    regex prop_accessdir_rx(R"(^\s*\+\s+PROPERTY\s+ACCESS_DIRECTION\s+"([^"]+)\")");
    regex net_start_rx(R"(^\s*-\s+(\S+))");
    regex net_pin_rx(R"(^\s*\(\s*(\S+)\s+(\S+)\s*\))");
    regex net_use_rx(R"(^\s*\+\s+USE\s+(\S+)\s*;)");

    smatch m;

    while (getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;

        if (regex_search(line, m, row_rx)) {
            RowInfo r;
            r.name = m[1];
            r.x = stoi(m[2]);
            r.y = stoi(m[3]);
            r.orientation = m[4];
            r.count = stoi(m[5]);
            r.by = stoi(m[6]);
            r.stepX = stoi(m[7]);
            r.stepY = stoi(m[8]);
            defData.rows.push_back(r);
        }
        else if (regex_search(line, m, tracks_rx)) {
            TrackInfo t;
            t.direction = m[1].str()[0];
            t.start = stoi(m[2]);
            t.count = stoi(m[3]);
            t.step = stoi(m[4]);
            t.layer = m[5];
            defData.tracks.push_back(t);
        }
        else if (regex_search(line, m, comp_rx)) {
            ComponentInfo c;
            c.name = m[1];
            c.cellType = m[2];
            c.x = stoi(m[3]);
            c.y = stoi(m[4]);
            c.orient = m[5];

            // 更新現有 component 或新增
            bool found = false;
            for (auto& existing : defData.components) {
                if (existing.name == c.name) {
                    existing.x = c.x;
                    existing.y = c.y;
                    existing.orient = c.orient;
                    if (existing.cellType.empty()) {
                        existing.cellType = c.cellType;
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                defData.components.push_back(c);
            }
        }
        else if (regex_search(line, m, pin_start_rx)) {
            PinInfo p;
            p.name = m[1];
            p.netName = m[2];
            p.direction = m[3];
            p.use = m[4];

            getline(in, line);
            if (regex_search(line, m, pin_layer_rx)) {
                p.layer = m[1];
                p.layerX1 = stoi(m[2]);
                p.layerY1 = stoi(m[3]);
                p.layerX2 = stoi(m[4]);
                p.layerY2 = stoi(m[5]);
            }

            getline(in, line);
            if (regex_search(line, m, pin_placed_rx)) {
                p.placedX = stoi(m[1]);
                p.placedY = stoi(m[2]);
                p.orient = m[3];
            }

            defData.pins.push_back(p);
        }
        else if (regex_search(line, m, pinprop_rx)) {
            string pinName = m[1];
            for (auto& p : defData.pins) {
                if (p.name == pinName) {
                    getline(in, line);
                    if (regex_search(line, m, prop_accessdir_rx)) {
                        p.accessDirection = m[1];
                    }
                    break;
                }
            }
        }
        else if (regex_search(line, m, net_start_rx)) {
            NetInfo net;
            net.name = m[1];
            while (getline(in, line)) {
                if (regex_search(line, m, net_pin_rx)) {
                    NetPin pin;
                    pin.instance = m[1];
                    pin.pin = m[2];
                    net.connections.push_back(pin);
                }
                else if (regex_search(line, m, net_use_rx)) {
                    net.use = m[1];
                    break;
                }
            }
            defData.nets.push_back(net);
        }
    }

    in.close();
    return true;
}

// 解析 SDC 檔案
bool parseSdcFile(const string& filename, vector<SdcCommand>& sdcCommands) {
    ifstream sdc_file(filename);
    if (!sdc_file.is_open()) {
        cerr << "Error: Cannot open SDC file: " << filename << endl;
        return false;
    }

    string line;
    regex re_set(R"(^\s*set\s+(\S+)\s+(\S+))");
    regex re_set_units(R"(^\s*set_units\s+(.+))");
    regex re_create_clock(R"(^\s*create_clock\s+(.+))");
    regex re_set_load(R"(^\s*set_load\s+(.+))");
    regex re_set_clock_latency(R"(^\s*set_clock_latency\s+(\S+)\s+\[.+\])");
    regex re_set_clock_uncertainty(R"(^\s*set_clock_uncertainty\s+(\S+)\s+\[.+\])");
    regex re_set_clock_transition(R"(^\s*set_clock_transition\s+(\S+)\s+\[.+\])");
    regex re_set_input_delay(R"(^\s*set_input_delay\s+(.+))");
    regex re_set_output_delay(R"(^\s*set_output_delay\s+(.+))");
    regex re_set_max_transition(R"(^\s*set_max_transition\s+(\S+)\s+\[.+\])");
    regex re_set_max_capacitance(R"(^\s*set_max_capacitance\s+(\S+)\s+\[.+\])");

    while (getline(sdc_file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == '#') continue;

        smatch match;
        SdcCommand cmd;
        cmd.raw_line = line;

        if (regex_match(line, match, re_set)) {
            cmd.command_type = "set";
            cmd.parameters["name"] = match[1];
            cmd.parameters["value"] = match[2];
        }
        else if (regex_match(line, match, re_set_units)) {
            cmd.command_type = "set_units";
            regex re_option(R"(-(\S+)\s+(\S+))");
            string args = match[1];
            auto args_begin = sregex_iterator(args.begin(), args.end(), re_option);
            auto args_end = sregex_iterator();
            for (auto it = args_begin; it != args_end; ++it) {
                cmd.parameters[it->str(1)] = it->str(2);
            }
        }
        else if (regex_match(line, match, re_create_clock)) {
            cmd.command_type = "create_clock";
            cmd.parameters["raw_args"] = match[1];
        }
        else if (regex_match(line, match, re_set_load)) {
            cmd.command_type = "set_load";
            cmd.parameters["raw_args"] = match[1];
        }
        else if (regex_match(line, match, re_set_clock_latency)) {
            cmd.command_type = "set_clock_latency";
            cmd.parameters["value"] = match[1];
        }
        else if (regex_match(line, match, re_set_clock_uncertainty)) {
            cmd.command_type = "set_clock_uncertainty";
            cmd.parameters["value"] = match[1];
        }
        else if (regex_match(line, match, re_set_clock_transition)) {
            cmd.command_type = "set_clock_transition";
            cmd.parameters["value"] = match[1];
        }
        else if (regex_match(line, match, re_set_input_delay)) {
            cmd.command_type = "set_input_delay";
            cmd.parameters["raw_args"] = match[1];
        }
        else if (regex_match(line, match, re_set_output_delay)) {
            cmd.command_type = "set_output_delay";
            cmd.parameters["raw_args"] = match[1];
        }
        else if (regex_match(line, match, re_set_max_transition)) {
            cmd.command_type = "set_max_transition";
            cmd.parameters["value"] = match[1];
        }
        else if (regex_match(line, match, re_set_max_capacitance)) {
            cmd.command_type = "set_max_capacitance";
            cmd.parameters["value"] = match[1];
        }
        else {
            cmd.command_type = "unknown";
        }

        sdcCommands.push_back(cmd);
    }

    sdc_file.close();
    return true;
}

// 解析 Technology File
bool parseTechFile(const string& filename, TechData& techData) {
    ifstream infile(filename);
    if (!infile.is_open()) {
        cerr << "Error: Cannot open technology file: " << filename << endl;
        return false;
    }

    string line;
    regex re_section_start(R"((\w+)\s*(\"[^\"]+\"|\S+)?\s*\{)");
    regex re_key_value(R"((\w+)\s*=\s*(\(.+\)|\"?[^\"\n]+\"?))");
    smatch match;

    string current_section;
    Color current_color;
    Layer current_layer;
    ContactCode current_contact;
    DesignRule current_design_rule;
    PRRule current_pr_rule;
    DensityRule current_density_rule;
    LayerDataType current_layer_data_type;
    bool in_section = false;

    while (getline(infile, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == '#') continue;

        if (regex_search(line, match, re_section_start)) {
            current_section = match[1];

            if (current_section == "Technology") {
                in_section = true;
            }
            else if (current_section == "Color") {
                current_color = Color();
                current_color.id = stoi(match[2]);
                in_section = true;
            }
            else if (current_section == "Layer") {
                current_layer = Layer();
                string layer_name = match[2];
                if (!layer_name.empty() && layer_name.front() == '"') {
                    layer_name = layer_name.substr(1, layer_name.size() - 2);
                }
                current_layer.name = layer_name;
                in_section = true;
            }
            else if (current_section == "ContactCode") {
                current_contact = ContactCode();
                string contact_name = match[2];
                if (!contact_name.empty() && contact_name.front() == '"') {
                    contact_name = contact_name.substr(1, contact_name.size() - 2);
                }
                current_contact.name = contact_name;
                in_section = true;
            }
            else if (current_section == "DesignRule") {
                current_design_rule = DesignRule();
                in_section = true;
            }
            else if (current_section == "PRRule") {
                current_pr_rule = PRRule();
                in_section = true;
            }
            else if (current_section == "DensityRule") {
                current_density_rule = DensityRule();
                in_section = true;
            }
            else if (current_section == "LayerDataType") {
                current_layer_data_type = LayerDataType();
                string name = match[2];
                if (!name.empty() && name.front() == '"') {
                    name = name.substr(1, name.size() - 2);
                }
                current_layer_data_type.name = name;
                in_section = true;
            }
            continue;
        }

        if (line == "}") {
            if (current_section == "Color") {
                techData.colors.push_back(current_color);
            }
            else if (current_section == "Layer") {
                techData.layers.push_back(current_layer);
            }
            else if (current_section == "ContactCode") {
                techData.contacts.push_back(current_contact);
            }
            else if (current_section == "DesignRule") {
                techData.designRules.push_back(current_design_rule);
            }
            else if (current_section == "PRRule") {
                techData.prRules.push_back(current_pr_rule);
            }
            else if (current_section == "DensityRule") {
                techData.densityRules.push_back(current_density_rule);
            }
            else if (current_section == "LayerDataType") {
                techData.layerDataTypes.push_back(current_layer_data_type);
            }
            current_section.clear();
            in_section = false;
            continue;
        }

        if (in_section) {
            if (regex_search(line, match, re_key_value)) {
                string key = match[1];
                string value = match[2];

                if (!value.empty() && value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.size() - 2);
                }

                if (current_section == "Technology") {
                    techData.tech.parameters[key] = value;
                }
                else if (current_section == "Color") {
                    current_color.parameters[key] = value;
                }
                else if (current_section == "Layer") {
                    current_layer.parameters[key] = value;
                }
                else if (current_section == "ContactCode") {
                    current_contact.parameters[key] = value;
                }
                else if (current_section == "DesignRule") {
                    current_design_rule.parameters[key] = value;
                }
                else if (current_section == "PRRule") {
                    current_pr_rule.parameters[key] = value;
                }
                else if (current_section == "DensityRule") {
                    current_density_rule.parameters[key] = value;
                }
                else if (current_section == "LayerDataType") {
                    current_layer_data_type.parameters[key] = value;
                }
            }
        }
    }

    infile.close();
    return true;
}

// === 輸出函數 ===

void printWeights(const Weights& weights) {
    cout << "\n=== Weight Parameters ===" << endl;
    cout << "Alpha: " << weights.Alpha << endl;
    cout << "Beta: " << weights.Beta << endl;
    cout << "Gamma: " << weights.Gamma << endl;
    cout << "TNS: " << weights.TNS << endl;
    cout << "TPO: " << weights.TPO << endl;
    cout << "Area: " << weights.Area << endl;
}

void printDefData(const DefData& defData) {
    cout << "\n=== DEF File Data ===" << endl;

    cout << "\n共讀取到 " << defData.rows.size() << " 條 ROW 記錄：" << endl;
    for (const auto& r : defData.rows) {
        cout << "  " << r.name << " @(" << r.x << "," << r.y << ") "
            << r.orientation << " DO " << r.count
            << " BY " << r.by << " STEP " << r.stepX << "," << r.stepY << endl;
    }

    cout << "\n共讀取到 " << defData.tracks.size() << " 條 TRACKS 記錄：" << endl;
    for (const auto& t : defData.tracks) {
        cout << "  方向: " << t.direction << " 起始: " << t.start
            << " 條數: " << t.count << " 間距: " << t.step
            << " 層: " << t.layer << endl;
    }

    cout << "\n共讀取到 " << defData.components.size() << " 個 COMPONENT 記錄：" << endl;
    for (const auto& c : defData.components) {
        cout << "  " << c.name << " (" << c.cellType << ") "
            << "@(" << c.x << "," << c.y << ") "
            << "方向: " << c.orient << endl;
    }

    cout << "\n共讀取到 " << defData.pins.size() << " 個 PIN 記錄：" << endl;
    for (const auto& p : defData.pins) {
        cout << "  " << p.name
            << " (net: " << p.netName
            << ") dir: " << p.direction
            << " layer: " << p.layer
            << " placed @(" << p.placedX << "," << p.placedY
            << ") orient: " << p.orient;
        if (p.direction == "OUTPUT") {
            cout << " [OUTPUT]";
        }
        if (!p.accessDirection.empty()) {
            cout << " access: " << p.accessDirection;
        }
        cout << endl;
    }

    cout << "\n共讀取到 " << defData.nets.size() << " 個 NET 記錄：" << endl;
    for (const auto& net : defData.nets) {
        cout << "  " << net.name << " USE: " << net.use << endl;
        for (const auto& pin : net.connections) {
            cout << "    (" << pin.instance << ", " << pin.pin << ")" << endl;
        }
    }

    cout << "\n共讀取到 " << defData.instPinNets.size() << " 個 Instance-Pin-Net 映射：" << endl;
    for (const auto& ipn : defData.instPinNets) {
        cout << "  " << ipn.inst << "." << ipn.pin << " -> " << ipn.net << endl;
    }
}

void printLefData(const LefData& lefData) {
    cout << "\n=== LEF File Data ===" << endl;
    cout << "Version: " << lefData.version << endl;
    cout << "Bus Bit Characters: " << lefData.busBitChars << endl;
    cout << "Units: " << lefData.units << endl;
    cout << "Manufacturing Grid: " << lefData.manufacturingGrid << endl;

    cout << "\n=== Sites ===" << endl;
    for (const auto& site : lefData.sites) {
        cout << "Site: " << site.name << endl;
        cout << "  Class: " << site.siteClass << endl;
        cout << "  Symmetry: " << site.symmetry << endl;
        cout << "  Size: " << site.width << " x " << site.height << endl;
    }

    cout << "\n=== Macros ===" << endl;
    for (const auto& macro : lefData.macros) {
        cout << "Macro: " << macro.name;
        if (macro.isAntennaCell) cout << " [ANTENNA CELL]";
        cout << endl;
        cout << "  Class: " << macro.macroClass << endl;
        cout << "  Origin: (" << macro.originX << ", " << macro.originY << ")" << endl;
        cout << "  Size: " << macro.sizeX << " x " << macro.sizeY << endl;
        cout << "  Symmetry: " << macro.symmetry << endl;
        cout << "  Site: " << macro.site << endl;

        cout << "  Pins:" << endl;
        for (const auto& pin : macro.pins) {
            cout << "    " << pin.name << " (" << pin.direction << ", " << pin.use << ")" << endl;
            if (pin.antennadiffarea > 0) {
                cout << "      Antenna Diff Area: " << pin.antennadiffarea << endl;
            }
            for (const auto& port : pin.ports) {
                cout << "      Port on " << port.layer;
                if (port.isPolygon) {
                    cout << " (POLYGON)" << endl;
                }
                else {
                    cout << " (RECT " << port.x1 << " " << port.y1
                        << " " << port.x2 << " " << port.y2 << ")" << endl;
                }
            }
        }

        if (!macro.obstructions.empty()) {
            cout << "  Obstructions: " << macro.obstructions.size() << " layers" << endl;
        }
    }
}

void printSdcData(const vector<SdcCommand>& sdcCommands) {
    cout << "\n=== SDC Commands ===" << endl;
    for (const auto& cmd : sdcCommands) {
        cout << "Command Type: " << cmd.command_type << endl;
        cout << "  Raw Line: " << cmd.raw_line << endl;
        for (const auto& pair : cmd.parameters) {
            cout << "    " << pair.first << " : " << pair.second << endl;
        }
    }
}

void printTechData(const TechData& techData) {
    cout << "\n=== Technology File Data ===" << endl;

    cout << "\n=== Technology ===" << endl;
    for (const auto& pair : techData.tech.parameters) {
        cout << pair.first << " = " << pair.second << endl;
    }

    cout << "\n=== Colors ===" << endl;
    for (const auto& color : techData.colors) {
        cout << "Color " << color.id << endl;
        for (const auto& pair : color.parameters) {
            cout << "  " << pair.first << " = " << pair.second << endl;
        }
    }

    cout << "\n=== Layers ===" << endl;
    for (const auto& layer : techData.layers) {
        cout << "Layer " << layer.name << endl;
        for (const auto& pair : layer.parameters) {
            cout << "  " << pair.first << " = " << pair.second << endl;
        }
    }

    cout << "\n=== ContactCodes ===" << endl;
    for (const auto& contact : techData.contacts) {
        cout << "ContactCode " << contact.name << endl;
        for (const auto& pair : contact.parameters) {
            cout << "  " << pair.first << " = " << pair.second << endl;
        }
    }
}

// 輸出結果到檔案
bool writeOutputFiles(const string& outputName, const Weights& weights,
    const DefData& defData, const vector<SdcCommand>& sdcCommands) {
    // 輸出 mapping 檔案 (.txt)
    ofstream mapFile(outputName + ".txt");
    if (!mapFile.is_open()) {
        cerr << "Error: Cannot create mapping file " << outputName << ".txt" << endl;
        return false;
    }

    // 計算 flip-flop 數量 (簡化版本，實際需要識別 flip-flop cells)
    int ffCount = 0;
    for (const auto& comp : defData.components) {
        if (comp.cellType.find("FF") != string::npos ||
            comp.cellType.find("DFF") != string::npos ||
            comp.cellType.find("SDFF") != string::npos) {
            ffCount++;
        }
    }

    mapFile << "CellInst " << ffCount << endl;

    // 輸出 pin mapping (目前為 1:1 mapping，實際需要實現 banking/debanking 演算法)
    for (const auto& ipn : defData.instPinNets) {
        mapFile << ipn.inst << "/" << ipn.pin << " map " << ipn.inst << "/" << ipn.pin << endl;
    }
    mapFile.close();

    // 輸出 DEF 檔案 (.def)
    ofstream defFile(outputName + ".def");
    if (!defFile.is_open()) {
        cerr << "Error: Cannot create DEF file " << outputName << ".def" << endl;
        return false;
    }

    defFile << "VERSION 5.8 ;" << endl;
    defFile << "DESIGN " << outputName << " ;" << endl;
    defFile << "UNITS DISTANCE MICRONS 1000 ;" << endl;

    // 輸出 DIEAREA (簡化版本)
    defFile << "DIEAREA ( 0 0 ) ( 1000 1000 ) ;" << endl;

    // 輸出 ROWS
    if (!defData.rows.empty()) {
        defFile << "ROW " << defData.rows.size() << " ;" << endl;
        for (const auto& row : defData.rows) {
            defFile << "ROW " << row.name << " core " << row.x << " " << row.y
                << " " << row.orientation << " DO " << row.count
                << " BY " << row.by << " STEP " << row.stepX << " " << row.stepY << " ;" << endl;
        }
    }

    // 輸出 TRACKS
    if (!defData.tracks.empty()) {
        defFile << "TRACKS " << defData.tracks.size() << " ;" << endl;
        for (const auto& track : defData.tracks) {
            defFile << "TRACKS " << track.direction << " " << track.start
                << " DO " << track.count << " STEP " << track.step
                << " LAYER " << track.layer << " ;" << endl;
        }
    }

    // 輸出 COMPONENTS
    defFile << "COMPONENTS " << defData.components.size() << " ;" << endl;
    for (const auto& comp : defData.components) {
        defFile << "- " << comp.name << " " << comp.cellType
            << " + PLACED ( " << comp.x << " " << comp.y << " ) " << comp.orient << " ;" << endl;
    }
    defFile << "END COMPONENTS" << endl;

    // 輸出 PINS
    if (!defData.pins.empty()) {
        defFile << "PINS " << defData.pins.size() << " ;" << endl;
        for (const auto& pin : defData.pins) {
            defFile << "- " << pin.name << " + NET " << pin.netName
                << " + DIRECTION " << pin.direction << " + USE " << pin.use << endl;
            defFile << "  + LAYER " << pin.layer << " ( " << pin.layerX1 << " " << pin.layerY1
                << " ) ( " << pin.layerX2 << " " << pin.layerY2 << " )" << endl;
            defFile << "  + PLACED ( " << pin.placedX << " " << pin.placedY << " ) " << pin.orient << " ;" << endl;
        }
        defFile << "END PINS" << endl;
    }

    // 輸出 NETS
    if (!defData.nets.empty()) {
        defFile << "NETS " << defData.nets.size() << " ;" << endl;
        for (const auto& net : defData.nets) {
            defFile << "- " << net.name;
            for (const auto& conn : net.connections) {
                defFile << " ( " << conn.instance << " " << conn.pin << " )";
            }
            defFile << " + USE " << net.use << " ;" << endl;
        }
        defFile << "END NETS" << endl;
    }

    defFile << "END DESIGN" << endl;
    defFile.close();

    // 輸出 Verilog 檔案 (.v)
    ofstream vFile(outputName + ".v");
    if (!vFile.is_open()) {
        cerr << "Error: Cannot create Verilog file " << outputName << ".v" << endl;
        return false;
    }

    vFile << "module " << outputName << " (" << endl;

    // 輸出 port declarations (簡化版本)
    bool first = true;
    for (const auto& pin : defData.pins) {
        if (!first) vFile << ",";
        vFile << endl << "    " << pin.name;
        first = false;
    }
    vFile << endl << ");" << endl << endl;

    // 輸出 port directions
    for (const auto& pin : defData.pins) {
        string direction = (pin.direction == "INPUT") ? "input" :
            (pin.direction == "OUTPUT") ? "output" : "inout";
        vFile << direction << " " << pin.name << ";" << endl;
    }
    vFile << endl;

    // 輸出 wire declarations
    set<string> wires;
    for (const auto& net : defData.nets) {
        bool isPort = false;
        for (const auto& pin : defData.pins) {
            if (pin.netName == net.name) {
                isPort = true;
                break;
            }
        }
        if (!isPort) {
            wires.insert(net.name);
        }
    }

    for (const auto& wire : wires) {
        vFile << "wire " << wire << ";" << endl;
    }
    vFile << endl;

    // 輸出 component instances
    for (const auto& comp : defData.components) {
        vFile << comp.cellType << " " << comp.name << " (";

        // 找出這個 component 的所有 pin connections
        vector<string> pinConnections;
        for (const auto& ipn : defData.instPinNets) {
            if (ipn.inst == comp.name) {
                pinConnections.push_back("." + ipn.pin + "(" + ipn.net + ")");
            }
        }

        for (size_t i = 0; i < pinConnections.size(); ++i) {
            if (i > 0) vFile << ", ";
            vFile << pinConnections[i];
        }
        vFile << ");" << endl;
    }

    vFile << endl << "endmodule" << endl;
    vFile.close();

    return true;
}

// === 主程式 ===
int main(int argc, char* argv[]) {
    // 解析所有輸入檔案
    Weights weights;
    DefData defData;
    vector<SdcCommand> sdcCommands;
    TechData techData;
    LefParser lefParser;  // 使用新版 LEF Parser

    // 根據競賽要求的檔案格式處理
    string baseName = "testcase1";
    string outputName = "output";

    // 如果有命令列參數，使用參數指定的檔案
    if (argc >= 2) {
        // 從第一個參數推斷基礎檔名
        string firstFile = argv[1];
        size_t pos = firstFile.find("_weight");
        if (pos != string::npos) {
            baseName = firstFile.substr(0, pos);
        }
        else {
            pos = firstFile.find(".def");
            if (pos != string::npos) {
                baseName = firstFile.substr(0, pos);
            }
            else {
                pos = firstFile.find(".sdc");
                if (pos != string::npos) {
                    baseName = firstFile.substr(0, pos);
                }
                else {
                    pos = firstFile.find(".tf");
                    if (pos != string::npos) {
                        baseName = firstFile.substr(0, pos);
                    }
                    else {
                        pos = firstFile.find(".v");
                        if (pos != string::npos) {
                            baseName = firstFile.substr(0, pos);
                        }
                        else {
                            pos = firstFile.find(".lef");
                            if (pos != string::npos) {
                                baseName = firstFile.substr(0, pos);
                            }
                        }
                    }
                }
            }
        }

        // 最後一個參數作為輸出檔名
        if (argc >= 3) {
            outputName = argv[argc - 1];
        }
    }

    // 構建檔案名稱
    string weightFile = baseName + "_weight";
    string defFile = baseName + ".def";
    string sdcFile = baseName + ".sdc";
    string techFile = baseName + ".tf";
    string verilogFile = baseName + ".v";
    string lefFile = baseName + ".lef";

    cout << "=== ICCAD Contest Problem B - File Parser ===" << endl;
    cout << "Base name: " << baseName << endl;
    cout << "Processing files..." << endl;

    // 解析權重檔案
    if (!parseWeightFile(weightFile, weights)) {
        cerr << "Failed to parse weight file: " << weightFile << endl;
        return 1;
    }
    cout << "✓ Weight file (" << weightFile << ") parsed successfully" << endl;

    // 解析 LEF 檔案 (使用新版 parser)
    try {
        lefParser.parse(lefFile);
        cout << "✓ LEF file (" << lefFile << ") parsed successfully" << endl;
        cout << "  - MACRO count: " << lefParser.macroDict.size() << endl;
        cout << "  - LAYER count: " << lefParser.layerDict.size() << endl;
        cout << "  - VIA count: " << lefParser.viaDict.size() << endl;
        cout << "  - SITE count: " << lefParser.siteDict.size() << endl;
    }
    catch (const exception& e) {
        cerr << "Warning: Failed to parse LEF file: " << lefFile << " - " << e.what() << endl;
    }

    // 解析 DEF 檔案
    if (!parseDefFile(defFile, defData)) {
        cerr << "Failed to parse DEF file: " << defFile << endl;
        return 1;
    }
    cout << "✓ DEF file (" << defFile << ") parsed successfully" << endl;

    // 解析 Verilog 檔案
    if (!parseVerilogFile(verilogFile, defData)) {
        cerr << "Warning: Failed to parse Verilog file: " << verilogFile << endl;
    }
    else {
        cout << "✓ Verilog file (" << verilogFile << ") parsed successfully" << endl;
    }

    // 解析 SDC 檔案
    if (!parseSdcFile(sdcFile, sdcCommands)) {
        cerr << "Warning: Failed to parse SDC file: " << sdcFile << endl;
    }
    else {
        cout << "✓ SDC file (" << sdcFile << ") parsed successfully" << endl;
    }

    // 解析 Technology 檔案
    cout << "Parsing Technology file..." << endl;
    if (!parseTechFile(techFile, techData)) {
        cerr << "Warning: Failed to parse Technology file: " << techFile << endl;
    }
    else {
        cout << "✓ Technology file (" << techFile << ") parsed successfully" << endl;
    }

    // 分析 Flip-Flops
    cout << "Analyzing Flip-Flops..." << endl;
    try {
        analyzeFlipFlops(defData);
        cout << "✓ Flip-Flop analysis completed" << endl;
    }
    catch (const exception& e) {
        cerr << "Error in Flip-Flop analysis: " << e.what() << endl;
    }

    // 輸出解析結果
    cout << "Printing results..." << endl;
    try {
        printWeights(weights);
        cout << "✓ Weight data printed" << endl;
    }
    catch (const exception& e) {
        cerr << "Error printing weights: " << e.what() << endl;
    }

    // 轉換並輸出 LEF 資料 (使用轉換函數)
    cout << "Converting LEF data..." << endl;
    try {
        if (!lefParser.macroDict.empty() || !lefParser.siteDict.empty()) {
            LefData lefData = convertLefParserToLefData(lefParser);
            cout << "✓ LEF data converted successfully" << endl;
            printLefData(lefData);
            cout << "✓ LEF data printed" << endl;
        }
        else {
            cout << "No LEF data to convert" << endl;
        }
    }
    catch (const exception& e) {
        cerr << "Error in LEF conversion: " << e.what() << endl;
    }

    cout << "Printing DEF data..." << endl;
    try {
        printDefData(defData);
        cout << "✓ DEF data printed" << endl;
    }
    catch (const exception& e) {
        cerr << "Error printing DEF data: " << e.what() << endl;
    }

    cout << "Processing SDC and Tech data..." << endl;
    try {
        if (!sdcCommands.empty()) {
            printSdcData(sdcCommands);
            cout << "✓ SDC data printed" << endl;
        }
        else {
            cout << "No SDC commands to print" << endl;
        }

        if (!techData.layers.empty()) {
            printTechData(techData);
            cout << "✓ Technology data printed" << endl;
        }
        else {
            cout << "No Technology data to print" << endl;
        }
    }
    catch (const exception& e) {
        cerr << "Error processing SDC/Tech data: " << e.what() << endl;
    }

    // 計算統計資訊
    cout << "Calculating statistics..." << endl;
    try {
        int totalElements = defData.rows.size() + defData.tracks.size() +
            defData.components.size() + defData.pins.size() +
            defData.nets.size() + defData.instPinNets.size();

        cout << "\n=== Summary ===" << endl;
        cout << "Total elements processed: " << totalElements << endl;
        cout << "Components: " << defData.components.size() << endl;
        cout << "Instance-Pin-Net mappings: " << defData.instPinNets.size() << endl;
        cout << "LEF Sites: " << lefParser.siteDict.size() << endl;
        cout << "LEF Macros: " << lefParser.macroDict.size() << endl;
        cout << "Output file prefix: " << outputName << endl;
        cout << "✓ Statistics calculated" << endl;
    }
    catch (const exception& e) {
        cerr << "Error calculating statistics: " << e.what() << endl;
    }

    // 元件統計
    cout << "Analyzing cell types..." << endl;
    try {
        struct CellCnt {
            long long total = 0;
            long long fsdn = 0;   // FSDN  (Q+QN)
            long long fsdnq = 0;   // FSDNQ (Q-only)
            long long an2 = 0;   // 2-input AND
            long long or2 = 0;   // 2-input OR
            long long buf = 0;   // Buffer / BUF_ECO
            long long inv = 0;   // Inverter
        } cnt;

        for (const auto& comp : defData.components) {
            ++cnt.total;
            const string& t = comp.cellType;

            // Flip-flops
            if (t.find("FSDNQ") != string::npos)
                ++cnt.fsdnq;
            else if (t.find("FSDN") != string::npos)
                ++cnt.fsdn;

            // Logic gates / buffers
            if (t.find("AN2") != string::npos)
                ++cnt.an2;
            else if (t.find("OR2") != string::npos)
                ++cnt.or2;
            else if (t.find("BUF") != string::npos)       // covers BUF_ECO_*
                ++cnt.buf;
            else if (t.find("INV") != string::npos)
                ++cnt.inv;
        }

        // 印結果
        cout << "\n=== Cell-Type Summary ===" << endl;
        cout << "Total components : " << cnt.total << endl;
        cout << "  Flip-flops      : " << (cnt.fsdn + cnt.fsdnq) << endl;
        cout << "    ├─ FSDN       : " << cnt.fsdn << endl;
        cout << "    └─ FSDNQ      : " << cnt.fsdnq << endl;
        cout << "  AN2 (2-i AND)   : " << cnt.an2 << endl;
        cout << "  OR2 (2-i OR)    : " << cnt.or2 << endl;
        cout << "  BUF / BUF_ECO   : " << cnt.buf << endl;
        cout << "  INV             : " << cnt.inv << endl;
        cout << "✓ Cell type analysis completed" << endl;
    }
    catch (const exception& e) {
        cerr << "Error in cell type analysis: " << e.what() << endl;
    }

    // 新版 LEF Parser 的詳細統計
    cout << "Generating LEF Parser details..." << endl;
    try {
        cout << "\n=== New LEF Parser Details ===" << endl;
        cout << "表頭" << endl;
        cout << "Idx  " << left
            << setw(30) << "Name"
            << setw(10) << "Class"
            << setw(14) << "Size(um)"
            << setw(6) << "#Pin"
            << "First 3 pins" << endl
            << string(80, '-') << endl;

        // 列出前 10 個巨集
        int idx = 0;
        for (auto it = lefParser.macroDict.begin();
            it != lefParser.macroDict.end() && idx < 10; ++it, ++idx)
        {
            const LefMacro* m = it->second;
            const string& name = m->name;

            // CLASS
            string cls;
            auto clsIt = m->info.find("CLASS");
            if (clsIt != m->info.end()) cls = clsIt->second;

            // Size
            ostringstream oss;
            oss << fixed << setprecision(3)
                << m->size.first << 'x' << m->size.second;

            // First 3 pins
            string pinStr;
            for (size_t i = 0; i < m->pins.size() && i < 3; ++i) {
                const LefPin* p = m->pins[i].get();

                auto dirIt = p->info.find("DIRECTION");
                const string dir = (dirIt != p->info.end()) ? dirIt->second : "";

                auto useIt = p->info.find("USE");
                const string use = (useIt != p->info.end()) ? useIt->second : "";

                if (i) pinStr += ", ";
                pinStr += p->name + "(" + dir + "/" + use + ")";
            }

            // 輸出一列
            cout << right << setw(3) << (idx + 1) << "  "
                << left << setw(30) << name
                << setw(10) << cls
                << setw(14) << oss.str()
                << setw(6) << m->pins.size()
                << pinStr << endl;
        }
        cout << "✓ LEF Parser details generated" << endl;
    }
    catch (const exception& e) {
        cerr << "Error generating LEF details: " << e.what() << endl;
    }

    // 輸出結果檔案
    cout << "Writing output files..." << endl;
    try {
        if (!writeOutputFiles(outputName, weights, defData, sdcCommands)) {
            cerr << "Failed to write output files!" << endl;
            return 1;
        }
        cout << "\n✓ Output files generated successfully:" << endl;
        cout << "  - " << outputName << ".txt (pin mapping)" << endl;
        cout << "  - " << outputName << ".def (DEF format)" << endl;
        cout << "  - " << outputName << ".v (Verilog netlist)" << endl;
    }
    catch (const exception& e) {
        cerr << "Error writing output files: " << e.what() << endl;
        return 1;
    }

    // TODO: 這裡可以加入 multibit flip-flop banking/debanking 的演算法
    // 目前只是基本的 1:1 mapping，實際競賽需要實現優化演算法

    cout << "\nParsing completed successfully!" << endl;

    return 0;
}