#include "dpc.h"
#include "DataStructures.h"
#include "ParserDEF.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <tuple>
#include <numeric>
#include <unordered_set>
// 放在檔案頂端（.cpp/.h），包含必要的 header




DensityPeakClustering::DensityPeakClustering() {}
DensityPeakClustering::~DensityPeakClustering() {}

std::map<std::string, std::vector<DPCCluster>>
DensityPeakClustering::clusterByScanChain(
    const std::map<std::string, std::vector<ScanChain>>& scanChains,
    const std::map<std::string, FlipFlopInfo>& ffLookup,
    bool autoTune)
{
    std::map<std::string, std::vector<DPCCluster>> result;
    for (std::map<std::string, std::vector<ScanChain>>::const_iterator it = scanChains.begin(); it != scanChains.end(); ++it) {
        const std::string& clockNet = it->first;
        const std::vector<ScanChain>& chains = it->second;

        std::vector<DPCCluster> allClusters;
        for (std::vector<ScanChain>::const_iterator cit = chains.begin(); cit != chains.end(); ++cit) {
            // 針對每一條 scan chain 個別分群
            performClusteringOnScanChain(*cit, ffLookup, autoTune);
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
    for (const auto& name : chain.ffNames) {
        auto it = ffLookup.find(name);
        if (it != ffLookup.end()) {
            ffList.push_back(it->second);
        }
    }
    performClustering(ffList, autoTune); // 這行一定要加！！
}


void DensityPeakClustering::performClustering(const std::vector<FlipFlopInfo>& flipFlops, bool autoTune) {
    clusters_.clear();
    loadPointsFromFF(flipFlops);

    const int N = (int)points_.size();
    // 依點數切換：門檻你可調（6000~10000 都可）
    useGrid_ = (N > 8000);

    if (!useGrid_) {
        // 小 N：維持你原本的做法（結果完全一致）
        buildDistanceMatrix();
        cutoffDistance_ = estimateOptimalCutoffDistance();
        std::cout << "[DPC] cutoffDistance auto-set to " << cutoffDistance_ << std::endl;
        computeRho();
        computeDelta();
    }
    else {
        // 大 N：改採取不建矩陣、近鄰網格
        cutoffDistance_ = estimateCutoffBySampling_(); // 抽樣估 dc
        std::cout << "[DPC][grid] cutoffDistance auto-set to " << cutoffDistance_ << std::endl;

        // 網格與半徑設定：經驗值
        gridCell_ = std::max(1.0, 2.0 * cutoffDistance_);
        rhoRadius_ = std::max(1.0, 5.0 * cutoffDistance_);
        deltaStartRadius_ = std::max(1.0, 2.0 * cutoffDistance_);

        buildGrid_(gridCell_);
        computeRhoGrid_();
        computeDeltaGrid_();
    }

    auto centerIndices = selectCenters();
    assignClusters(centerIndices);
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
   // int K = estimateBestClusterCount();  // 你可以根據經驗指定 K，也可手動傳參數
    //if (K <= 0) K = std::min(8, N);      // 預設8群，或視點數決定
    //int K = 3;

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

    // 替代 std::clamp 的寫法
    int targetNeighbor = N / 100;
    if (targetNeighbor < 10) targetNeighbor = 10;
    if (targetNeighbor > 50) targetNeighbor = 50;

    int cutoffIdx = std::min(targetNeighbor * N, static_cast<int>(dists.size()) - 1);
    return dists[cutoffIdx];
}



void DensityPeakClustering::assignClusters(const std::vector<int>& centerIndices) {
    int N = points_.size();

    if (N == 0) return;

    // 初始化中心點的 clusterId
    for (size_t i = 0; i < centerIndices.size(); ++i) {
        int idx = centerIndices[i];
        points_[idx].clusterId = idx;
    }

    // 按照 rho 降序排序
    std::vector<int> sortedIdx(N);
    for (int i = 0; i < N; ++i) sortedIdx[i] = i;

    std::sort(sortedIdx.begin(), sortedIdx.end(),
        [this](int a, int b) { return points_[a].rho > points_[b].rho; });

    for (size_t i = 0; i < sortedIdx.size(); ++i) {
        int idx = sortedIdx[i];
        if (points_[idx].isCenter) continue;
        int parent = points_[idx].nearestHigher;
        if (parent != -1) {
            points_[idx].clusterId = points_[parent].clusterId;
        }
    }

    // 建立 clusterMap
    std::unordered_map<int, DPCCluster> clusterMap;
    for (int i = 0; i < N; ++i) {
        int cid = points_[i].clusterId;
        if (cid == -1) continue;
        clusterMap[cid].members.push_back(i);
    }

    // 製作 clusters_
    clusters_.clear();
    for (std::unordered_map<int, DPCCluster>::iterator it = clusterMap.begin(); it != clusterMap.end(); ++it) {
        int cid = it->first;
        DPCCluster& cluster = it->second;
        cluster.clusterId = cid;

        // 找出中心點 index
        cluster.centerIdx = -1;
        for (size_t j = 0; j < cluster.members.size(); ++j) {
            int idx = cluster.members[j];
            if (points_[idx].isCenter) {
                cluster.centerIdx = idx;
                break;
            }
        }
        if (cluster.centerIdx == -1 && !cluster.members.empty()) {
            cluster.centerIdx = cluster.members[0];
        }

        // 設定 celltype
        cluster.celltype = points_[cluster.centerIdx].cellType;

        clusters_.push_back(cluster);
    }

    if (clusters_.empty()) {
        std::cerr << "[DPC] Warning: No clusters assigned! Check if selectCenters() returned any.\n";
    }

    // 建立 cluster 查表
    instanceToClusterId_.clear();
    clusterIdToCluster_.clear();
    for (size_t i = 0; i < clusters_.size(); ++i) {
        const DPCCluster& cluster = clusters_[i];
        clusterIdToCluster_[cluster.clusterId] = cluster;
        for (size_t j = 0; j < cluster.members.size(); ++j) {
            int idx = cluster.members[j];
            if (idx >= 0 && idx < static_cast<int>(points_.size())) {
                instanceToClusterId_[points_[idx].instanceName] = cluster.clusterId;
            }
        }
    }

    std::cout << "[DPC] Assigned " << clusters_.size() << " clusters.\n";
    computeClusterGeometry();
}



void DensityPeakClustering::computeClusterGeometry() {
    for (auto& cluster : clusters_) {
        double sumX = 0, sumY = 0;
        double minX = std::numeric_limits<double>::max();
        double maxX = std::numeric_limits<double>::lowest();
        double minY = std::numeric_limits<double>::max();
        double maxY = std::numeric_limits<double>::lowest();

        for (int idx : cluster.members) {
            const auto& pt = points_[idx];
            sumX += pt.x;
            sumY += pt.y;
            minX = std::min(minX, pt.x);
            maxX = std::max(maxX, pt.x);
            minY = std::min(minY, pt.y);
            maxY = std::max(maxY, pt.y);
        }

        int N = cluster.members.size();
        if (N > 0) {
            cluster.avgX = sumX / N;
            cluster.avgY = sumY / N;
        }
        cluster.minX = minX;
        cluster.maxX = maxX;
        cluster.minY = minY;
        cluster.maxY = maxY;

        // Radius 為所有成員點到中心點的最大距離
        double maxR = 0;
        for (int idx : cluster.members) {
            const auto& pt = points_[idx];
            double dx = pt.x - cluster.avgX;
            double dy = pt.y - cluster.avgY;
            maxR = std::max(maxR, std::sqrt(dx * dx + dy * dy));
        }
        cluster.radius = maxR;
    }
}

void DensityPeakClustering::printClusteringSummary() const {
    if (clusters_.empty()) {
        std::cerr << "[DPC] No clusters found when printing summary!\n";
    }

    std::ofstream fout("DPC_ClusteringSummary.txt");
    std::ostream& out = fout.is_open() ? fout : std::cout;

    out << "=== DPC Clustering Summary ===\n";
    out << "Total points clustered: " << points_.size() << "\n";
    out << "Total clusters formed : " << clusters_.size() << "\n\n";



    for (const auto& cluster : clusters_) {
        if (cluster.centerIdx >= 0 && cluster.centerIdx < points_.size()) {
            const auto& centerPt = points_[cluster.centerIdx];
            out << "Cluster ID    : " << cluster.clusterId << "\n";
            out << "  Center      : " << centerPt.instanceName << "\n";
        }
        else {
            out << "Cluster ID    : " << cluster.clusterId << "\n";
            out << "  Center      : [Invalid index: " << cluster.centerIdx << "]\n";
        }

        out << "  Members     : " << cluster.members.size() << "-->" << cluster.celltype << "\n";
        out << "  Member List : [ ";
        for (int idx : cluster.members) {
            if (idx >= 0 && idx < points_.size()) {
                out << points_[idx].instanceName << "-->" << points_[idx].cellType << " ";
            }
        }
        out << "]\n";

        out << "  Avg Coord   : (" << cluster.avgX << ", " << cluster.avgY << ")\n";
        out << "  Radius      : " << cluster.radius << "\n";
        out << "  BoundingBox : ["
            << cluster.minX << ", " << cluster.maxX << "] x ["
            << cluster.minY << ", " << cluster.maxY << "]\n";
        /*if (libParser_) {
            std::map<std::string, std::vector<std::string>> sbffToMBFFs;
            for (const auto& pt : points_) {
                const LibCell* cell = libParser_->getCell(pt.cellType);
                if (cell) {
                    if (!cell->singleBitDegenerate.empty()) {
                        sbffToMBFFs[cell->singleBitDegenerate].push_back(pt.cellType);
                        std::cout << "[DPC Debug] " << pt.instanceName << " (" << pt.cellType
                            << ") 退化為 " << cell->singleBitDegenerate << "\n";
                    }
                    else {
                        std::cout << "[DPC Debug] " << pt.instanceName << " (" << pt.cellType
                            << ") 沒有退化設定\n";
                    }
                }
                else {
                    std::cout << "[DPC Debug] " << pt.instanceName << " 無法在 libParser 中找到對應 cell: " << pt.cellType << "\n";
                }
            }
            out << "\n=== MBFF Reverse Mapping ===\n";
            for (const auto& [sbff, mbffs] : sbffToMBFFs) {
                out << "Single-bit FF \"" << sbff << "\" → 可升級為: ";
                for (const auto& mb : mbffs) {
                    out << mb << " ";
                }
                out << "\n";
            }
            out << "-----------------------------------\n";
        }*/


        out << "-----------------------------------\n";
    }
    /* std::string targetFF = "SNPSLOPT25_FSDN_V2_1";

    for (const auto& cluster : clusters_) {
        int targetCount = 0;

        // Step 1: 統計該 cluster 中目標 cellType 出現次數
        for (int idx : cluster.members) {
            if (idx >= 0 && idx < points_.size()) {
                if (points_[idx].cellType == targetFF) {
                    targetCount++;
                }
            }
        }

        // Step 2: 若出現次數 >= 2，才印出這個 cluster
        if (targetCount >= 2) {
            out << "Cluster ID    : " << cluster.clusterId << "\n";
            if (cluster.centerIdx >= 0 && cluster.centerIdx < points_.size()) {
                const auto& centerPt = points_[cluster.centerIdx];
                out << "  Center      : " << centerPt.instanceName << "\n";
            }
            else {
                out << "  Center      : [Invalid index: " << cluster.centerIdx << "]\n";
            }

            out << "  Members     : " << cluster.members.size() << " --> " << cluster.celltype << "\n";

            // Optional：印出成員清單
            out << "  Member List : [ ";
            for (int idx : cluster.members) {
                if (idx >= 0 && idx < points_.size()) {
                    const auto& pt = points_[idx];
                    out << pt.instanceName << "-->" << pt.cellType << " ";
                }
            }
            out << "]\n";

            out << "  ⚠️  Found " << targetCount << " instances of " << targetFF << " in this cluster!\n";
            out << "  Avg Coord   : (" << cluster.avgX << ", " << cluster.avgY << ")\n";
            out << "  Radius      : " << cluster.radius << "\n";
            out << "  BoundingBox : [" << cluster.minX << ", " << cluster.maxX
                << "] x [" << cluster.minY << ", " << cluster.maxY << "]\n";
            out << "-----------------------------------\n";
        }
    }*/

    if (fout.is_open()) {
        std::cout << "[DPC] Clustering summary saved to DPC_ClusteringSummary.txt\n";
        fout.close();
    }
}
std::map<std::string, std::vector<DPCCluster>>
DensityPeakClustering::clusterByClockNet(
    const std::map<std::string, FlipFlopInfo>& ffLookup,
    bool autoTune)
{
    // Step 1: 將 flip-flops 依 clockNet 分組
    std::map<std::string, std::vector<FlipFlopInfo>> clockToFFs;
    for (std::map<std::string, FlipFlopInfo>::const_iterator it = ffLookup.begin(); it != ffLookup.end(); ++it) {
        const FlipFlopInfo& ff = it->second;
        clockToFFs[ff.clockNet].push_back(ff);
    }

    // Step 2: 對每個 clockNet 進行 clustering
    std::map<std::string, std::vector<DPCCluster>> result;
    for (std::map<std::string, std::vector<FlipFlopInfo> >::const_iterator it = clockToFFs.begin(); it != clockToFFs.end(); ++it) {
        const std::string& clockNet = it->first;
        const std::vector<FlipFlopInfo>& ffList = it->second;

        performClustering(ffList, autoTune);    // 核心分群
        result[clockNet] = clusters_;           // clusters_ 是成員變數
        std::cout << "[DPC] ClockNet " << clockNet
            << " clustered into " << clusters_.size() << " clusters.\n";
    }

    return result;
}

int DensityPeakClustering::getClusterIdForInstance(const std::string& name) const {
    auto it = instanceToClusterId_.find(name);
    return (it != instanceToClusterId_.end()) ? it->second : -1;
}

const DPCCluster* DensityPeakClustering::getClusterById(int cid) const {
    auto it = clusterIdToCluster_.find(cid);
    return (it != clusterIdToCluster_.end()) ? &(it->second) : nullptr;
}





// 統一處理 Single-Bit FF Merge with fallback width logic，包含距離優化群組選擇

void DensityPeakClustering::analyzeSingleBitMergeCandidates() {
    // local collection (不要改 header，改成 local)
    std::vector<std::string> remainingSingleBitFFs;

    mergeMap_.clear();
    mergedFFResults_.clear();

    if (!libParser_) {
        std::cerr << "[DPC] Error: libParser is not set!" << std::endl;
        return;
    }

    std::cout << "\n=== [DPC] Analyze Single-Bit FF Merge Candidates (4-bit priority, greedy, no-duplicate) ===" << std::endl;

    const auto& allCells = libParser_->getAllCells();
    int totalClustersWithMerges = 0;
    int totalMergePairs = 0;
    int fourbitFF = 0;
    int twobitFF = 0;

    // track globally-used single-bit instances so each instance is merged at most once
    std::unordered_set<std::string> usedInstances;

    auto getBitWidthFromName = [](const std::string& name) -> int {
        if (name.find("16_") != std::string::npos) return 16;
        if (name.find("8_") != std::string::npos) return 8;
        if (name.find("4_") != std::string::npos) return 4;
        if (name.find("2_") != std::string::npos) return 2;
        return 1;
        };

    auto distSq = [](double x1, double y1, double x2, double y2) -> double {
        double dx = x1 - x2;
        double dy = y1 - y2;
        return dx * dx + dy * dy;
        };

    auto calcGroupCenterFromCoords = [&](const std::vector<std::string>& group,
        const std::map<std::string, std::pair<double, double>>& coords)
        -> std::pair<int, int> {
        double sumX = 0, sumY = 0;
        int count = 0;
        for (const auto& name : group) {
            auto cit = coords.find(name);
            if (cit != coords.end()) {
                sumX += cit->second.first;
                sumY += cit->second.second;
                ++count;
            }
            else {
                std::cerr << "[DPC] Warning: coord not found for " << name << "\n";
            }
        }
        if (count == 0) return { 0, 0 };
        return { static_cast<int>(sumX / count), static_cast<int>(sumY / count) };
        };

    // GREEDY grouping: pick best seed (min sum of nearest neighbors), then take nearest (bitSize-1) neighbors
    auto findGroupsRemoveUsedGreedy = [&](std::vector<std::string>& tmpList, int bitSize,
        const std::map<std::string, std::pair<double, double>>& coords)
        -> std::vector<std::vector<std::string>> {
        std::vector<std::vector<std::string>> groups;
        // filter tmpList to those with coords
        std::vector<std::string> filtered;
        for (auto& s : tmpList) if (coords.find(s) != coords.end()) filtered.push_back(s);
        tmpList = filtered;

        while (tmpList.size() >= static_cast<size_t>(bitSize)) {
            int n = static_cast<int>(tmpList.size());
            // find best seed: compute sum of nearest (bitSize-1) distances for each candidate
            double bestSeedScore = std::numeric_limits<double>::infinity();
            int bestSeedIdx = 0;
            for (int i = 0; i < n; ++i) {
                std::vector<double> dists; dists.reserve(n - 1);
                for (int j = 0; j < n; ++j) if (i != j) {
                    const auto& A = coords.at(tmpList[i]);
                    const auto& B = coords.at(tmpList[j]);
                    dists.push_back(distSq(A.first, A.second, B.first, B.second));
                }
                if (dists.empty()) continue;
                std::nth_element(dists.begin(), dists.begin() + std::min((int)dists.size(), bitSize - 1), dists.end());
                double s = 0;
                int take = std::min((int)dists.size(), bitSize - 1);
                for (int k = 0; k < take; ++k) s += dists[k];
                if (s < bestSeedScore) { bestSeedScore = s; bestSeedIdx = i; }
            }

            // build group from seed + its nearest neighbors
            std::vector<std::pair<double, int>> neigh; neigh.reserve(n - 1);
            for (int j = 0; j < n; ++j) if (j != bestSeedIdx) {
                const auto& A = coords.at(tmpList[bestSeedIdx]);
                const auto& B = coords.at(tmpList[j]);
                neigh.emplace_back(distSq(A.first, A.second, B.first, B.second), j);
            }
            std::sort(neigh.begin(), neigh.end());
            std::vector<std::string> group;
            group.push_back(tmpList[bestSeedIdx]);
            int need = bitSize - 1;
            for (int k = 0; k < need && k < (int)neigh.size(); ++k)
                group.push_back(tmpList[neigh[k].second]);

            // if we couldn't gather enough neighbors (shouldn't happen due to while condition), break
            if (group.size() < static_cast<size_t>(bitSize)) break;

            // push group and remove used
            groups.push_back(group);
            for (const auto& s : group) tmpList.erase(std::remove(tmpList.begin(), tmpList.end(), s), tmpList.end());
        }
        return groups;
        };

    // iterate clusters_
    for (const auto& cluster : clusters_) {
        // build sbffToInstances and coords, but skip instances already used
        std::map<std::string, std::vector<std::string>> sbffToInstances;
        std::map<std::string, std::pair<double, double>> coords;
        for (int idx : cluster.members) {
            const auto& pt = points_[idx];
            // skip if already merged elsewhere
            if (usedInstances.find(pt.instanceName) != usedInstances.end()) continue;

            std::string sbff = libParser_->getSingleBitDegenerate(pt.cellType);
            if (sbff.empty()) sbff = pt.cellType;
            sbffToInstances[sbff].push_back(pt.instanceName);
            coords[pt.instanceName] = { pt.x, pt.y };
        }

        bool clusterPrinted = false;

        for (auto& kv : sbffToInstances) {
            const std::string& sbff = kv.first;
            const auto& instListRawAll = kv.second;

            // filter out any that became used since grouping (conservative)
            std::vector<std::string> instListRaw;
            instListRaw.reserve(instListRawAll.size());
            for (const auto& nm : instListRawAll) if (usedInstances.find(nm) == usedInstances.end()) instListRaw.push_back(nm);

            if (instListRaw.size() < 2) {
                // add remaining that are not used
                for (const auto& nm : instListRaw) remainingSingleBitFFs.push_back(nm);
                continue;
            }

            // group by path prefix
            std::map<std::string, std::vector<std::string>> prefixGroups;
            for (const auto& inst : instListRaw) {
                auto pos = inst.find_last_of('/');
                std::string prefix = (pos != std::string::npos ? inst.substr(0, pos) : "");
                prefixGroups[prefix].push_back(inst);
            }

            for (auto& pg : prefixGroups) {
                // filter out used in this prefix group too (defensive)
                std::vector<std::string> instList;
                instList.reserve(pg.second.size());
                for (const auto& nm : pg.second) if (usedInstances.find(nm) == usedInstances.end()) instList.push_back(nm);

                if (instList.size() < 2) {
                    for (const auto& nm : instList) remainingSingleBitFFs.push_back(nm);
                    continue;
                }

                if (!clusterPrinted) {
                    std::cout << "\nCluster #" << cluster.clusterId << ":\n";
                    clusterPrinted = true;
                    totalClustersWithMerges++;
                }

                // find MBFF candidates
                std::vector<std::string> mbffCandidates;
                for (const auto& cellIt : allCells) {
                    if (cellIt.second.singleBitDegenerate == sbff)
                        mbffCandidates.push_back(cellIt.first);
                }
                if (mbffCandidates.empty()) {
                    remainingSingleBitFFs.insert(remainingSingleBitFFs.end(), instList.begin(), instList.end());
                    continue;
                }

                // before grouping, ensure tmpList excludes globally-used instances (defensive)
                std::vector<std::string> tmpList;
                tmpList.reserve(instList.size());
                for (const auto& nm : instList) if (usedInstances.find(nm) == usedInstances.end()) tmpList.push_back(nm);

                // prioritize 4-bit then 2-bit using greedy grouping that removes used instances from tmpList
                auto groups4 = findGroupsRemoveUsedGreedy(tmpList, 4, coords);
                // ensure groups do not contain already-used instances (shouldn't, but double-check)
                for (auto& g : groups4) {
                    bool ok = true;
                    for (const auto& mbr : g) if (usedInstances.find(mbr) != usedInstances.end()) { ok = false; break; }
                    if (!ok) continue;
                }
                auto groups2 = findGroupsRemoveUsedGreedy(tmpList, 2, coords);

                auto handleGroups = [&](const std::vector<std::vector<std::string>>& groups, int bit) {
                    for (const auto& group : groups) {
                        // skip any group if any member already used (defensive)
                        bool anyUsed = false;
                        for (const auto& mbr : group) if (usedInstances.find(mbr) != usedInstances.end()) { anyUsed = true; break; }
                        if (anyUsed) continue;

                        std::string bestFF;
                        //double bestScore = std::numeric_limits<double>::infinity(); // 不再使用原加權分數

                        // 替換後的選擇邏輯：按你要求的規則（beta/gamma 比較與 tie-breaker）
                        double alpha = weights_.Alpha;
                        double beta = weights_.Beta;
                        double gamma = weights_.Gamma;

                        auto chooseByAreaThenPower = [&](const std::vector<std::string>& cands) -> std::string {
                            std::string best;
                            double bestArea = std::numeric_limits<double>::infinity();
                            double bestPower = std::numeric_limits<double>::infinity();
                            for (const auto& mbff : cands) {
                                if (getBitWidthFromName(mbff) != bit) continue;
                                auto cit = allCells.find(mbff);
                                if (cit == allCells.end()) continue;
                                const auto& cell = cit->second;
                                if (cell.area < bestArea || (cell.area == bestArea && cell.cellLeakagePower < bestPower)) {
                                    bestArea = cell.area;
                                    bestPower = cell.cellLeakagePower;
                                    best = mbff;
                                }
                            }
                            return best;
                            };

                        auto chooseByPowerThenArea = [&](const std::vector<std::string>& cands) -> std::string {
                            std::string best;
                            double bestPower = std::numeric_limits<double>::infinity();
                            double bestArea = std::numeric_limits<double>::infinity();
                            for (const auto& mbff : cands) {
                                if (getBitWidthFromName(mbff) != bit) continue;
                                auto cit = allCells.find(mbff);
                                if (cit == allCells.end()) continue;
                                const auto& cell = cit->second;
                                if (cell.cellLeakagePower < bestPower || (cell.cellLeakagePower == bestPower && cell.area < bestArea)) {
                                    bestPower = cell.cellLeakagePower;
                                    bestArea = cell.area;
                                    best = mbff;
                                }
                            }
                            return best;
                            };

                        // Decide strategy according to weights
                        std::string bestFF_cand;
                        if (beta == 0.0) {
                            // Beta = 0 -> choose area smallest (tie-breaker power)
                            bestFF_cand = chooseByAreaThenPower(mbffCandidates);
                        }
                        else if (gamma == 0.0) {
                            // Gamma = 0 -> choose power smallest (tie-breaker area)
                            bestFF_cand = chooseByPowerThenArea(mbffCandidates);
                        }
                        else if (alpha > 10000*beta) {
                            bestFF_cand = chooseByPowerThenArea(mbffCandidates);
                        }
                        else if (gamma >= 30000.0 * beta) {
                            // Gamma >> Beta (>= 3000x) -> prioritize area
                            bestFF_cand = chooseByAreaThenPower(mbffCandidates);
                        }
                        else if (beta >= 30000.0 * gamma) {
                            // Beta >> Gamma (>= 3000x) -> prioritize power
                            bestFF_cand = chooseByPowerThenArea(mbffCandidates);
                        }
                        else {
                            // Default (weights comparable) -> 遵從 "不然一律選 power 小"
                            bestFF_cand = chooseByPowerThenArea(mbffCandidates);
                        }

                        if (!bestFF_cand.empty()) {
                            bestFF = bestFF_cand;
                        }

                        if (bestFF.empty()) continue;

                        totalMergePairs++;
                        MergedFF merged;
                        merged.mbffType = bestFF;
                        merged.bitwidth = bit;
                        merged.mergedFFs = group;
                        std::string base = "merged_" + std::to_string(totalMergePairs);
                        merged.newInstanceName = (pg.first.empty() ? base : pg.first + "/" + base);

                        auto cen = calcGroupCenterFromCoords(group, coords);
                        merged.newX = cen.first;
                        merged.newY = cen.second;

                        auto cellIt = allCells.find(bestFF);
                        if (cellIt != allCells.end()) {
                            merged.power = cellIt->second.cellLeakagePower;
                            merged.area = cellIt->second.area;
                        }
                        else {
                            merged.power = 0.0; merged.area = 0.0;
                        }

                        // simple D/Q mapping
                        for (int i = 0; i < (int)merged.mergedFFs.size(); ++i) {
                            std::string d_pin = "D" + std::to_string(i);
                            std::string q_pin = "Q" + std::to_string(i);
                            merged.mbffPinToOrigPin[d_pin] = merged.mergedFFs[i] + "/D";
                            merged.mbffPinToOrigPin[q_pin] = merged.mergedFFs[i] + "/Q";
                            merged.mbffPinToOrigFF[d_pin] = merged.mergedFFs[i];
                            merged.mbffPinToOrigFF[q_pin] = merged.mergedFFs[i];
                        }

                        // record merged and mark members as used (prevent duplicates globally)
                        mergedFFResults_.push_back(merged);
                        for (const auto& mbr : merged.mergedFFs) usedInstances.insert(mbr);

                        if (bit == 4) ++fourbitFF; else if (bit == 2) ++twobitFF;
                    }
                    };

                handleGroups(groups4, 4);
                handleGroups(groups2, 2);
            }
        }
    }

    // build mergeMap
    for (const auto& m : mergedFFResults_) {
        for (const auto& s : m.mergedFFs) {
            mergeMap_.addMapping(s, m.newInstanceName);
        }
    }

    std::cout << "\n[DPC] Found " << mergedFFResults_.size()
        << " merged instances. (4-bit: " << fourbitFF << ", 2-bit: " << twobitFF << ")\n";
}




void DensityPeakClustering::reportMergedFFResults() const {
    std::cout << "\n=== [DPC] Merged FF Summary ===\n";
    if (mergedFFResults_.empty()) {
        std::cout << "No merged FF results available.\n";
        return;
    }
    int compcount = 0;
    for (const auto& merged : mergedFFResults_) {
        std::cout << "Merged Instance: " << merged.newInstanceName << "\n";
        std::cout << "  MBFF Type: " << merged.mbffType << "\n";
        std::cout << "  Bitwidth: " << merged.bitwidth << "\n";
        std::cout << "  Power: " << merged.power << ", Area: " << merged.area << "\n";
        std::cout << "  Location: (" << merged.newX << ", " << merged.newY << ")\n";
        std::cout << "  Merged Components: ";
        for (const auto& name : merged.mergedFFs) {
            std::cout << name << " ";
        }
        std::cout << "\n\n";
        compcount++;
    }

    std::cout << "merged:" << compcount << std::endl << std::endl;
}



// 新增對應關係
void MergeMapping::addMapping(const std::string& singleBit, const std::string& multiBit) {
    singleToMultiBitName[singleBit] = multiBit;
    multiBitToSingles[multiBit].push_back(singleBit);
}

// 查詢某單一 bit FF 是否已被 merge
bool MergeMapping::isMerged(const std::string& singleBit) const {
    return singleToMultiBitName.find(singleBit) != singleToMultiBitName.end();
}

// 取得單一 bit FF 對應的多 bit FF 名稱（若不存在則回傳空字串）
std::string MergeMapping::getMergedName(const std::string& singleBit) const {
    auto it = singleToMultiBitName.find(singleBit);
    return it != singleToMultiBitName.end() ? it->second : "";
}

// 取得某個多 bit FF 包含的所有 single-bit FF 名稱（若不存在則回傳空 vector）
std::vector<std::string> MergeMapping::getSingleBits(const std::string& multiBit) const {
    auto it = multiBitToSingles.find(multiBit);
    return it != multiBitToSingles.end() ? it->second : std::vector<std::string>();
}

// 移除某個 single-bit FF 的 mapping
void MergeMapping::removeMapping(const std::string& singleBit) {
    auto it = singleToMultiBitName.find(singleBit);
    if (it != singleToMultiBitName.end()) {
        std::string multiBit = it->second;
        singleToMultiBitName.erase(it);

        auto& singles = multiBitToSingles[multiBit];
        singles.erase(std::remove(singles.begin(), singles.end(), singleBit), singles.end());

        if (singles.empty()) {
            multiBitToSingles.erase(multiBit);
        }
    }
}

// 清空所有紀錄
void MergeMapping::clear() {
    singleToMultiBitName.clear();
    multiBitToSingles.clear();
}
// 印出所有映射結果
void MergeMapping::printMappings() const {
    for (auto it = multiBitToSingles.begin(); it != multiBitToSingles.end(); ++it) {
        const std::string& multiBit = it->first;
        const std::vector<std::string>& singleBits = it->second;

        std::cout << "Multi-bit FF: " << multiBit << " [ ";
        for (auto jt = singleBits.begin(); jt != singleBits.end(); ++jt) {
            std::cout << *jt << " ";
        }
        std::cout << "]\n";
    }
}
std::vector<PlacedComponent>
DensityPeakClustering::generatePlacementComponents(const DefData& defData, const LefData& lefData) {
    std::vector<PlacedComponent> result;

    // 建立 cell type -> size 快取
    std::unordered_map<std::string, std::pair<int, int>> cellSizes;
    for (const auto& macro : lefData.macros) {
        int width = static_cast<int>(macro.sizeX * defData.units);
        int height = static_cast<int>(macro.sizeY * defData.units);
        cellSizes[macro.name] = { width, height };
    }

    // Step 1: 加入 merged FFs
    for (std::vector<MergedFF>::const_iterator it = mergedFFResults_.begin(); it != mergedFFResults_.end(); ++it) {
        const MergedFF& merged = *it;

        std::pair<int, int> size = cellSizes.count(merged.mbffType)
            ? cellSizes.at(merged.mbffType)
            : std::pair<int, int>(0, 0);

        int width = size.first;
        int height = size.second;

        result.emplace_back(
            merged.newInstanceName,
            merged.mbffType,
            merged.newX,
            merged.newY,
            merged.orientation,
            width,
            height,
            true, // isMergedFF
            true  // isFF
        );
    }

    // Step 2: 加入未被 merge 的原始 FFs
    MergeMapping mergeMap;
    for (const auto& merged : mergedFFResults_) {
        for (const auto& ff : merged.mergedFFs) {
            mergeMap.addMapping(ff, merged.newInstanceName);
        }
    }

    for (std::vector<FlipFlopInfo>::const_iterator it = defData.flipFlops.begin(); it != defData.flipFlops.end(); ++it) {
        const FlipFlopInfo& ff = *it;

        if (mergeMap.isMerged(ff.instName)) continue;

        std::pair<int, int> size = cellSizes.count(ff.cellType)
            ? cellSizes.at(ff.cellType)
            : std::pair<int, int>(0, 0);

        int width = size.first;
        int height = size.second;

        result.emplace_back(
            ff.instName,
            ff.cellType,
            ff.x,
            ff.y,
            ff.orient,
            width,
            height,
            false, // isMergedFF
            true   // isFF
        );
    }

    // ✅ Step 3: 加入邏輯閘等「其他元件」
    // 判斷是否是 flip-flop：建立一份 lookup set
    std::unordered_set<std::string> allFFs;
    for (const auto& ff : defData.flipFlops) {
        allFFs.insert(ff.instName);
    }

    for (const ComponentInfo& comp : defData.components) {
        if (allFFs.count(comp.name)) continue;

        std::pair<int, int> size = cellSizes.count(comp.cellType)
            ? cellSizes.at(comp.cellType)
            : std::pair<int, int>(0, 0);

        int width = size.first;
        int height = size.second;

        result.emplace_back(
            comp.name,
            comp.cellType,
            comp.x,
            comp.y,
            comp.orient,
            width,
            height,
            false, // isMergedFF
            false  // ✅ isFF
        );
    }
    totalPlacedComponentCount_ = static_cast<long long int>(result.size());

    return result;
}



std::vector<NewFlipFlopInfo>
DensityPeakClustering::generatePlacementFFsNew(const DefData& defData, const LefData& lefData) const {
    std::vector<NewFlipFlopInfo> result;

    // 建立 cell size map
    std::unordered_map<std::string, std::pair<int, int>> cellSizes;
    for (const auto& macro : lefData.macros) {
        int width = static_cast<int>(macro.sizeX * defData.units);
        int height = static_cast<int>(macro.sizeY * defData.units);
        cellSizes[macro.name] = { width, height };
    }

    // 1. 建立 merge mapping
    MergeMapping mergeMap;
    for (const auto& merged : mergedFFResults_) {
        for (const auto& ff : merged.mergedFFs) {
            mergeMap.addMapping(ff, merged.newInstanceName);
        }
    }

    // 2. Merged FFs
    for (const auto& merged : mergedFFResults_) {
        NewFlipFlopInfo ff;
        ff.instName = merged.newInstanceName;
        ff.cellType = merged.mbffType;
        ff.x = merged.newX;
        ff.y = merged.newY;
        ff.orient = merged.orientation;
        ff.bitWidth = merged.bitwidth;
        ff.isMultiBit = true;

        if (cellSizes.count(merged.mbffType)) {
            ff.width = cellSizes.at(merged.mbffType).first;
            ff.height = cellSizes.at(merged.mbffType).second;
        }

        result.push_back(ff);
    }

    // 3. 原始未 merge FFs
    for (const auto& ffOrig : defData.flipFlops) {
        if (!mergeMap.isMerged(ffOrig.instName)) {
            NewFlipFlopInfo ff;
            ff.instName = ffOrig.instName;
            ff.cellType = ffOrig.cellType;
            ff.x = ffOrig.x;
            ff.y = ffOrig.y;
            ff.orient = ffOrig.orient;
            ff.orientation = ffOrig.orientation;
            ff.clockNet = ffOrig.clockNet;
            ff.dataPins = ffOrig.dataPins;
            ff.outputPins = ffOrig.outputPins;
            ff.scanIn = ffOrig.scanIn;
            ff.scanOut = ffOrig.scanOut;
            ff.dataIn = ffOrig.dataIn;
            ff.dataOut = ffOrig.dataOut;
            ff.scanEnable = ffOrig.scanEnable;
            ff.bitWidth = ffOrig.bitWidth;
            ff.isMultiBit = false;

            if (cellSizes.count(ff.cellType)) {
                ff.width = cellSizes.at(ff.cellType).first;
                ff.height = cellSizes.at(ff.cellType).second;
            }

            result.push_back(ff);
        }
    }

    return result;
}

void DensityPeakClustering::dumpNewFFsToTxt(const std::vector<NewFlipFlopInfo>& newFFs, const std::string& filename) const {
    std::ofstream fout(filename);
    if (!fout.is_open()) {
        std::cerr << "[DPC] Error: Cannot open output file: " << filename << "\n";
        return;
    }

    for (const auto& ff : newFFs) {
        fout << ff.instName << " "
            << ff.cellType << " + PLACED ( "
            << ff.x << " " << ff.y << " ) "
            << ff.orient << " "
            << "width=" << ff.width << " "
            << "height=" << ff.height << " ;\n";
    }

    fout.close();
    std::cout << "[DPC] New FFs written to " << filename << "\n";
}


void DensityPeakClustering::dumpPlacedComponentsToTxt(const std::vector<PlacedComponent>& components, const std::string& filename) const {
    std::ofstream fout(filename);
    if (!fout.is_open()) {
        std::cerr << "[DPC] Error: Cannot open output file: " << filename << "\n";
        return;
    }

    for (const auto& comp : components) {
        fout << comp.instanceName << " "
            << comp.cellType << " + PLACED ( "
            << comp.x << " " << comp.y << " ) "
            << comp.orientation << " "
            << "width=" << comp.width << " "
            << "height=" << comp.height << " ;\n";
    }

    fout.close();
    std::cout << "[DPC] Placed components written to " << filename << "\n";
}

const MergeMapping& DensityPeakClustering::getMergeMap() const {
    return mergeMap_;
}

std::vector<std::pair<int, std::string>> MergeMapping::getBitIndexedPairs(const std::string& mbffName) const {
    std::vector<std::pair<int, std::string>> result;
    auto it = multiBitToSingles.find(mbffName);
    if (it == multiBitToSingles.end()) return result;

    const std::vector<std::string>& singles = it->second;
    for (size_t i = 0; i < singles.size(); ++i) {
        result.emplace_back(static_cast<int>(i), singles[i]);
    }
    return result;
}

void DensityPeakClustering::buildGrid_(double cell) {
    grid_.clear();
    for (int i = 0; i < (int)points_.size(); ++i) {
        int ix = (int)std::floor(points_[i].x / cell);
        int iy = (int)std::floor(points_[i].y / cell);
        grid_[cellKey_(ix, iy)].push_back(i);
    }
}

void DensityPeakClustering::rebuildGridIfNeeded_() {
    // 若座標不會在此階段變動，可不做事
}

std::vector<int> DensityPeakClustering::getCandidatesInRadius_(const DPCPoint& p, double radius) const {
    std::vector<int> cand;
    if (gridCell_ <= 0) return cand;

    int ix0 = (int)std::floor((p.x - radius) / gridCell_);
    int ix1 = (int)std::floor((p.x + radius) / gridCell_);
    int iy0 = (int)std::floor((p.y - radius) / gridCell_);
    int iy1 = (int)std::floor((p.y + radius) / gridCell_);

    cand.reserve((ix1 - ix0 + 1) * (iy1 - iy0 + 1) * 8); // 粗估
    for (int ix = ix0; ix <= ix1; ++ix) {
        for (int iy = iy0; iy <= iy1; ++iy) {
            auto it = grid_.find(cellKey_(ix, iy));
            if (it == grid_.end()) continue;
            const auto& bucket = it->second;
            cand.insert(cand.end(), bucket.begin(), bucket.end());
        }
    }
    return cand;
}


void DensityPeakClustering::computeRhoGrid_() {
    const int N = (int)points_.size();
    if (N == 0) return;

    const double dc = cutoffDistance_;
    const double inv2dc2 = 1.0 / (dc * dc);

    for (int i = 0; i < N; ++i) points_[i].rho = 0.0;

    // 可加 OpenMP
#pragma omp parallel for schedule(static)
    for (int i = 0; i < N; ++i) {
        const auto& pi = points_[i];
        double sum = 0.0;
        auto cand = getCandidatesInRadius_(pi, rhoRadius_);
        for (int j : cand) {
            if (j == i) continue;
            double d = boxManhattanDistance(pi, points_[j]);
            if (d <= rhoRadius_) {
                sum += std::exp(-(d * d) * inv2dc2);
            }
        }
        points_[i].rho = sum;
    }

    double minRho = std::numeric_limits<double>::max();
    double maxRho = std::numeric_limits<double>::lowest();
    for (const auto& pt : points_) {
        minRho = std::min(minRho, pt.rho);
        maxRho = std::max(maxRho, pt.rho);
    }
    std::cout << "[DPC][grid] rho range: " << minRho << " ~ " << maxRho << std::endl;
}

double DensityPeakClustering::estimateMaxDistBySampling_(size_t samples) const {
    const int N = (int)points_.size();
    if (N < 2) return 10.0;
    samples = std::min(samples, (size_t)N * 20);

    unsigned seed = 987654321u;
    auto rnd = [&]() { seed = 1664525u * seed + 1013904223u; return seed; };

    double best = 0.0;
    for (size_t k = 0; k < samples; ++k) {
        int i = (int)(rnd() % N);
        int j = (int)(rnd() % N);
        if (i == j) continue;
        best = std::max(best, boxManhattanDistance(points_[i], points_[j]));
    }
    return best;
}

void DensityPeakClustering::computeDeltaGrid_() {
    const int N = (int)points_.size();
    if (N == 0) return;

    std::vector<int> sortedIdx(N);
    for (int i = 0; i < N; ++i) sortedIdx[i] = i;
    std::sort(sortedIdx.begin(), sortedIdx.end(),
        [this](int a, int b) { return points_[a].rho > points_[b].rho; });

    // 估全域最大距離（抽樣）
    const double approxMaxDist = estimateMaxDistBySampling_();

    // rank 0（最高 rho）
    int top = sortedIdx[0];
    points_[top].delta = approxMaxDist;
    points_[top].nearestHigher = -1;

    // 為了加速判斷「誰是更高密度」，建立布林表
    std::vector<char> isHigher(N, 0);

    for (int rank = 1; rank < N; ++rank) {
        int idx = sortedIdx[rank];
        const double myRho = points_[idx].rho;

        // 標記前面的都是更高密度
        isHigher.assign(N, 0);
        for (int r = 0; r < rank; ++r) isHigher[sortedIdx[r]] = 1;

        double bestD = std::numeric_limits<double>::max();
        int    bestJ = -1;

        // 從較小半徑開始，逐步加大直到找到
        double radius = deltaStartRadius_;
        const double maxRadius = std::max(approxMaxDist, rhoRadius_ * 2.0);

        while (radius <= maxRadius && bestJ == -1) {
            auto cand = getCandidatesInRadius_(points_[idx], radius);
            for (int j : cand) {
                if (j == idx || !isHigher[j]) continue;
                double d = boxManhattanDistance(points_[idx], points_[j]);
                if (d <= radius && d < bestD) {
                    bestD = d; bestJ = j;
                }
            }
            if (bestJ == -1) radius *= 2.0; // 擴半徑
        }

        // 還是沒找到 → 退回全域掃描（只掃更高密度者，O(rank)）
        if (bestJ == -1) {
            for (int r = 0; r < rank; ++r) {
                int j = sortedIdx[r];
                double d = boxManhattanDistance(points_[idx], points_[j]);
                if (d < bestD) { bestD = d; bestJ = j; }
            }
        }

        points_[idx].delta = (bestJ == -1) ? approxMaxDist : bestD;
        points_[idx].nearestHigher = bestJ;
    }

    double minD = std::numeric_limits<double>::max(), maxD = -1;
    for (const auto& pt : points_) { minD = std::min(minD, pt.delta); maxD = std::max(maxD, pt.delta); }
    std::cout << "[DPC][grid] delta range: " << minD << " ~ " << maxD << std::endl;
}


double DensityPeakClustering::estimateCutoffBySampling_(size_t samples) const {
    const int N = (int)points_.size();
    if (N < 2) return 10.0;
    samples = std::min(samples, (size_t)N * 20);

    std::vector<double> ds;
    ds.reserve(samples);

    unsigned seed = 1234567u;
    auto rnd = [&]() { seed = 1664525u * seed + 1013904223u; return seed; };

    size_t cnt = 0;
    while (cnt < samples) {
        int i = (int)(rnd() % N);
        int j = (int)(rnd() % N);
        if (i == j) continue;
        ds.push_back(boxManhattanDistance(points_[i], points_[j]));
        ++cnt;
    }

    std::sort(ds.begin(), ds.end());

    int targetNeighbor = N / 100;
    if (targetNeighbor < 10) targetNeighbor = 10;
    if (targetNeighbor > 50) targetNeighbor = 50;

    // 以分位數近似原本 cutoff 索引
    size_t approxIdx = std::min((size_t)targetNeighbor * (size_t)N * ds.size()
        / (size_t)((long long)N * (N - 1) / 2),
        ds.size() - 1);
    return ds[approxIdx];
}

