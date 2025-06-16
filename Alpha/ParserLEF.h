#ifndef PARSER_LEF_H
#define PARSER_LEF_H

#include "DataStructures.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <stack>

// Forward declarations
struct Statement;
using StmtUPtr = std::unique_ptr<Statement>;
enum class ParseRes { kContinue, kDone, kError, kChild };

// Geometry helpers
struct Rect {
    double x0, y0, x1, y1;
};

struct Poly {
    std::vector<std::pair<double, double>> pts;
};

struct LayerDef {
    std::string name;
    std::vector<Rect> rects;
    std::vector<Poly> polys;
    explicit LayerDef(std::string n) : name(std::move(n)) {}
};

// Base Statement class
struct Statement {
    virtual ~Statement() = default;
    virtual ParseRes parseNext(const std::vector<std::string>&, Statement*& child) = 0;
};

// LEF Statement implementations
struct LefPort : Statement {
    std::vector<std::unique_ptr<LayerDef>> layers;
    ParseRes parseNext(const std::vector<std::string>& tk, Statement*& child) override;
};

struct LefObs : LefPort {};  // identical logic

struct LefPin : Statement {
    std::string name;
    std::unordered_map<std::string, std::string> info;
    std::unique_ptr<LefPort> port;
    explicit LefPin(std::string n) : name(std::move(n)) {}
    ParseRes parseNext(const std::vector<std::string>& tk, Statement*& child) override;
};

struct LefMacro : Statement {
    std::string name;
    std::unordered_map<std::string, std::string> info;
    std::pair<double, double> size{ 0,0 };
    std::pair<double, double> origin{ 0,0 };
    std::vector<std::unique_ptr<LefPin>> pins;
    std::unique_ptr<LefObs> obs;
    bool isAntennaCell = false;

    explicit LefMacro(std::string n) : name(std::move(n)) {}
    ParseRes parseNext(const std::vector<std::string>& tk, Statement*& child) override;
};

struct LefLayer : Statement {
    std::string name;
    std::unordered_map<std::string, std::string> info;
    explicit LefLayer(std::string n) : name(std::move(n)) {}
    ParseRes parseNext(const std::vector<std::string>& tk, Statement*& child) override;
};

struct LefVia : Statement {
    std::string name;
    std::vector<std::unique_ptr<LayerDef>> layers;
    explicit LefVia(std::string n) : name(std::move(n)) {}
    ParseRes parseNext(const std::vector<std::string>& tk, Statement*& child) override;
};

struct LefSite : Statement {
    std::string name;
    std::unordered_map<std::string, std::string> info;
    std::pair<double, double> size{ 0,0 };

    explicit LefSite(std::string n) : name(std::move(n)) {}
    ParseRes parseNext(const std::vector<std::string>& tk, Statement*& child) override;
};

// LEF Parser Class
class LefParser {
private:
    std::vector<StmtUPtr> allObjs;   // own ALL top‑level objects
    bool isLoaded_;  // 添加載入狀態追蹤

    // Helper methods
    std::vector<std::string> tokenize(const std::string& line);
    LefData convertToLefData() const;

public:
    std::unordered_map<std::string, LefMacro*> macroDict;
    std::unordered_map<std::string, LefLayer*> layerDict;
    std::unordered_map<std::string, LefVia*> viaDict;
    std::unordered_map<std::string, LefSite*> siteDict;

    double version = 0.0;
    std::string busBitChars;
    double units = 0.0;
    double manufacturingGrid = 0.0;

    // Constructor & Destructor
    LefParser() : isLoaded_(false) {}
    ~LefParser() = default;

    // Main interface methods
    bool parseFile(const std::string& filename);
    LefData getData() const;
    bool isLoaded() const { return isLoaded_; }  // 添加 isLoaded 方法

    // Information methods
    size_t getMacroCount() const { return macroDict.size(); }
    size_t getLayerCount() const { return layerDict.size(); }
    size_t getViaCount() const { return viaDict.size(); }
    size_t getSiteCount() const { return siteDict.size(); }

    // Debug and display methods
    void printSummary() const;
    void printMacroDetails(int maxCount = 10) const;

    // Clear data
    void clear();
};

#endif // PARSER_LEF_H