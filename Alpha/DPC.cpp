#include "DPC.h"
#include "DataStructures.h"
#include <cmath>
#include <iostream>

DensityPeakClustering::DensityPeakClustering() {}
DensityPeakClustering::~DensityPeakClustering() {}

// 批次?理：對每? clock domain 的 scan chains 進行分群
std::map<std::string, std::vector<DPCCluster>>
DensityPeakClustering::clusterByScanChain(
    const std::map<std::string, std::vector<ScanChainClustered>>& scanChains,
    const std::map<std::string, FlipFlopInfo>& ffLookup,
    bool autoTune)
{
    std::map<std::string, std::vector<DPCCluster>> result;

    // 對每? clock domain ?理
    for (std::map<std::string, std::vector<ScanChainClustered> >::const_iterator it = scanChains.begin();
        it != scanChains.end(); ++it) {
        const std::string& clockNet = it->first;
        const std::vector<ScanChainClustered>& chains = it->second;

        std::cout << "\n[DPC] Processing clock domain: " << clockNet
            << " with " << chains.size() << " scan chains" << std::endl;

        std::vector<DPCCluster> allClusters;

        for (size_t i = 0; i < chains.size(); ++i) {
            const ScanChainClustered& chain = chains[i];
            std::cout << "  Processing scan chain " << i
                << " (length=" << chain.length() << ")" << std::endl;

            clear();

            performClusteringOnScanChain(chain, ffLookup, autoTune);

            for (size_t j = 0; j < clusters_.size(); ++j) {
                clusters_[j].scanChainIdx = i;
                clusters_[j].clockNet = clockNet;
            }
            allClusters.insert(allClusters.end(), clusters_.begin(), clusters_.end());
        }

        result[clockNet] = allClusters;

        std::cout << "  Total clusters for " << clockNet << ": "
            << allClusters.size() << std::endl;
    }


    return result;
}

// 對單一 scan chain 進行分群
void DensityPeakClustering::performClusteringOnScanChain(
    const ScanChainClustered& chain,
    const std::map<std::string, FlipFlopInfo>& ffLookup,
    bool autoTune)
{
    // 從 scan chain 提取 FF 資?
    std::vector<FlipFlopInfo> ffList;

    for (const auto& node : chain.nodes) {
        auto it = ffLookup.find(node.instanceName);
        if (it != ffLookup.end()) {
            ffList.push_back(it->second);
        }
        else {
            std::cout << "[DPC] Warning: FF " << node.instanceName
                << " not found in lookup table" << std::endl;
        }
    }

    if (ffList.empty()) {
        std::cout << "[DPC] No valid FFs found in scan chain" << std::endl;
        return;
    }

    // 執行分群
    performClustering(ffList, autoTune);
}

// 對 FlipFlopInfo 列表進行分群
void DensityPeakClustering::performClustering(const std::vector<FlipFlopInfo>& flipFlops, bool autoTune) {
    clusters_.clear();
    loadPointsFromFF(flipFlops);

    if (points_.empty()) {
        std::cout << "[DPC] No points to cluster" << std::endl;
        return;
    }

    buildDistanceMatrix();

    // 自動調整參數
    if (autoTune || cutoffDistance_ <= 0) {
        cutoffDistance_ = estimateOptimalCutoffDistance();
        std::cout << "[DPC] cutoffDistance auto-set to " << cutoffDistance_ << std::endl;
    }

    computeRho();
    computeDelta();

    auto centerIndices = selectCenters();

    if (centerIndices.empty()) {
        std::cout << "[DPC] No cluster centers found" << std::endl;
        return;
    }

    assignClusters(centerIndices);
    computeClusterGeometry();
    computeStatistics();
}

