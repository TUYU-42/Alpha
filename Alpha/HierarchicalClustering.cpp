#include "HierarchicalClustering.h"
#include "ParserDEF.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <set>
#include <queue>
#include <iomanip>

// Constructor
HierarchicalClustering::HierarchicalClustering() : defParser_(nullptr), useDefScanChains_(false) {
    // Initialize statistics
    statistics_ = ClusteringStatistics();
}

// Destructor
HierarchicalClustering::~HierarchicalClustering() {
    // Clean up if needed
}

// Main clustering function
void HierarchicalClustering::performClustering(const std::vector<FlipFlopInfo>& flipFlops) {
    std::cout << "\n=== Starting Hierarchical Clustering ===" << std::endl;

    // Store the input
    flipFlops_ = flipFlops;
    statistics_.totalFlipFlops = flipFlops_.size();

    // Clear previous results
    clockDomains_.clear();
    clusteredDesign_.clear();
    useDefScanChains_ = false;

    // 檢查是否有 DEF scan chain 資訊
    if (defParser_ && defParser_->isLoaded() && defParser_->getScanChainCount() > 0) {
        useDefScanChains_ = true;
        std::cout << "✓ Found " << defParser_->getScanChainCount()
            << " scan chains in DEF, will use them for clustering" << std::endl;
    }
    else {
        std::cout << "✓ No scan chains found in DEF, will use connectivity-based clustering" << std::endl;
    }

    // Step 1: Build clock domains
    buildClockDomains();

    // Step 2: Build scan chains within each clock domain
    for (std::map<std::string, std::vector<FlipFlopInfo> >::const_iterator it = clockDomains_.begin();
        it != clockDomains_.end(); ++it) {
        const std::string& clockNet = it->first;
        const std::vector<FlipFlopInfo>& domainFFs = it->second;
        buildScanChainsForDomain(clockNet, domainFFs);
    }


    // Step 3: Calculate statistics
    calculateStatistics();

    std::cout << "✓ Hierarchical clustering completed" << std::endl;
}

// Step 1: Build clock domains
void HierarchicalClustering::buildClockDomains() {
    std::cout << "\n--- Step 1: Building Clock Domains ---" << std::endl;

    for (const auto& ff : flipFlops_) {
        std::string clockNet = ff.clockNet;

        // Handle unconnected or empty clock nets
        if (clockNet.empty() || clockNet == "UNCONNECTED") {
            clockNet = "NO_CLOCK";
        }

        // Add FF to its clock domain
        clockDomains_[clockNet].push_back(ff);
    }

    statistics_.totalClockDomains = clockDomains_.size();

    // Print summary
    std::cout << "Found " << clockDomains_.size() << " clock domains:" << std::endl;
    for (std::map<std::string, std::vector<FlipFlopInfo> >::const_iterator it = clockDomains_.begin();
        it != clockDomains_.end(); ++it) {
        const std::string& clockNet = it->first;
        const std::vector<FlipFlopInfo>& ffs = it->second;

        std::cout << "  " << std::setw(20) << std::left << clockNet
            << " : " << ffs.size() << " flip-flops" << std::endl;
        statistics_.ffPerClockDomain[clockNet] = ffs.size();
    }

}

