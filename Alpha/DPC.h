#pragma once
#include "DataStructures.h"
#include <algorithm>
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <utility>
#include <limits>
#include <unordered_map>

// Forward declarations to avoid heavy includes
struct FlipFlopInfo;
struct ScanChain;
struct BankingCandidate;

// --------------------------------
// DPC 基本資料結構
// --------------------------------
struct DPCPoint {
    std::string instanceName;
    std::string cellType;
    std::string rowName;   // optional
    double x = 0;          // coord (database units or micron: caller保證一致)
    double y = 0;
    double width = 0;      // optional: for later packing
    double height = 0;

    double rho = 0;        // local density
    double delta = 0;      // dist to nearest higher density
    int nearestHigher = -1; // index into points_ vector

    int clusterId = -1;
    bool isCenter = false;
};

struct DPCCluster {
    int clusterId = -1;
    int centerIdx = -1;
    std::vector<int> members; // indices into points_

    // derived geometry
    double avgX = 0;
    double avgY = 0;
    double radius = 0;
    double minX = 0, maxX = 0, minY = 0, maxY = 0;

    // optional meta
    std::string clockNet;
    int scanChainIdx = -1;
};

struct DecisionPoint {
    int pointIndex;
    double rho;
    double delta;
    bool isCandidate;
};

struct DPCStatistics {
    int totalPoints = 0;
    int totalClusters = 0;
    double dc = 0;
    double avgClusterSize = 0;
    double avgClusterRadius = 0;
    std::vector<int> clusterSizes;
    std::vector<double> clusterRadii;
};

// --------------------------------
// DPC 類別
// --------------------------------
class DensityPeakClustering {
public:
    DensityPeakClustering();
    ~DensityPeakClustering();

    // 主流程（從 FlipFlopInfo 產生點並分群）
    void performClustering(const std::vector<FlipFlopInfo>& flipFlops, bool autoTune = true);

    // 針對單一 scan chain（需全域 FF lookup）
    void performClusteringOnScanChain(
        const ScanChain& chain,
        const std::map<std::string, FlipFlopInfo>& ffLookup,
        bool autoTune = true);

    // 直接對自備 DPC 點分群（最底層，不依賴 FlipFlopInfo）
    void performClusteringOnPoints(const std::vector<DPCPoint>& pts, bool autoTune = true);

    // 參數設定 / 查詢
    void setCutoffDistance(double dc) { cutoffDistance_ = dc; }
    void setDensityThreshold(double thr) { densityThreshold_ = thr; }
    void setDistanceThreshold(double thr) { distanceThreshold_ = thr; }
    void setUseGaussianKernel(bool b) { useGaussianKernel_ = b; }
    int estimateBestClusterCount() const;

    // 自動參數估計（可在外部手動呼叫）
    void autoTuneParameters();

    // 結果
    const std::vector<DPCPoint>& points() const { return points_; }
    const std::vector<DPCCluster>& clusters() const { return clusters_; }
    const DPCStatistics& stats() const { return statistics_; }

    // Decision Graph
    std::vector<DecisionPoint> getDecisionGraph() const;
    void exportDecisionGraphCSV(const std::string& filename) const;

    // 報告
    void printClusteringSummary() const;
    void printDetailedReport() const;
    void exportClustersCSV(const std::string& filename) const;

    // 評估
    double silhouetteScore() const;
    double daviesBouldinIndex() const;

    // 批次：對 clockDomain→scanChains 做分群
    using ClockScanMap = std::map<std::string, std::vector<ScanChain>>;
    std::map<std::string, std::vector<DPCCluster>>
        clusterByScanChain(const ClockScanMap& scanChains,
            const std::map<std::string, FlipFlopInfo>& ffLookup,
            bool autoTune = true);

    // Banking 候選（由分群結果推）
    std::vector<BankingCandidate> findBankingCandidatesInClusters() const;

    //read information
    void setMacroMap(const std::unordered_map<std::string, LefMacroInfo>* macroMap) {
        macroMap_ = macroMap;
    }
    void clear();

private:
    // 資料
    std::vector<DPCPoint> points_;
    std::vector<std::vector<double>> distMat_;
    std::vector<DPCCluster> clusters_;
    DPCStatistics statistics_;
    const std::unordered_map<std::string, LefMacroInfo>* macroMap_ = nullptr;

    // 參數
    double cutoffDistance_ = -1;
    double densityThreshold_ = -1;
    double distanceThreshold_ = -1;
    bool useGaussianKernel_ = true;

    // --- 私有流程 ---
    void loadPointsFromFF(const std::vector<FlipFlopInfo>&); // 座標轉換
    void buildDistanceMatrix();

    static double boxManhattanDistance(const DPCPoint& a, const DPCPoint& b);

    void computeRho();
    void computeDelta();
    std::vector<int> selectCenters(); // uses density/delta thresholds
    void assignClusters(const std::vector<int>& centers);
    void computeClusterGeometry();
    void computeStatistics();

    // helper
    double estimateOptimalCutoffDistance() const;
    std::pair<double, double> estimateThresholdsFromDecisionGraph() const;
};