// 從 FlipFlopInfo 載入點資料
void DensityPeakClustering::loadPointsFromFF(const std::vector<FlipFlopInfo>& flipFlops) {
    points_.clear();
    for (const auto& ff : flipFlops) {
        DPCPoint pt;
        pt.instanceName = ff.instName;
        pt.cellType = ff.cellType;
        pt.x = static_cast<double>(ff.x);
        pt.y = static_cast<double>(ff.y);

        // 從 macro map 取得尺寸資?
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
    std::cout << "[DPC] Loaded " << points_.size() << " points from flip-flop info" << std::endl;
}

// 建立距離矩?
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

// ?算兩? box 之間的曼哈頓距離
double DensityPeakClustering::boxManhattanDistance(const DPCPoint& a, const DPCPoint& b) {
    double ax0 = a.x, ax1 = a.x + a.width;
    double ay0 = a.y, ay1 = a.y + a.height;
    double bx0 = b.x, bx1 = b.x + b.width;
    double by0 = b.y, by1 = b.y + b.height;

    // 水平距離
    double dx = std::max(0.0, std::max(ax0, bx0) - std::min(ax1, bx1));
    // 垂直距離
    double dy = std::max(0.0, std::max(ay0, by0) - std::min(ay1, by1));

    return dx + dy;
}

// ?算局部密度 rho
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
            if (useGaussianKernel_) {
                points_[i].rho += std::exp(-(d * d) / (cutoffDistance_ * cutoffDistance_));
            }
            else {
                // Step function
                if (d < cutoffDistance_) {
                    points_[i].rho += 1.0;
                }
            }
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

// ?算到更高密度點的最小距離 delta
void DensityPeakClustering::computeDelta() {
    int N = points_.size();
    if (N == 0) return;

    // (1) 準備 rho 降序索引
    std::vector<int> sortedIdx(N);
    for (int i = 0; i < N; ++i) sortedIdx[i] = i;
    std::sort(sortedIdx.begin(), sortedIdx.end(),
        [this](int a, int b) { return points_[a].rho > points_[b].rho; });

    // (2) 預?理全場最大距離
    double maxDist = 0;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            if (i != j) maxDist = std::max(maxDist, distMat_[i][j]);

    // (3) 依序?算 delta, nearestHigher
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
            points_[idx].nearestHigher = -1;
        }
        else {
            points_[idx].delta = minDist;
            points_[idx].nearestHigher = nearestIdx;
        }
    }

    // debug: 印出delta?圍
    double minD = std::numeric_limits<double>::max(), maxD = -1;
    for (const auto& pt : points_) {
        minD = std::min(minD, pt.delta);
        maxD = std::max(maxD, pt.delta);
    }
    std::cout << "[DPC] delta range: " << minD << " ~ " << maxD << std::endl;
}

// 選擇聚?中心
std::vector<int> DensityPeakClustering::selectCenters() {
    std::vector<int> centerIndices;
    int N = points_.size();
    if (N == 0) return centerIndices;

    // --- Step 1. ?算 rho*delta 乘積 ---
    std::vector<std::pair<double, int>> rhoDeltaProduct;
    for (int i = 0; i < N; ++i) {
        double score = points_[i].rho * points_[i].delta;
        rhoDeltaProduct.emplace_back(score, i);
    }

    // --- Step 2. 依照 score 由大到小排序 ---
    std::sort(rhoDeltaProduct.rbegin(), rhoDeltaProduct.rend());

    // --- Step 3. 選 Top-K ?中心 ---
    int K = estimateBestClusterCount();
    if (K <= 0) K = std::min(8, N);      // 預設8群，或?點數決定

    for (int i = 0; i < K && i < N; ++i) {
        int idx = rhoDeltaProduct[i].second;
        points_[idx].isCenter = true;
        points_[idx].clusterId = idx;    // 用 index ? cluster id
        centerIndices.push_back(idx);

        // Debug log:
        std::cout << "[DPC] Center #" << i << " : " << points_[idx].instanceName
            << " (rho=" << points_[idx].rho
            << ", delta=" << points_[idx].delta
            << ", product=" << rhoDeltaProduct[i].first << ")\n";
    }

    return centerIndices;
}

// 估?最佳聚?數量
int DensityPeakClustering::estimateBestClusterCount() const {
    int N = points_.size();
    // ?單策略：每 4 ?點一? cluster
    return std::max(1, (N + 3) / 4);
}

// 估?最佳 cutoff distance
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

    // も笆 clamp1%N程10程50
    int targetNeighbor = N / 100;
    if (targetNeighbor < 10) targetNeighbor = 10;
    if (targetNeighbor > 50) targetNeighbor = 50;

    int cutoffIdx = std::min(targetNeighbor * N, (int)dists.size() - 1);
    return dists[cutoffIdx];
}


// 分配點到聚?
void DensityPeakClustering::assignClusters(const std::vector<int>& centers) {
    int N = points_.size();

    // 初始化聚?
    clusters_.clear();
    for (int centerIdx : centers) {
        DPCCluster cluster;
        cluster.clusterId = centerIdx;
        cluster.centerIdx = centerIdx;
        cluster.members.push_back(centerIdx);
        clusters_.push_back(cluster);
    }

    // 按密度降序?理非中心點
    std::vector<int> sortedIdx(N);
    for (int i = 0; i < N; ++i) sortedIdx[i] = i;
    std::sort(sortedIdx.begin(), sortedIdx.end(),
        [this](int a, int b) { return points_[a].rho > points_[b].rho; });

    // 分配每?點到其 nearestHigher 的聚?
    for (int idx : sortedIdx) {
        if (points_[idx].isCenter) continue;

        int higher = points_[idx].nearestHigher;
        if (higher >= 0 && points_[higher].clusterId >= 0) {
            points_[idx].clusterId = points_[higher].clusterId;

            // 找到對應的聚?並加入
            for (auto& cluster : clusters_) {
                if (cluster.clusterId == points_[idx].clusterId) {
                    cluster.members.push_back(idx);
                    break;
                }
            }
        }
    }
}