// Step 2: Build scan chains for a specific clock domain
void HierarchicalClustering::buildScanChainsForDomain(const std::string& clockNet,
    const std::vector<FlipFlopInfo>& domainFFs) {
    std::cout << "\n--- Building scan chains for clock domain: " << clockNet << " ---" << std::endl;

    std::vector<ScanChainClustered> chains;

    if (useDefScanChains_) {
        // 使用 DEF 中的 scan chain 資訊
        chains = reconstructScanChainsFromDef(domainFFs);
        std::cout << "  Using scan chains from DEF file" << std::endl;
    }
    else {
        // 使用連接性分析重建 scan chain
        chains = reconstructScanChainsFromConnectivity(domainFFs);
        std::cout << "  Reconstructing scan chains from connectivity" << std::endl;
    }

    clusteredDesign_[clockNet] = chains;

    // Update statistics
    statistics_.chainsPerClockDomain[clockNet] = chains.size();
    statistics_.totalScanChains += chains.size();

    // Print summary for this domain
    std::cout << "  Found " << chains.size() << " scan chains:" << std::endl;

    std::vector<int> chainLengths;
    for (size_t i = 0; i < chains.size(); ++i) {
        int length = chains[i].length();
        chainLengths.push_back(length);

        if (i < 5 || length > 1) { // Show first 5 or all multi-node chains
            std::cout << "    Chain " << i << ": " << length << " nodes";
            if (length == 1) {
                std::cout << " (isolated FF: " << chains[i].nodes[0].instanceName << ")";
            }
            else {
                std::cout << " (" << chains[i].nodes[0].instanceName
                    << " -> ... -> "
                    << chains[i].nodes[length - 1].instanceName << ")";
            }
            std::cout << std::endl;
        }
    }

    if (chains.size() > 5) {
        std::cout << "    ... and " << (chains.size() - 5) << " more chains" << std::endl;
    }

    statistics_.chainLengthsPerDomain[clockNet] = chainLengths;
}

// 使用 DEF 的 scan chain 資訊重建 scan chains
std::vector<ScanChainClustered> HierarchicalClustering::reconstructScanChainsFromDef(
    const std::vector<FlipFlopInfo>& domainFFs) {

    std::vector<ScanChainClustered> chains;

    if (!defParser_) return chains;

    // 建立 instance name 到 FF 的映射
    std::map<std::string, const FlipFlopInfo*> instanceToFF;
    for (const auto& ff : domainFFs) {
        instanceToFF[ff.instName] = &ff;
    }

    // 標記已處理的 FF
    std::set<std::string> processed;

    // 處理 DEF 中的每條 scan chain
    const auto& defScanChains = defParser_->getScanChains();

    for (const auto& defChain : defScanChains) {
        ScanChainClustered chain;
        chain.chainId = defChain.name;

        // 只處理屬於這個 clock domain 的 FF
        for (const auto& ffName : defChain.ffNames) {
            auto it = instanceToFF.find(ffName);
            if (it != instanceToFF.end()) {
                chain.addNode(ffName, it->second->cellType);
                processed.insert(ffName);
            }
        }

        // 如果這條鏈有屬於此 domain 的 FF，則加入結果
        if (!chain.isEmpty()) {
            chains.push_back(chain);
        }
    }

    // 處理不在任何 DEF scan chain 中的 FF（isolated）
    for (const auto& ff : domainFFs) {
        if (processed.find(ff.instName) == processed.end()) {
            ScanChainClustered isolatedChain;
            isolatedChain.addNode(ff.instName, ff.cellType);
            chains.push_back(isolatedChain);
        }
    }

    return chains;
}

