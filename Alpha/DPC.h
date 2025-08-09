#pragma once
#include "DataStructures.h"
#include "LibParser.h"
#include <algorithm>
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <utility>
#include <limits>
#include <unordered_map>

struct DPCPoint {
    std::string instanceName;
    std::string cellType;
    double x, y;
    double width = 0.0, height = 0.0;
    double rho = 0.0;
    double delta = 0.0;
    int nearestHigher = -1;
    int clusterId = -1;
    bool isCenter = false;
};

struct DPCCluster {
    int clusterId = -1;
    int centerIdx = -1;
    std::vector<int> members;
    std::string celltype; // optional: 若你有用這個記錄 MBFF 型別
    double avgX = 0.0, avgY = 0.0;
    double minX = 0.0, maxX = 0.0;
    double minY = 0.0, maxY = 0.0;
    double radius = 0.0;
};

class DensityPeakClustering {
public:
    DensityPeakClustering();
    ~DensityPeakClustering();

    std::map<std::string, std::vector<DPCCluster>> clusterByScanChain(
        const std::map<std::string, std::vector<ScanChain>>& scanChains,
        const std::map<std::string, FlipFlopInfo>& ffLookup,
        bool autoTune = true);

    std::map<std::string, std::vector<DPCCluster>> clusterByClockNet(
        const std::map<std::string, FlipFlopInfo>& ffLookup,
        bool autoTune = true);

    void printClusteringSummary() const;

    void setMacroMap(const std::unordered_map<std::string, LefMacroInfo>* m) {
        macroMap_ = m;
    }
    void setLibParser(class LibParser* lib) {
        libParser_ = lib;
    }

    int getClusterIdForInstance(const std::string& name) const;
    const DPCCluster* getClusterById(int cid) const;
    void analyzeSingleBitMergeCandidates();
    Weights weights_;
    const std::vector<MergedFF>& getMergedFFResults() const {
        return mergedFFResults_;
    }
    void reportMergedFFResults() const;

    std::vector<PlacedComponent> generatePlacementComponents(const DefData& defData, const LefData& lefData);
    std::vector<NewFlipFlopInfo> generatePlacementFFsNew(const DefData& defData, const LefData& lefData) const;
    void dumpNewFFsToTxt(const std::vector<NewFlipFlopInfo>& newFFs, const std::string& filename) const;
    void dumpPlacedComponentsToTxt(const std::vector<PlacedComponent>& components, const std::string& filename) const;
    long long int getTotalPlacedComponentCount() const { return totalPlacedComponentCount_; }
    const MergeMapping& getMergeMap() const;
    std::vector<std::pair<int, std::string>> getBitIndexedPairs(const std::string& mbffName) const;

private:
    void performClusteringOnScanChain(
        const ScanChain& chain,
        const std::map<std::string, FlipFlopInfo>& ffLookup,
        bool autoTune);

    void performClustering(const std::vector<FlipFlopInfo>& flipFlops, bool autoTune);
    void loadPointsFromFF(const std::vector<FlipFlopInfo>& flipFlops);
    void buildDistanceMatrix();
    double boxManhattanDistance(const DPCPoint& a, const DPCPoint& b);
    void computeRho();
    void computeDelta();
    std::vector<int> selectCenters();
    void assignClusters(const std::vector<int>& centerIndices);
    void computeClusterGeometry();
    int estimateBestClusterCount() const;
    double estimateOptimalCutoffDistance() const;

    std::vector<DPCPoint> points_;
    std::vector<std::vector<double>> distMat_;
    std::vector<DPCCluster> clusters_;
    double cutoffDistance_ = 0.0;
    MergeMapping mergeMap_;
    NewFlipFlopInfo FF_;
    const std::unordered_map<std::string, LefMacroInfo>* macroMap_ = nullptr;
    class LibParser* libParser_ = nullptr;
    std::unordered_map<std::string, int> instanceToClusterId_;
    std::unordered_map<int, DPCCluster> clusterIdToCluster_;
    std::vector<MergedFF> mergedFFResults_; // 儲存分析後的合併結果
    long long int totalPlacedComponentCount_;

};
