#ifndef PARSER_DEF_H
#define PARSER_DEF_H

#include "DataStructures.h"
#include "LibParser.h"

#include <string>
#include <vector>
#include <regex>
#include <memory>
#include <fstream>
#include <map>

class DefParser {
private:
    DefData defData_;
    bool isLoaded_;
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;
    std::map<std::string, std::vector<ComponentInfo*>> rowToComponentsMap_;
    std::map<std::string, int> rowComponentCount_;
    const LibParser* lib_ = nullptr;
    // Regex patterns
    std::regex rowRegex_;
    std::regex trackRegex_;
    std::regex componentRegex_;
    std::regex pinStartRegex_;
    std::regex pinLayerRegex_;
    std::regex pinPlacedRegex_;
    std::regex netStartRegex_;
    std::regex netPinRegex_;
    std::regex netUseRegex_;
    std::regex dieAreaRegex_;
    std::regex unitsRegex_;
    std::regex scanChainLineRegex_;  // New regex for scan chains
    std::regex blockageStartRegex_;
    std::regex blockagePlacementRegex_;
    std::regex blockageLayerRegex_;
    std::regex blockageRectRegex_;
    // Helper methods
    bool parseRowInfo(const std::string& line);
    bool parseTrackInfo(const std::string& line);
    bool parseComponentInfo(const std::string& line);
    bool parsePinInfo(std::ifstream& file, const std::string& line);
    bool parseNetInfo(std::ifstream& file, const std::string& line);
    bool parseScanChainLine(const std::string& line);  // New method for parsing scan chains
    void addError(const std::string& error);
    void addWarning(const std::string& warning);
    bool parseBlockageInfo(std::ifstream& file, const std::string& line);
    bool isFlipFlopCell(const std::string& cellType);
    int declaredComponentsCount_ = -1;

    // （新增）用於抓 "COMPONENTS <N> ;" 的 regex
    std::regex componentsHeaderRegex_ = std::regex(R"(^\s*COMPONENTS\s+(\d+)\s*;)");
public:
    // Constructor & Destructor
    DefParser();
    ~DefParser() = default;
    const DefData& getDefData() const { return defData_; }
    DefData& getDefData() { return defData_; }  // Non-const for modificati
    // Copy/Move constructors
    DefParser(const DefParser&) = delete;
    DefParser& operator=(const DefParser&) = delete;
    DefParser(DefParser&&) = default;
    DefParser& operator=(DefParser&&) = default;
    void setLibParser(const LibParser* p) { lib_ = p; }
    // Main interface methods
    bool parseFile(const std::string& filename);
    bool parseFromString(const std::string& content);

    // Data access methods
    void setDefData(const DefData& newData) {
        this->defData_ = newData; // 安砞ず场 DefData Θ跑计暗 data_
    }
    bool isLoaded() const { return isLoaded_; }
    int getDeclaredComponentsCount() const {
        return (declaredComponentsCount_ > 0) ? declaredComponentsCount_
            : static_cast<int>(defData_.components.size());
    }
    // Component access methods
    const std::vector<RowInfo>& getRows() const { return defData_.rows; }
    const std::vector<TrackInfo>& getTracks() const { return defData_.tracks; }
    const std::vector<ComponentInfo>& getComponents() const { return defData_.components; }
    const std::vector<PinInfo>& getPins() const { return defData_.pins; }
    const std::vector<NetInfo>& getNets() const { return defData_.nets; }
    const std::vector<InstPinNet>& getInstPinNets() const { return defData_.instPinNets; }
    const std::vector<FlipFlopInfo>& getFlipFlops() const { return defData_.flipFlops; }
    const std::vector<ScanChain>& getScanChains() const { return defData_.scanChains; }  // New getter

    // Statistics methods
    size_t getRowCount() const { return defData_.rows.size(); }
    size_t getTrackCount() const { return defData_.tracks.size(); }
    size_t getComponentCount() const { return defData_.components.size(); }
    size_t getPinCount() const { return defData_.pins.size(); }
    size_t getNetCount() const { return defData_.nets.size(); }
    size_t getInstPinNetCount() const { return defData_.instPinNets.size(); }
    size_t getFlipFlopCount() const { return defData_.flipFlops.size(); }
    size_t getScanChainCount() const { return defData_.scanChains.size(); }  // New getter

    // Analysis methods
    void analyzeFlipFlops();
    void updateComponentFromVerilog(const std::vector<InstPinNet>& verilogData);
    ComponentInfo* findComponent(const std::string& name);
    NetInfo* findNet(const std::string& name);

    // Scan chain related methods
    void printScanChains() const;  // Print all scan chains
    std::pair<int, int> findFFinScanChains(const std::string& ffName) const;  // Find FF in scan chains

    // Utility methods
    void clear();
    void printSummary() const;
    void printDefData() const;
    bool validateData() const;

    // Error handling
    const std::vector<std::string>& getErrors() const { return errors_; }
    const std::vector<std::string>& getWarnings() const { return warnings_; }
    bool hasErrors() const { return !errors_.empty(); }
    bool hasWarnings() const { return !warnings_.empty(); }

    // Export methods
    bool writeDefFile(const std::string& filename) const;
    std::string toString() const;

    void assignComponentsToRows();
    void computeRowDimensions();
    void printComponentsByRow() const; // optional
    void clearFlipFlops() { defData_.flipFlops.clear(); }
    void addFlipFlop(const FlipFlopInfo& ff) { defData_.flipFlops.push_back(ff); }
    const std::vector<DefBlockageInfo>& getBlockages() const { return defData_.blockages; }
    size_t getBlockageCount() const { return defData_.blockages.size(); }
};

// Utility functions for DEF parsing
namespace DefUtils {

    int getBitWidth(const std::string& cellType);
    std::string extractOrientation(const std::string& orientStr);
    bool validateCoordinate(int x, int y);

    // Additional functions
    std::string getCellCategory(const std::string& cellType);
    bool isClockSignal(const std::string& netName);
    bool isScanSignal(const std::string& pinName);
}

#endif // PARSER_DEF_H