// 從連接性重建 scan chains（原始方法）
std::vector<ScanChainClustered> HierarchicalClustering::reconstructScanChainsFromConnectivity(
    const std::vector<FlipFlopInfo>& ffs) {

    std::vector<ScanChainClustered> chains;

    // Build connection maps
    std::map<std::string, std::string> soToInstance;  // SO net -> instance name
    std::map<std::string, std::string> siToInstance;  // SI net -> instance name
    std::map<std::string, const FlipFlopInfo*> instanceToFF;  // instance name -> FF info

    for (const auto& ff : ffs) {
        instanceToFF[ff.instName] = &ff;

        if (!ff.scanOut.empty() && ff.scanOut != "UNCONNECTED") {
            soToInstance[ff.scanOut] = ff.instName;
        }

        if (!ff.scanIn.empty() && ff.scanIn != "UNCONNECTED") {
            siToInstance[ff.scanIn] = ff.instName;
        }
    }

    // Build SO->SI connection map (which FF's SO connects to which FF's SI)
    std::map<std::string, std::string> nextInChain;  // instance -> next instance
    std::map<std::string, std::string> prevInChain;  // instance -> previous instance

    for (const auto& ff : ffs) {
        if (!ff.scanOut.empty() && ff.scanOut != "UNCONNECTED") {
            // Check if this SO net connects to any SI
            auto it = siToInstance.find(ff.scanOut);
            if (it != siToInstance.end()) {
                nextInChain[ff.instName] = it->second;
                prevInChain[it->second] = ff.instName;
            }
        }
    }

    // Find chain starts (FFs with no previous FF or unconnected SI)
    std::set<std::string> chainStarts;
    std::set<std::string> visited;

    for (const auto& ff : ffs) {
        bool isStart = false;

        // Case 1: No previous FF in chain
        if (prevInChain.find(ff.instName) == prevInChain.end()) {
            isStart = true;
        }

        // Case 2: SI is unconnected
        if (ff.scanIn.empty() || ff.scanIn == "UNCONNECTED") {
            isStart = true;
        }

        // Case 3: SI connects to a net that no FF drives
        if (!ff.scanIn.empty() && ff.scanIn != "UNCONNECTED" &&
            soToInstance.find(ff.scanIn) == soToInstance.end()) {
            isStart = true;
        }

        if (isStart) {
            chainStarts.insert(ff.instName);
        }
    }

    // Build chains starting from each chain start
    for (const std::string& start : chainStarts) {
        if (visited.find(start) != visited.end()) continue;

        ScanChainClustered chain;
        std::string current = start;

        // Follow the chain
        while (!current.empty() && visited.find(current) == visited.end()) {
            visited.insert(current);

            auto ffIt = instanceToFF.find(current);
            if (ffIt != instanceToFF.end()) {
                chain.addNode(current, ffIt->second->cellType);
            }

            // Move to next in chain
            auto nextIt = nextInChain.find(current);
            if (nextIt != nextInChain.end()) {
                current = nextIt->second;
            }
            else {
                break;
            }
        }

        if (!chain.isEmpty()) {
            chains.push_back(chain);
        }
    }

    // Handle isolated FFs (not in any chain)
    for (const auto& ff : ffs) {
        if (visited.find(ff.instName) == visited.end()) {
            ScanChainClustered isolatedChain;
            isolatedChain.addNode(ff.instName, ff.cellType);
            chains.push_back(isolatedChain);
            visited.insert(ff.instName);
        }
    }

    return chains;
}

// 將 DEF scan chain 轉換為內部格式
bool HierarchicalClustering::convertDefScanChainToInternal(const ScanChain& defChain,
    const std::vector<FlipFlopInfo>& domainFFs,
    ScanChainClustered& result) {

    // 建立 instance name 到 FF 的映射
    std::map<std::string, const FlipFlopInfo*> instanceToFF;
    for (const auto& ff : domainFFs) {
        instanceToFF[ff.instName] = &ff;
    }

    result.chainId = defChain.name;

    // 轉換每個 FF
    for (const auto& ffName : defChain.ffNames) {
        auto it = instanceToFF.find(ffName);
        if (it != instanceToFF.end()) {
            result.addNode(ffName, it->second->cellType);
        }
    }

    return !result.isEmpty();
}

// Calculate clustering statistics
void HierarchicalClustering::calculateStatistics() {
    // Basic statistics are already calculated during clustering
    // This function can be extended for more complex analysis
}

