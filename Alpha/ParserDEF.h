#ifndef PARSER_DEF_H
#define PARSER_DEF_H

#include "DataStructures.h"
#include <string>
#include <vector>
#include <regex>
#include <memory>
#include <fstream>

class DefParser {
private:
    DefData defData_;
    bool isLoaded_;
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;

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

    // Helper methods
    bool parseRowInfo(const std::string& line);
    bool parseTrackInfo(const std::string& line);
    bool parseComponentInfo(const std::string& line);
    bool parsePinInfo(std::ifstream& file, const std::string& line);
    bool parseNetInfo(std::ifstream& file, const std::string& line);
    void addError(const std::string& error);
    void addWarning(const std::string& warning);

public:
    // Constructor & Destructor
    DefParser();
    ~DefParser() = default;

    // Copy/Move constructors
    DefParser(const DefParser&) = delete;
    DefParser& operator=(const DefParser&) = delete;
    DefParser(DefParser&&) = default;
    DefParser& operator=(DefParser&&) = default;

    // Main interface methods
    bool parseFile(const std::string& filename);
    bool parseFromString(const std::string& content);

    // Data access methods
    const DefData& getDefData() const { return defData_; }
    DefData& getDefData() { return defData_; }
    bool isLoaded() const { return isLoaded_; }

    // Component access methods
    const std::vector<RowInfo>& getRows() const { return defData_.rows; }
    const std::vector<TrackInfo>& getTracks() const { return defData_.tracks; }
    const std::vector<ComponentInfo>& getComponents() const { return defData_.components; }
    const std::vector<PinInfo>& getPins() const { return defData_.pins; }
    const std::vector<NetInfo>& getNets() const { return defData_.nets; }
    const std::vector<InstPinNet>& getInstPinNets() const { return defData_.instPinNets; }
    const std::vector<FlipFlopInfo>& getFlipFlops() const { return defData_.flipFlops; }

    // Statistics methods
    size_t getRowCount() const { return defData_.rows.size(); }
    size_t getTrackCount() const { return defData_.tracks.size(); }
    size_t getComponentCount() const { return defData_.components.size(); }
    size_t getPinCount() const { return defData_.pins.size(); }
    size_t getNetCount() const { return defData_.nets.size(); }
    size_t getInstPinNetCount() const { return defData_.instPinNets.size(); }
    size_t getFlipFlopCount() const { return defData_.flipFlops.size(); }

    // Analysis methods
    void analyzeFlipFlops();
    void updateComponentFromVerilog(const std::vector<InstPinNet>& verilogData);
    ComponentInfo* findComponent(const std::string& name);
    NetInfo* findNet(const std::string& name);

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
};

// Utility functions for DEF parsing
namespace DefUtils {
    bool isFlipFlopCell(const std::string& cellType);
    int getBitWidth(const std::string& cellType);
    std::string extractOrientation(const std::string& orientStr);
    bool validateCoordinate(int x, int y);

    // 新增的函數聲明
    std::string getCellCategory(const std::string& cellType);
    bool isClockSignal(const std::string& netName);
    bool isScanSignal(const std::string& pinName);
}

#endif // PARSER_DEF_H