#pragma once
#ifndef HIERARCHICAL_CLUSTERING_H
#define HIERARCHICAL_CLUSTERING_H

#include "DataStructures.h"
#include <map>
#include <vector>
#include <string>
#include <memory>

class HierarchicalClustering {
private:
    // 原始的 flip-flop 資訊
    std::vector<FlipFlopInfo> flipFlops_;

    // 階層式分群結果
    // 第一層：按時脈域分群
    std::map<std::string, std::vector<FlipFlopInfo>> clockDomains_;

    // 第二層：在每個時脈域內的掃描鏈
    std::map<std::string, std::vector<ScanChain>> clusteredDesign_;

    // 統計資訊
    ClusteringStatistics statistics_;

    // Helper functions
    void buildClockDomains();
    void buildScanChainsForDomain(const std::string& clockNet,
        const std::vector<FlipFlopInfo>& domainFFs);
    std::vector<ScanChain> reconstructScanChains(const std::vector<FlipFlopInfo>& ffs);
    void calculateStatistics();

public:
    // Constructor
    HierarchicalClustering();

    // Destructor
    ~HierarchicalClustering();

    // Main clustering function
    void performClustering(const std::vector<FlipFlopInfo>& flipFlops);

    // Getters
    const std::map<std::string, std::vector<FlipFlopInfo>>& getClockDomains() const {
        return clockDomains_;
    }

    const std::map<std::string, std::vector<ScanChain>>& getClusteredDesign() const {
        return clusteredDesign_;
    }

    const ClusteringStatistics& getStatistics() const {
        return statistics_;
    }


    // Analysis functions
    void printClusteringSummary() const;
    void printDetailedReport() const;
    void exportToFile(const std::string& filename) const;

    // Utility functions
    std::vector<BankingCandidate> findBankingCandidates(const std::string& clockDomain = "") const;
    bool validateClustering() const;
};

#endif // HIERARCHICAL_CLUSTERING_H