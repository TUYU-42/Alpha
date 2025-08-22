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
    std::string celltype; // optional: �Y�A���γo�ӰO�� MBFF ���O
    double avgX = 0.0, avgY = 0.0;
    double minX = 0.0, maxX = 0.0;
    double minY = 0.0, maxY = 0.0;
    double radius = 0.0;
};
// DPC.h
struct CkCapReport {
    int groupSize = 0;
    std::string sbCell;      // ← 舊欄位，為相容保留（會放第一個 family）
    std::string mbffCell;
    double ckCapBefore = 0.0;
    double ckCapAfter = 0.0;
    double ckCapSaving = 0.0;
    std::vector<std::string> members;

    // NEW: 支援混用型別的報表欄位
    std::vector<std::string> sbFamiliesUnique;                  // 這組出現過的 SB 家族（去重）
    std::vector<std::pair<std::string, int>> sbFamilyCounts;     // 各家族出現次數
    std::vector<double> sbMemberCkCaps;                         // 與 members 對齊的 per-SB CK cap
    double ckCapBeforeSum = 0.0;                                // Σ(CK_SB)
};

class DensityPeakClustering {
public:
    DensityPeakClustering();
    ~DensityPeakClustering();
    void dumpCkCapReport(const std::string& path) const;
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
    const std::vector<std::string>& getRemainingSingleBitFFs() const {
        return remainingSingleBitFFs;
    }
private:
    std::vector<std::string> remainingSingleBitFFs; // �ΨӦ����L�k�X�֩γQ���L�� single-bit FFs
    void performClusteringOnScanChain(
        const ScanChain& chain,
        const std::map<std::string, FlipFlopInfo>& ffLookup,
        bool autoTune);

    void performClustering(const std::vector<FlipFlopInfo>& flipFlops, bool autoTune);
    void loadPointsFromFF(const std::vector<FlipFlopInfo>& flipFlops);
    void buildDistanceMatrix();
    static double boxManhattanDistance(const DPCPoint& a, const DPCPoint& b);
    void computeRho();
    void computeDelta();
    std::vector<int> selectCenters();
    void assignClusters(const std::vector<int>& centerIndices);
    void computeClusterGeometry();
    int estimateBestClusterCount() const;
    double estimateOptimalCutoffDistance() const;
    std::vector<CkCapReport> ckCapReports_;
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
    std::vector<MergedFF> mergedFFResults_; // �x�s���R�᪺�X�ֵ��G
    long long int totalPlacedComponentCount_;// === Grid-neighbor mode (big-N) ===
    bool useGrid_ = false;
    double gridCell_ = 0.0;        // �������
    double rhoRadius_ = 0.0;       // �p�� rho ���I�_�b�| (�� 5*dc)
    double deltaStartRadius_ = 0.0;// �p�� delta ���_�l�b�| (�� 2*dc)

    std::unordered_map<long long, std::vector<int>> grid_; // (ix,iy)-> indices

    // helpers
    inline long long cellKey_(int ix, int iy) const {
        return ((long long)ix << 32) ^ (long long)(iy & 0xffffffff);
    }
    void buildGrid_(double cell);
    void rebuildGridIfNeeded_();
    std::vector<int> getCandidatesInRadius_(const DPCPoint& p, double radius) const;

    // streaming / grid versions
    void computeRhoGrid_();
    void computeDeltaGrid_();

    // sampling-based estimators (�קK�� distMat_)
    double estimateCutoffBySampling_(size_t samples = 200000) const;
    double estimateMaxDistBySampling_(size_t samples = 200000) const;


};