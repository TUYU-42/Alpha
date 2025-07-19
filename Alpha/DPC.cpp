#include "dpc.h"
#include "DataStructures.h"
#include <iostream>

DensityPeakClustering::DensityPeakClustering() {}
DensityPeakClustering::~DensityPeakClustering() {}

std::map<std::string, std::vector<DPCCluster>>


DensityPeakClustering::clusterByScanChain(
    const std::map<std::string, std::vector<ScanChain>>& scanChains,
    const std::map<std::string, FlipFlopInfo>& ffLookup,
    bool autoTune)
{
    std::map<std::string, std::vector<DPCCluster>> result;
    for (const auto& [clockNet, chains] : scanChains) {
        std::vector<DPCCluster> allClusters;
        for (const auto& chain : chains) {
            // 針對每一條 scan chain 個別分群
            performClusteringOnScanChain(chain, ffLookup, autoTune);
            // 把 clusters_ 的內容取出，放進 allClusters
            allClusters.insert(allClusters.end(), clusters_.begin(), clusters_.end());
        }
        result[clockNet] = allClusters;
    }
    return result;
}

void DensityPeakClustering::performClusteringOnScanChain(
    const ScanChain& chain,
    const std::map<std::string, FlipFlopInfo>& ffLookup,
    bool autoTune)
{
    std::vector<FlipFlopInfo> ffList;
    for (const auto& node : chain.nodes) {
        auto it = ffLookup.find(node.instanceName);
        if (it != ffLookup.end()) {
            ffList.push_back(it->second);
        }
    }
    performClustering(ffList, autoTune); // 這行一定要加！！
}


void DensityPeakClustering::performClustering(const std::vector<FlipFlopInfo>& flipFlops, bool autoTune) {
    clusters_.clear();
    loadPointsFromFF(flipFlops);
    buildDistanceMatrix();
    cutoffDistance_ = estimateOptimalCutoffDistance();
    std::cout << "[DPC] cutoffDistance auto-set to " << cutoffDistance_ << std::endl;
    computeRho();
    computeDelta();
    auto centerIndices = selectCenters();
}

void DensityPeakClustering::loadPointsFromFF(const std::vector<FlipFlopInfo>& flipFlops) {
    points_.clear();
    for (const auto& ff : flipFlops) {
        DPCPoint pt;
        pt.instanceName = ff.instName;
        pt.cellType = ff.cellType;
        pt.x = static_cast<double>(ff.x);
        pt.y = static_cast<double>(ff.y);

        if (macroMap_ && macroMap_->count(pt.cellType)) {
            const auto& info = macroMap_->at(pt.cellType);
            pt.width = info.sizeX;
            pt.height = info.sizeY;
        }
        else {
            pt.width = 0;
            pt.height = 0;
        }
        points_.push_back(pt);
    }
    std::cout << "[DPC] Loaded " << points_.size() << " points from flip-flop info (with macroMap).\n";
}

void DensityPeakClustering::buildDistanceMatrix() {
    int N = points_.size();
    distMat_.assign(N, std::vector<double>(N, 0.0));
    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            double dist = boxManhattanDistance(points_[i], points_[j]);
            distMat_[i][j] = dist;
            distMat_[j][i] = dist;
        }
    }
}

double DensityPeakClustering::boxManhattanDistance(const DPCPoint& a, const DPCPoint& b) {
    double ax0 = a.x, ax1 = a.x + a.width;
    double ay0 = a.y, ay1 = a.y + a.height;
    double bx0 = b.x, bx1 = b.x + b.width;
    double by0 = b.y, by1 = b.y + b.height;
    // 水平
    double dx = std::max(0.0, std::max(ax0, bx0) - std::min(ax1, bx1));
    // 垂直
    double dy = std::max(0.0, std::max(ay0, by0) - std::min(ay1, by1));
    return dx + dy;
}

void DensityPeakClustering::computeRho() {
    int N = points_.size();
    if (N == 0) return;
    if (distMat_.empty() || distMat_.size() != N) buildDistanceMatrix();

    if (cutoffDistance_ <= 0) {
        cutoffDistance_ = estimateOptimalCutoffDistance();
        std::cout << "[DPC] Auto-set cutoffDistance = " << cutoffDistance_ << std::endl;
    }

    for (int i = 0; i < N; ++i) {
        points_[i].rho = 0.0;
        for (int j = 0; j < N; ++j) {
            if (i == j) continue;
            double d = distMat_[i][j];
            points_[i].rho += std::exp(-(d * d) / (cutoffDistance_ * cutoffDistance_));
        }
    }

    // 顯示 max/min 供 debug
    double minRho = std::numeric_limits<double>::max();
    double maxRho = std::numeric_limits<double>::lowest();
    for (const auto& pt : points_) {
        minRho = std::min(minRho, pt.rho);
        maxRho = std::max(maxRho, pt.rho);
    }
    std::cout << "[DPC] Local density rho range: " << minRho << " ~ " << maxRho << std::endl;
}

