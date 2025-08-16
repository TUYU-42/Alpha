// WriteOutput.h
#ifndef WRITE_OUTPUT_H
#define WRITE_OUTPUT_H

#include "DataStructures.h"
#include "DPC.h"
#include "ParserDEF.h"
#include "ParserVerilog.h"
#include "LibParser.h"
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <set>
#include <regex>

class WriteOutput {
private:

    const std::string outputName_;
    const MergeMapping& mergeMap_;
    const DefData& originalDefData_;
    const std::vector<MergedFF>& mergedFFResults_;
    const VerilogParser* verilogParser_;
    LibParser* libParser_;
    std::unordered_map<std::string, std::string> simpleToFullNameMap_;
    // 階層名稱映射
    std::unordered_map<std::string, std::string> hierarchicalMapping_; // local -> full path
    std::unordered_map<std::string, std::string> reverseMapping_;      // full path -> local

    // 已處理的 instances
    std::set<std::string> processedInstances_;
    std::set<std::string> outputtedMBFFs_;

    // Helper methods
    void buildHierarchicalMapping();
    std::string getFullPath(const std::string& localName) const;
    std::string getLocalName(const std::string& fullPath) const;

    // Instance 處理
    bool isFlipFlopInstance(const std::string& cellType) const;
    std::string generateMBFFInstance(const MergedFF& mergedFF) const;
    std::string generateInstanceString(const VerilogInstance& inst) const;

    // Pin mapping helpers
    std::vector<std::pair<std::string, std::string>> getMBFFPinConnections(
        const MergedFF& mergedFF) const;
    std::string findNetForPin(const std::string& ffName, const std::string& pinName) const;
    void buildSimpleToFullNameMapping();
    // Utility
    int getBitIndexFromPairs(const std::vector<std::pair<int, std::string>>& pairs,
        const std::string& instanceName) const;
    std::string generateMBFFInstance(const MergedFF& mergedFF,
        const VerilogInstance& origInst) const;
public:
    WriteOutput(const std::string& outputName,
        const MergeMapping& mergeMap,
        const DefData& originalDefData,
        const std::vector<MergedFF>& mergedFFResults,
        const VerilogParser* verilogParser = nullptr);

    // Setters
    void setLibParser(LibParser* parser) { libParser_ = parser; }

    // 主要輸出方法
    bool writeMapList();      // Step 1: 產生 outputName.list
    bool writeVerilog();      // Step 2: 產生 outputName.v  
    bool writeDef();          // Step 3: 產生 outputName.def

    // 整合的輸出方法
    bool writeAll();
};

#endif // WRITE_OUTPUT_H