// Print clustering summary
void HierarchicalClustering::printClusteringSummary() const {
    std::cout << "\n=== Hierarchical Clustering Summary ===" << std::endl;
    std::cout << "Total flip-flops: " << statistics_.totalFlipFlops << std::endl;
    std::cout << "Total clock domains: " << statistics_.totalClockDomains << std::endl;
    std::cout << "Total scan chains: " << statistics_.totalScanChains << std::endl;

    if (useDefScanChains_) {
        std::cout << "Scan chain source: DEF file" << std::endl;
    }
    else {
        std::cout << "Scan chain source: Connectivity analysis" << std::endl;
    }

    std::cout << "\nPer clock domain statistics:" << std::endl;
    for (std::map<std::string, int>::const_iterator it = statistics_.ffPerClockDomain.begin();
        it != statistics_.ffPerClockDomain.end(); ++it) {
        const std::string& clockNet = it->first;
        int ffCount = it->second;

        std::cout << "  " << std::setw(20) << std::left << clockNet << ": "
            << ffCount << " FFs, "
            << statistics_.chainsPerClockDomain.at(clockNet) << " chains" << std::endl;
    }

}

// Print detailed report
void HierarchicalClustering::printDetailedReport() const {
    std::cout << "\n=== Detailed Clustering Report ===" << std::endl;

    for (std::map<std::string, std::vector<ScanChainClustered> >::const_iterator it = clusteredDesign_.begin();
        it != clusteredDesign_.end(); ++it) {
        const std::string& clockNet = it->first;
        const std::vector<ScanChainClustered>& chains = it->second;

        std::cout << "\nClock Domain: " << clockNet << std::endl;
        std::cout << "Number of chains: " << chains.size() << std::endl;

        // Chain length distribution
        std::map<int, int> lengthDistribution;
        for (std::vector<ScanChainClustered>::const_iterator ch_it = chains.begin(); ch_it != chains.end(); ++ch_it) {
            lengthDistribution[ch_it->length()]++;
        }

        std::cout << "Chain length distribution:" << std::endl;
        for (std::map<int, int>::const_iterator ld_it = lengthDistribution.begin(); ld_it != lengthDistribution.end(); ++ld_it) {
            int length = ld_it->first;
            int count = ld_it->second;
            std::cout << "  Length " << std::setw(3) << length << ": "
                << std::setw(4) << count << " chains" << std::endl;
        }

        // Show details of longest chains
        std::vector<const ScanChainClustered*> sortedChains;
        for (std::vector<ScanChainClustered>::const_iterator ch_it = chains.begin(); ch_it != chains.end(); ++ch_it) {
            sortedChains.push_back(&(*ch_it));
        }

        std::sort(sortedChains.begin(), sortedChains.end(),
            [](const ScanChainClustered* a, const ScanChainClustered* b) {
                return a->length() > b->length();
            });

        std::cout << "\nLongest chains in this domain:" << std::endl;
        for (size_t i = 0; i < std::min(size_t(3), sortedChains.size()); ++i) {
            const ScanChainClustered* chain = sortedChains[i];
            std::cout << "  Chain " << i + 1 << " (length " << chain->length() << "):" << std::endl;
            if (!chain->chainId.empty()) {
                std::cout << "    ID: " << chain->chainId << std::endl;
            }
            std::cout << "    Start: " << chain->nodes.front().instanceName
                << " (" << chain->nodes.front().cellType << ")" << std::endl;
            std::cout << "    End:   " << chain->nodes.back().instanceName
                << " (" << chain->nodes.back().cellType << ")" << std::endl;
        }
    }

}

