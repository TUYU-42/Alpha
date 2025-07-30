#pragma once
#ifndef CROSS_REFERENCE_MANAGER_H
#define CROSS_REFERENCE_MANAGER_H

#include "LibParser.h"
#include "ParserVerilog.h"
#include "DataStructures.h"
#include <string>
#include <vector>
#include <map>
#include <set>

// 用於存儲 pin mapping 資?
struct PinMapping {
    std::string sourceInstance;
    std::string sourcePin;
    std::string targetInstance;
    std::string targetPin;
    std::string net;
};

// 用於存儲 scan chain segment
struct ScanChainSegment {
    std::string fromInstance;
    std::string fromPin;
    std::string toInstance;
    std::string toPin;
    std::string net;
    bool usesQAsOut;  // 是否使用 Q pin 作為 scan out
};

// Cross-reference 管理器
class CrossReferenceManager {
private:
    const LibParser* libParser_;
    const VerilogParser* verilogParser_;

    // Cache for frequently accessed data
    std::map<std::string, const LibCell*> cellCache_;
    std::map<std::string, std::vector<std::string>> scanPinCache_;

public:
    CrossReferenceManager(const LibParser* lib, const VerilogParser* verilog)
        : libParser_(lib), verilogParser_(verilog) {
    }

    // 基本查?功能
    const LibCell* getCellForInstance(const VerilogInstance& inst);
    std::vector<std::string> getInstanceScanPins(const VerilogInstance& inst);
    bool isInstanceScanPin(const VerilogInstance& inst, const std::string& pinName);

    // Bundle 相關功能
    std::vector<std::string> getInstanceBundleMembers(const VerilogInstance& inst, const std::string& bundleName);
    std::string findScanOutPin(const VerilogInstance& inst);

    // Scan chain 分析
    std::vector<ScanChainSegment> analyzeScanChainConnections();
    std::map<std::string, std::vector<std::string>> buildScanChains();

    // Banking/Debanking 支援
    std::vector<PinMapping> generateBankingMapping(
        const std::vector<std::string>& singleBitInstances,
        const std::string& multiBitInstance,
        const std::string& multiBitCellType);

    std::vector<PinMapping> generateDebankingMapping(
        const std::string& multiBitInstance,
        const std::vector<std::string>& singleBitInstances,
        const std::string& singleBitCellType);

    // ?證功能
    bool validateScanChainIntegrity();
    bool validatePinConnections(const VerilogInstance& inst);

    // 輔助功能
    void printInstanceDetails(const std::string& instName);
    void printScanChainReport();
    void clearCache();
};  
#endif