void DensityPeakClustering::computeDelta() {
    int N = points_.size();
    if (N == 0) return;

    // (1) 準備 rho 降序索引
    std::vector<int> sortedIdx(N);
    for (int i = 0; i < N; ++i) sortedIdx[i] = i;
    std::sort(sortedIdx.begin(), sortedIdx.end(),
        [this](int a, int b) { return points_[a].rho > points_[b].rho; });

    // (2) 預處理全場最大距離
    double maxDist = 0;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            if (i != j) maxDist = std::max(maxDist, distMat_[i][j]);

    // (3) 依序計算 delta, nearestHigher
    for (int rank = 0; rank < N; ++rank) {
        int idx = sortedIdx[rank];
        double myRho = points_[idx].rho;
        double minDist = std::numeric_limits<double>::max();
        int nearestIdx = -1;
        // 比自己 rho 高的只有前面（rank 0 ~ rank-1）
        for (int r = 0; r < rank; ++r) {
            int jdx = sortedIdx[r];
            double d = distMat_[idx][jdx];
            if (d < minDist) {
                minDist = d;
                nearestIdx = jdx;
            }
        }
        if (rank == 0) {
            // 全場最高密度
            points_[idx].delta = maxDist;
            points_[idx].nearestHigher = -1; // 沒有高於它的
        }
        else {
            points_[idx].delta = minDist;
            points_[idx].nearestHigher = nearestIdx;
        }
    }

    // debug: 印出delta範圍
    double minD = std::numeric_limits<double>::max(), maxD = -1;
    for (const auto& pt : points_) {
        minD = std::min(minD, pt.delta);
        maxD = std::max(maxD, pt.delta);
    }
    std::cout << "[DPC] delta range: " << minD << " ~ " << maxD << std::endl;
}


std::vector<int> DensityPeakClustering::selectCenters() {
    std::vector<int> centerIndices;
    int N = points_.size();
    if (N == 0) return centerIndices;

    // --- Step 1. 計算 rho*delta 乘積 ---
    std::vector<std::pair<double, int>> rhoDeltaProduct;
    for (int i = 0; i < N; ++i) {
        double score = points_[i].rho * points_[i].delta;
        rhoDeltaProduct.emplace_back(score, i);
    }

    // --- Step 2. 依照 score 由大到小排序 ---
    std::sort(rhoDeltaProduct.rbegin(), rhoDeltaProduct.rend());

    // --- Step 3. 選 Top-K 當中心 ---
    int K = estimateBestClusterCount();  // 你可以根據經驗指定 K，也可手動傳參數
    if (K <= 0) K = std::min(8, N);      // 預設8群，或視點數決定

    for (int i = 0; i < K; ++i) {
        int idx = rhoDeltaProduct[i].second;
        points_[idx].isCenter = true;
        points_[idx].clusterId = idx;    // 用 index 當 cluster id
        centerIndices.push_back(idx);
        // Debug log:
        std::cout << "[DPC] Center #" << i << " : " << points_[idx].instanceName
            << " (rho=" << points_[idx].rho
            << ", delta=" << points_[idx].delta
            << ", product=" << rhoDeltaProduct[i].first << ")\n";
    }

    // 若想用 threshold（例如 delta > δ_thr & rho > ρ_thr），可用這個寫法：
    /*
    double rho_thr = ...;    // 可外部 set 或自動算
    double delta_thr = ...;  // 可外部 set 或自動算
    for (int i = 0; i < N; ++i) {
        if (points_[i].rho > rho_thr && points_[i].delta > delta_thr) {
            points_[i].isCenter = true;
            points_[i].clusterId = i;
            centerIndices.push_back(i);
        }
    }
    */

    return centerIndices;
}

int DensityPeakClustering::estimateBestClusterCount() const {
    int N = points_.size();
    return (N + 3) / 4; // ceil(N/4)
}

double DensityPeakClustering::estimateOptimalCutoffDistance() const {
    if (distMat_.empty() || points_.size() < 2)
        return 10.0;
    std::vector<double> dists;
    int N = points_.size();
    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            dists.push_back(distMat_[i][j]);
        }
    }
    if (dists.empty()) return 10.0;
    std::sort(dists.begin(), dists.end());

    // 想要每個點平均有 targetNeighbor 個鄰居
    int targetNeighbor = std::clamp(N / 100, 10, 50); // 1%N，最少10，最多50
    int cutoffIdx = std::min(targetNeighbor * N, (int)dists.size() - 1);
    return dists[cutoffIdx];
}