// ?算聚?的幾何特性
void DensityPeakClustering::computeClusterGeometry() {
    for (auto& cluster : clusters_) {
        if (cluster.members.empty()) continue;

        double sumX = 0, sumY = 0;
        cluster.minX = std::numeric_limits<double>::max();
        cluster.maxX = std::numeric_limits<double>::lowest();
        cluster.minY = std::numeric_limits<double>::max();
        cluster.maxY = std::numeric_limits<double>::lowest();

        for (int idx : cluster.members) {
            const auto& pt = points_[idx];
            sumX += pt.x;
            sumY += pt.y;
            cluster.minX = std::min(cluster.minX, pt.x);
            cluster.maxX = std::max(cluster.maxX, pt.x + pt.width);
            cluster.minY = std::min(cluster.minY, pt.y);
            cluster.maxY = std::max(cluster.maxY, pt.y + pt.height);
        }

        cluster.avgX = sumX / cluster.members.size();
        cluster.avgY = sumY / cluster.members.size();

        // ?算半徑（到中心的最大距離）
        double maxDist = 0;
        for (int idx : cluster.members) {
            double dx = points_[idx].x - cluster.avgX;
            double dy = points_[idx].y - cluster.avgY;
            maxDist = std::max(maxDist, std::sqrt(dx * dx + dy * dy));
        }
        cluster.radius = maxDist;
    }
}

// ?算統?資?
void DensityPeakClustering::computeStatistics() {
    statistics_.totalPoints = points_.size();
    statistics_.totalClusters = clusters_.size();
    statistics_.dc = cutoffDistance_;

    statistics_.clusterSizes.clear();
    statistics_.clusterRadii.clear();

    double sumSize = 0, sumRadius = 0;

    for (const auto& cluster : clusters_) {
        int size = cluster.members.size();
        statistics_.clusterSizes.push_back(size);
        statistics_.clusterRadii.push_back(cluster.radius);
        sumSize += size;
        sumRadius += cluster.radius;
    }

    if (!clusters_.empty()) {
        statistics_.avgClusterSize = sumSize / clusters_.size();
        statistics_.avgClusterRadius = sumRadius / clusters_.size();
    }
}

// 清空所有資料
void DensityPeakClustering::clear() {
    points_.clear();
    distMat_.clear();
    clusters_.clear();
    statistics_ = DPCStatistics();
}

// 列印聚?摘要
void DensityPeakClustering::printClusteringSummary() const {
    std::cout << "\n=== DPC Clustering Summary ===" << std::endl;
    std::cout << "Total points: " << statistics_.totalPoints << std::endl;
    std::cout << "Total clusters: " << statistics_.totalClusters << std::endl;
    std::cout << "Cutoff distance: " << statistics_.dc << std::endl;
    std::cout << "Average cluster size: " << statistics_.avgClusterSize << std::endl;
    std::cout << "Average cluster radius: " << statistics_.avgClusterRadius << std::endl;
}

// 列印??報告
void DensityPeakClustering::printDetailedReport() const {
    std::cout << "\n=== DPC Detailed Report ===" << std::endl;

    for (size_t i = 0; i < clusters_.size(); ++i) {
        const auto& cluster = clusters_[i];
        std::cout << "\nCluster " << i << " (ID=" << cluster.clusterId << "):" << std::endl;
        std::cout << "  Center: " << points_[cluster.centerIdx].instanceName << std::endl;
        std::cout << "  Size: " << cluster.members.size() << std::endl;
        std::cout << "  Center position: (" << cluster.avgX << ", " << cluster.avgY << ")" << std::endl;
        std::cout << "  Bounding box: [" << cluster.minX << "," << cluster.maxX
            << "] x [" << cluster.minY << "," << cluster.maxY << "]" << std::endl;
        std::cout << "  Radius: " << cluster.radius << std::endl;

        if (!cluster.clockNet.empty()) {
            std::cout << "  Clock net: " << cluster.clockNet << std::endl;
        }
        if (cluster.scanChainIdx >= 0) {
            std::cout << "  Scan chain index: " << cluster.scanChainIdx << std::endl;
        }
    }
}