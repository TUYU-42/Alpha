#ifndef PARSER_VERILOG_H
#define PARSER_VERILOG_H

#include "DataStructures.h"
#include <string>
#include <vector>
#include <regex>
#include <unordered_map>
#include <memory>
#include <set>

// Verilog-specific data structures
struct VerilogModule {
    std::string name;
    std::vector<std::string> ports;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> wires;
    std::vector<std::string> regs;
    std::unordered_map<std::string, std::string> portDirections;
};

struct VerilogInstance {
    std::string cellType;
    std::string instName;
    std::vector<std::pair<std::string, std::string>> connections; // pin -> net
};




class VerilogParser {
private:
    std::vector<VerilogModule> modules_;
    std::vector<VerilogInstance> instances_;
    std::vector<InstPinNet> instPinNets_;
    bool isLoaded_;
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;
    std::vector<ScanChain> scanChains_;
    // Regex patterns
    std::regex moduleRegex_;
    std::regex instanceRegex_;
    std::regex pinConnectionRegex_;
    std::regex portDeclRegex_;
    std::regex wireDeclRegex_;

    // Helper methods
    bool parseModule(const std::string& content, size_t& pos);
    bool parsePortList(const std::string& portList, VerilogModule& module);
    bool parsePortDeclarations(const std::string& content, size_t& pos, VerilogModule& module);
    bool parseWireDeclarations(const std::string& content, size_t& pos, VerilogModule& module);
    bool parseInstances(const std::string& content, size_t& pos, VerilogModule& module);
    bool parseInstanceConnections(const std::string& connectionStr, VerilogInstance& instance);
    bool parseInstanceFromString(const std::string& instStr);

    void addError(const std::string& error);
    void addWarning(const std::string& warning);
	//scan chain methods
    void printScanChainTopology(const std::vector<VerilogInstance>& flipFlops,
        const std::map<std::string, std::vector<std::string>>& scanNets);
    void reconstructScanChains(const std::vector<VerilogInstance>& flipFlops);

public:
    // Constructor & Destructor
    VerilogParser();
    ~VerilogParser() = default;

    // Copy/Move constructors
    VerilogParser(const VerilogParser&) = delete;
    VerilogParser& operator=(const VerilogParser&) = delete;
    VerilogParser(VerilogParser&&) = default;
    VerilogParser& operator=(VerilogParser&&) = default;

    // Main interface methods
    bool parseFile(const std::string& filename);
    bool parseFromString(const std::string& content);

    // Data access methods
    const std::vector<VerilogModule>& getModules() const { return modules_; }
    const std::vector<VerilogInstance>& getInstances() const { return instances_; }
    const std::vector<InstPinNet>& getInstPinNets() const { return instPinNets_; }
    bool isLoaded() const { return isLoaded_; }

    // Query methods
    const VerilogModule* findModule(const std::string& name) const;
    const VerilogInstance* findInstance(const std::string& name) const;
    std::vector<std::string> getInstancesOfType(const std::string& cellType) const;
    std::vector<std::string> getNetsConnectedToInstance(const std::string& instName) const;
    int getCellBitWidth(const std::string& cellType) const;
    // Statistics methods
    size_t getModuleCount() const { return modules_.size(); }
    size_t getInstanceCount() const { return instances_.size(); }
    size_t getInstPinNetCount() const { return instPinNets_.size(); }

    // Analysis methods
    void extractInstPinNets();
    void analyzeHierarchy();
    void analyzeFlipFlopInstances();
    void analyzeClockConnections(const std::vector<VerilogInstance>& flipFlops);
    void analyzeScanChains(const std::vector<VerilogInstance>& flipFlops);
    void findClockNets();
    std::unordered_map<std::string, int> getCellTypeStatistics() const;

    // Utility methods
    void clear();
    void printSummary() const;
    void printModules() const;
    void printInstances() const;
    bool validateVerilog() const;

    // Error handling
    const std::vector<std::string>& getErrors() const { return errors_; }
    const std::vector<std::string>& getWarnings() const { return warnings_; }
    bool hasErrors() const { return !errors_.empty(); }
    bool hasWarnings() const { return !warnings_.empty(); }

    // Export methods
    bool writeVerilogFile(const std::string& filename) const;
    bool writeInstPinNetMapping(const std::string& filename) const;
    std::string toString() const;

	//scan chain methods
    void printScanChainSummary();

};


// Utility functions for Verilog parsing
namespace VerilogUtils {
    std::string removeComments(const std::string& content);
    std::string normalizeWhitespace(const std::string& content);
    std::vector<std::string> splitPortList(const std::string& portList);
    bool isValidIdentifier(const std::string& identifier);
    std::string extractNetName(const std::string& connection);
    bool isClockSignal(const std::string& signalName);

    // Enhanced utility functions
    std::string cleanNetName(const std::string& netName);
    bool isFlipFlopCell(const std::string& cellType);
    std::string categorizeCell(const std::string& cellType);
    int extractBitWidth(const std::string& cellType);
    bool isClockPin(const std::string& pinName);
    bool isScanPin(const std::string& pinName);
    bool isDataPin(const std::string& pinName);
}

#endif // PARSER_VERILOG_H