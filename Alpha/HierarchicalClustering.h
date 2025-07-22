#pragma once
#ifndef HIERARCHICAL_CLUSTERING_H
#define HIERARCHICAL_CLUSTERING_H

#include "DataStructures.h"
#include <map>
#include <vector>
#include <string>
#include <memory>

// Forward declaration
class DefParser;

class HierarchicalClustering {
private:
    // 原始的 flip-flop 資訊
    std::vector<FlipFlopInfo> flipFlops_;

    // DEF parser 的參考（用來取得 scan chain 資訊）
    const DefParser* defParser_;

    // 階層式分群結果
    // 第一層：按時脈領域分群
    std::map<std::string, std::vector<FlipFlopInfo>> clockDomains_;

    // 第二層：在每個時脈領域內的掃描鏈
    std::map<std::string, std::vector<ScanChainClustered>> clusteredDesign_;

    // 統計資訊
    ClusteringStatistics statistics_;

    // 標記是否使用 DEF 的 scan chain 資訊
    bool useDefScanChains_;

    // Helper functions
    void buildClockDomains();
    void buildScanChainsForDomain(const std::string& clockNet,
        const std::vector<FlipFlopInfo>& domainFFs);

    // 兩種重建 scan chain 的方法
    std::vector<ScanChainClustered> reconstructScanChainsFromDef(
        const std::vector<FlipFlopInfo>& domainFFs);
    std::vector<ScanChainClustered> reconstructScanChainsFromConnectivity(
        const std::vector<FlipFlopInfo>& domainFFs);

    void calculateStatistics();

    // 將 DEF 的 scan chain 轉換為內部格式
    bool convertDefScanChainToInternal(const ScanChain& defChain,
        const std::vector<FlipFlopInfo>& domainFFs,
        ScanChainClustered& result);

public:
    // Constructor
    HierarchicalClustering();

    // Destructor
    ~HierarchicalClustering();

    // 設定 DEF parser（用來取得 scan chain 資訊）
    void setDefParser(const DefParser* defParser) {
        defParser_ = defParser;
    }

    // Main clustering function
    void performClustering(const std::vector<FlipFlopInfo>& flipFlops);

    // Getters（保持原有介面）
    const std::map<std::string, std::vector<FlipFlopInfo>>& getClockDomains() const {
        return clockDomains_;
    }

    const std::map<std::string, std::vector<ScanChainClustered>>& getClusteredDesign() const {
        return clusteredDesign_;
    }

    const ClusteringStatistics& getStatistics() const {
        return statistics_;
    }

    // 檢查是否使用 DEF scan chains
    bool isUsingDefScanChains() const { return useDefScanChains_; }

    // Analysis functions（保持原有介面）
    void printClusteringSummary() const;
    void printDetailedReport() const;
    void exportToFile(const std::string& filename) const;

    // Utility functions（保持原有介面）
    std::vector<BankingCandidate> findBankingCandidates(const std::string& clockDomain = "") const;
    bool validateClustering() const;
};

#endif // HIERARCHICAL_CLUSTERING_H