// Export clustering results to file
void HierarchicalClustering::exportToFile(const std::string& filename) const {
    std::ofstream outFile(filename);
    if (!outFile.is_open()) {
        std::cerr << "Error: Cannot create file " << filename << std::endl;
        return;
    }

    outFile << "# Hierarchical Clustering Results" << std::endl;
    outFile << "# Total FFs: " << statistics_.totalFlipFlops << std::endl;
    outFile << "# Clock Domains: " << statistics_.totalClockDomains << std::endl;
    outFile << "# Total Chains: " << statistics_.totalScanChains << std::endl;
    outFile << "# Scan Chain Source: " << (useDefScanChains_ ? "DEF" : "Connectivity") << std::endl;
    outFile << std::endl;

    for (std::map<std::string, std::vector<ScanChainClustered> >::const_iterator it = clusteredDesign_.begin();
        it != clusteredDesign_.end(); ++it) {
        const std::string& clockNet = it->first;
        const std::vector<ScanChainClustered>& chains = it->second;

        outFile << "CLOCK_DOMAIN " << clockNet << std::endl;
        outFile << "CHAIN_COUNT " << chains.size() << std::endl;

        for (size_t i = 0; i < chains.size(); ++i) {
            outFile << "CHAIN " << i << " LENGTH " << chains[i].length();
            if (!chains[i].chainId.empty()) {
                outFile << " ID " << chains[i].chainId;
            }
            outFile << std::endl;
            for (std::vector<ScanChainNode>::const_iterator node_it = chains[i].nodes.begin();
                node_it != chains[i].nodes.end(); ++node_it) {
                outFile << "  " << node_it->instanceName << " " << node_it->cellType << std::endl;
            }
        }
        outFile << std::endl;
    }


    outFile.close();
    std::cout << "✓ Clustering results exported to " << filename << std::endl;
}

// Find banking candidates
std::vector<BankingCandidate> HierarchicalClustering::findBankingCandidates(
    const std::string& clockDomain) const {

    std::vector<BankingCandidate> candidates;

    // 如果指定了 clock domain，只處理該 domain
    std::vector<std::string> domainsToProcess;
    if (!clockDomain.empty()) {
        if (clusteredDesign_.find(clockDomain) != clusteredDesign_.end()) {
            domainsToProcess.push_back(clockDomain);
        }
    }
    else {
        // 處理所有 domains
        for (std::map<std::string, std::vector<ScanChainClustered> >::const_iterator it = clusteredDesign_.begin();
            it != clusteredDesign_.end(); ++it) {
            const std::string& domain = it->first;
            domainsToProcess.push_back(domain);
        }

    }

    // 對每個 domain 找 banking candidates
    for (const auto& domain : domainsToProcess) {
        const auto& chains = clusteredDesign_.at(domain);

        for (const auto& chain : chains) {
            // 如果 chain 長度 >= 2，可以考慮 banking
            if (chain.length() >= 2) {
                // 簡單策略：每 2 個或 4 個連續 FF 作為一個 candidate
                for (size_t i = 0; i + 1 < chain.nodes.size(); i += 2) {
                    BankingCandidate candidate;
                    candidate.flipFlops.push_back(chain.nodes[i].instanceName);
                    candidate.flipFlops.push_back(chain.nodes[i + 1].instanceName);

                    // 檢查是否可以組成 4-bit
                    if (i + 3 < chain.nodes.size()) {
                        candidate.flipFlops.push_back(chain.nodes[i + 2].instanceName);
                        candidate.flipFlops.push_back(chain.nodes[i + 3].instanceName);
                        candidate.targetMBFF = "4BIT_FF"; // 假設的目標類型
                        i += 2; // 跳過已處理的
                    }
                    else {
                        candidate.targetMBFF = "2BIT_FF"; // 假設的目標類型
                    }

                    candidates.push_back(candidate);
                }
            }
        }
    }

    return candidates;
}

// Validate clustering results
bool HierarchicalClustering::validateClustering() const {
    // Check that all original FFs are accounted for
    int totalFFsInClusters = 0;

    for (std::map<std::string, std::vector<ScanChainClustered> >::const_iterator it = clusteredDesign_.begin();
        it != clusteredDesign_.end(); ++it) {
        const std::vector<ScanChainClustered>& chains = it->second;
        for (std::vector<ScanChainClustered>::const_iterator chain_it = chains.begin(); chain_it != chains.end(); ++chain_it) {
            totalFFsInClusters += chain_it->length();
        }
    }


    if (totalFFsInClusters != statistics_.totalFlipFlops) {
        std::cerr << "Error: FF count mismatch! Original: " << statistics_.totalFlipFlops
            << ", In clusters: " << totalFFsInClusters << std::endl;
        return false;
    }

    std::cout << "✓ Clustering validation passed" << std::endl;
    return true;
}