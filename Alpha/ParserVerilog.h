// ParserVerilog.h - 修改後的版本

#ifndef PARSER_VERILOG_H
#define PARSER_VERILOG_H

#include "DataStructures.h"
#include <string>
#include <vector>
#include <regex>
#include <unordered_map>
#include"LibParser.h"
#include <memory>
#include <set>

// 前向宣告
struct VerilogInstance;
struct VerilogModule;

// 增強的 VerilogInstance 結構
struct VerilogInstance {
    std::string cellType;
    std::string instName;
    std::vector<std::pair<std::string, std::string>> connections; // pin -> net

    // 新增：階層資訊
    std::string parentModuleName;  // 所屬的 module 名稱
    std::string hierarchicalPath;  // 完整的階層路徑

    // 新增：是否為模組實例
    bool isModuleInstance = false;
    std::string referencedModule;  // 如果是模組實例，參考的模組名稱
};

// 增強的 VerilogModule 結構
struct VerilogModule {
    std::string name;
    std::vector<std::string> ports;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> wires;
    std::vector<std::string> assignStatements;
    std::vector<std::string> regs;
    std::unordered_map<std::string, std::string> portDirections;

    // 新增：此模組內的 instances
    std::vector<VerilogInstance> instances;

    // 新增：子模組實例映射
    std::unordered_map<std::string, std::string> subModuleInstances; // inst_name -> module_type
};

// 新增：階層資訊結構
struct HierarchyNode {
    std::string instanceName;
    std::string moduleName;
    std::string fullPath;
    std::vector<std::shared_ptr<HierarchyNode>> children;
    std::weak_ptr<HierarchyNode> parent;
};

class VerilogParser {
private:
    std::vector<VerilogModule> modules_;
    std::vector<VerilogInstance> instances_;  // 保留為相容性，但主要存在 module 中
    std::vector<InstPinNet> instPinNets_;
    bool isLoaded_;
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;
    std::vector<ScanChain> scanChains_;
    LibParser* libParser_;
    // 新增：階層管理
    std::shared_ptr<HierarchyNode> hierarchyRoot_;
    std::unordered_map<std::string, VerilogModule*> moduleMap_;  // 快速查找模組
    std::unordered_map<std::string, VerilogInstance*> hierarchicalInstanceMap_;  // 完整路徑 -> instance
    std::string topModuleName_;  // 頂層模組名稱

    // Regex patterns
    std::regex moduleRegex_;
    std::regex instanceRegex_;
    std::regex pinConnectionRegex_;
    std::regex portDeclRegex_;
    std::regex wireDeclRegex_;

    // Helper methods - 原有的
    bool parseModule(const std::string& content, size_t& pos);
    bool parsePortList(const std::string& portList, VerilogModule& module);
    bool parsePortDeclarations(const std::string& content, size_t& pos, VerilogModule& module);
    bool parseWireDeclarations(const std::string& content, size_t& pos, VerilogModule& module);
    bool parseInstances(const std::string& content, size_t& pos, VerilogModule& module);
    bool parseInstanceConnections(const std::string& connectionStr, VerilogInstance& instance);
    bool parseInstanceFromString(const std::string& instStr);
    void identifyTopModule();
    // 新增：階層相關方法
    void buildHierarchy();
    void buildModuleMap();
    void buildHierarchyRecursive(std::shared_ptr<HierarchyNode> parentNode,
        const std::string& moduleName,
        const std:: string& parentPath);
    std::shared_ptr<HierarchyNode> buildHierarchyNode(const std::string& moduleName,
        const std::string& instanceName,
        const std::string& parentPath);
    void flattenHierarchy(std::shared_ptr<HierarchyNode> node, const std::string& currentPath);
    std::string buildHierarchicalPath(const std::string& parentPath, const std::string& instanceName);
    void updateInstanceHierarchicalPaths();
    void printHierarchyNode(std::shared_ptr<HierarchyNode> node, int depth) const;
    // Scan chain methods
    void printScanChainTopology(const std::vector<VerilogInstance>& flipFlops,
        const std::map<std::string, std::vector<std::string>>& scanNets);
    void reconstructScanChains(const std::vector<VerilogInstance>& flipFlops);
    void updateModuleInstanceFlags();
    void addError(const std::string& error);
    void addWarning(const std::string& warning);

public:
    // Constructor & Destructor
    VerilogParser();
    ~VerilogParser() = default;
    void setLibParser(LibParser* parser) { libParser_ = parser; }
    // Main interface methods
    bool parseFile(const std::string& filename);
    bool parseFromString(const std::string& content);

    // Data access methods - 原有的
    const std::vector<VerilogModule>& getModules() const { return modules_; }
    const std::vector<VerilogInstance>& getInstances() const { return instances_; }
    const std::vector<InstPinNet>& getInstPinNets() const { return instPinNets_; }
    bool isLoaded() const { return isLoaded_; }

    // 新增：階層查詢方法
    const VerilogModule* getTopModule() const;
    std::string getInstanceHierarchicalPath(const std::string& localName) const;
    const VerilogInstance* findInstanceByHierarchicalPath(const std::string& path) const;
    std::vector<VerilogInstance> getAllInstancesFlattened() const;
    std::unordered_map<std::string, std::string> getHierarchicalNameMapping() const;

    // 新增：取得所有 FF instances 的完整路徑
    std::vector<std::pair<std::string, std::string>> getFFInstancesWithPaths() const;

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
    bool parseAssignStatements(const std::string& content, size_t& pos, VerilogModule& module);  // << 宣告
    // Utility methods
    void clear();
    void printSummary() const;
    void printModules() const;
    void printInstances() const;
    void printHierarchy() const;  // 新增
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

    // Scan chain methods
    void printScanChainSummary();
};

// Utility functions
namespace VerilogUtils {
    // 原有的工具函數...
    std::string removeComments(const std::string& content);
    std::string normalizeWhitespace(const std::string& content);
    std::vector<std::string> splitPortList(const std::string& portList);
    bool isValidIdentifier(const std::string& identifier);
    std::string extractNetName(const std::string& connection);
    bool isClockSignal(const std::string& signalName);
    std::string cleanNetName(const std::string& netName);
    bool isFlipFlopCell(const std::string& cellType);
    std::string categorizeCell(const std::string& cellType);
    int extractBitWidth(const std::string& cellType);
    bool isClockPin(const std::string& pinName);
    bool isScanPin(const std::string& pinName);
    bool isDataPin(const std::string& pinName);

    // 新增：階層相關工具函數
    std::string extractInstanceBaseName(const std::string& hierarchicalName);
    std::vector<std::string> splitHierarchicalPath(const std::string& path);
    std::string joinHierarchicalPath(const std::vector<std::string>& parts);
}

#endif // PARSER_VERILOG_H