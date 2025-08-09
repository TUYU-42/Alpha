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
    buildDistanceMatrix();
    cutoffDistance_ = estimateOptimalCutoffDistance();
    std::cout << "[DPC] cutoffDistance auto-set to " << cutoffDistance_ << std::endl;
    computeRho();
    computeDelta();
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
    int twobitFF = 0;
    int fourbitFF = 0;

    mergeMap_.clear();
    if (!libParser_) {
        std::cerr << "[DPC] Error: libParser is not set!" << std::endl;
        return;
    }

    std::cout << "\n=== [DPC] Analyze Single-Bit FF Merge Candidates ===" << std::endl;
    mergedFFResults_.clear();

    const auto& allCells = libParser_->getAllCells();
    int totalClustersWithMerges = 0;
    int totalMergePairs = 0;

    auto getBitWidthFromName = [](const std::string& name) -> int {
        if (name.find("16_") != std::string::npos) return 16;
        if (name.find("8_") != std::string::npos) return 8;
        if (name.find("4_") != std::string::npos) return 4;
        if (name.find("2_") != std::string::npos) return 2;
        return 1;
        };

    auto distSq = [](int x1, int y1, int x2, int y2) {
        return (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2);
        };

    auto calcGroupCenter = [&](const std::vector<std::string>& group) -> std::pair<int, int> {
        int sumX = 0, sumY = 0;
        for (const auto& name : group) {
            for (const auto& pt : points_) {
                if (pt.instanceName == name) {
                    sumX += pt.x;
                    sumY += pt.y;
                    break;
                }
            }
        }
        return { sumX / static_cast<int>(group.size()), sumY / static_cast<int>(group.size()) };
        };

    std::vector<std::string> remainingSingleBitFFs;

    for (const auto& cluster : clusters_) {
        // collect by "single-bit degenerate" type
        std::map<std::string, std::vector<std::string>> sbffToInstances;
        std::map<std::string, std::pair<int, int>> coords;
        for (int idx : cluster.members) {
            const auto& pt = points_[idx];
            std::string sbff = libParser_->getSingleBitDegenerate(pt.cellType);
            if (sbff.empty()) sbff = pt.cellType;
            sbffToInstances[sbff].push_back(pt.instanceName);
            coords[pt.instanceName] = { pt.x, pt.y };
        }

        bool clusterPrinted = false;

        for (auto& kv : sbffToInstances) {
            const std::string& sbff = kv.first;
            const auto& instListRaw = kv.second;
            if (instListRaw.size() < 2) {
                remainingSingleBitFFs.insert(remainingSingleBitFFs.end(), instListRaw.begin(), instListRaw.end());
                continue;
            }
            // extra: group by path prefix
            std::map<std::string, std::vector<std::string>> prefixGroups;
            for (const auto& inst : instListRaw) {
                auto pos = inst.find_last_of('/');
                std::string prefix = (pos != std::string::npos ? inst.substr(0, pos) : "");
                prefixGroups[prefix].push_back(inst);
            }
            for (auto& pg : prefixGroups) {
                auto& instList = pg.second;
                if (instList.size() < 2) {
                    remainingSingleBitFFs.insert(remainingSingleBitFFs.end(), instList.begin(), instList.end());
                    continue;
                }
                if (!clusterPrinted) {
                    std::cout << "\nCluster #" << cluster.clusterId << ":\n";
                    clusterPrinted = true;
                    totalClustersWithMerges++;
                }

                std::vector<std::string> mbffCandidates;
                for (auto& cellIt : allCells) {
                    if (cellIt.second.singleBitDegenerate == sbff)
                        mbffCandidates.push_back(cellIt.first);
                }
                if (mbffCandidates.empty()) {
                    remainingSingleBitFFs.insert(remainingSingleBitFFs.end(), instList.begin(), instList.end());
                    continue;
                }

                // find best 4-bit groups
                auto findGroups = [&](int bitSize) {
                    std::vector<std::vector<std::string>> groups;
                    auto tmpList = instList;
                    while (tmpList.size() >= bitSize) {
                        std::vector<std::string> bestGroup;
                        int bestD = INT_MAX;
                        std::vector<int> idx(tmpList.size()); std::iota(idx.begin(), idx.end(), 0);
                        std::vector<bool> sel(tmpList.size(), false); std::fill(sel.begin(), sel.begin() + bitSize, true);
                        do {
                            std::vector<std::string> cand;
                            for (int i = 0; i < sel.size(); ++i) if (sel[i]) cand.push_back(tmpList[i]);
                            int dsum = 0;
                            for (int a = 0; a < bitSize; ++a) for (int b = a + 1; b < bitSize; ++b)
                                dsum += distSq(coords[cand[a]].first, coords[cand[a]].second,
                                    coords[cand[b]].first, coords[cand[b]].second);
                            if (dsum < bestD) { bestD = dsum; bestGroup = cand; }
                        } while (std::prev_permutation(sel.begin(), sel.end()));
                        if (bestGroup.empty()) break;
                        groups.push_back(bestGroup);
                        for (auto& n : bestGroup) tmpList.erase(std::remove(tmpList.begin(), tmpList.end(), n), tmpList.end());
                    }
                    return groups;
                    };
                auto groups4 = findGroups(4);
                auto groups2 = findGroups(2);

                auto handle = [&](const std::vector<std::vector<std::string>>& groups, int bit) {
                    for (auto& group : groups) {
                        // pick best MBFF cell
                        std::string bestFF;
                        double bestM = 1e9;
                        for (auto& mbff : mbffCandidates) if (getBitWidthFromName(mbff) == bit) {
                            const auto& cell = allCells.at(mbff);
                            double metric = (weights_.Beta / weights_.Gamma > 3000) ? cell.cellLeakagePower : cell.area;
                            if (metric < bestM) { bestM = metric; bestFF = mbff; }
                        }
                        if (bestFF.empty()) continue;

                        totalMergePairs++;
                        MergedFF merged;
                        merged.mbffType = bestFF;
                        merged.bitwidth = bit;
                        merged.mergedFFs = group;
                        // newInstanceName = same prefix + "/merged_x"
                        std::string base = "merged_" + std::to_string(totalMergePairs);
                        merged.newInstanceName = (pg.first.empty() ? base : pg.first + "/" + base);
                        auto cen = calcGroupCenter(group);
                        merged.newX = cen.first; merged.newY = cen.second;
                        auto& cellInfo = allCells.at(bestFF);
                        merged.power = cellInfo.cellLeakagePower;
                        merged.area = cellInfo.area;
                        const auto& mbffMacro = macroMap_->at(merged.mbffType).pins;
                        for (int i = 0; i < merged.mergedFFs.size(); ++i) {
                            std::string d_pin = "D" + std::to_string(i);
                            std::string q_pin = "Q" + std::to_string(i);
                            merged.mbffPinToOrigPin[d_pin] = merged.mergedFFs[i] + "/D";
                            merged.mbffPinToOrigPin[q_pin] = merged.mergedFFs[i] + "/Q";
                            merged.mbffPinToOrigFF[d_pin] = merged.mergedFFs[i];
                            merged.mbffPinToOrigFF[q_pin] = merged.mergedFFs[i];
                        }
                        mergedFFResults_.push_back(merged);
                    }
                    };
                handle(groups4, 4);
                handle(groups2, 2);
            }
        }
    }
    // build mergeMap
    for (auto& m : mergedFFResults_) for (auto& s : m.mergedFFs)
        mergeMap_.addMapping(s, m.newInstanceName);

    std::cout << "\n[DPC] Found " << mergedFFResults_.size() << " merged instances.\n";
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



