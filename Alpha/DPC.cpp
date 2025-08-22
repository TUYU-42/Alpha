#include "dpc.h"
#include "DataStructures.h"
#include "ParserDEF.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <tuple>
#include <numeric>
#include <unordered_set>
#include"CompatibleList.h"
#include<algorithm>
#include<map>
#include<unordered_map>
#include <regex>
#include <iomanip> // for std::setprecision
#include "LibParser.h"

#include <limits>
// 放在檔案頂端（.cpp/.h），包含必要的 header

// DPC.cpp
static std::string join(const std::vector<std::string>& v, const char* sep) {
    std::ostringstream oss;
    for (size_t i = 0;i < v.size();++i) { if (i) oss << sep; oss << v[i]; }
    return oss.str();
}
static std::string joinD(const std::vector<double>& v, const char* sep) {
    std::ostringstream oss; oss.setf(std::ios::fixed); oss << std::setprecision(6);
    for (size_t i = 0;i < v.size();++i) { if (i) oss << sep; oss << v[i]; }
    return oss.str();
}
static std::string countsToString(const std::vector<std::pair<std::string, int>>& v, const char* sepOuter = ";", const char* sepInner = ":") {
    std::ostringstream oss;
    for (size_t i = 0;i < v.size();++i) { if (i) oss << sepOuter; oss << v[i].first << sepInner << v[i].second; }
    return oss.str();
}




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

void DensityPeakClustering::dumpCkCapReport(const std::string& path) const {
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        std::cerr << "[DPC] Cannot open report file: " << path << "\n";
        return;
    }
    // 新的表頭（舊欄位保留，新增 3 欄）
    ofs << "merged_name,bitwidth,mbff_cell,sb_cell_legacy,"
        "sb_families,sb_family_counts,sb_ck_caps,"
        "ck_before_sum,ck_after_mb,ck_saving,members\n";

    for (const auto& rep : ckCapReports_) {
        // 尋找對應 merged instance 名稱（你應該能從 mergedFFResults_ 反查；若已有映射更好）
        // 這裡示例：members[0] 所在的新名；若你已有 mergeMap_ 可改成 mergeMap_.getNewName(members[0]).
        std::string mergedName = "(unknown)";
        for (const auto& m : mergedFFResults_) {
            if (!m.mergedFFs.empty() && m.mergedFFs[0] == rep.members.front()) {
                mergedName = m.newInstanceName; break;
            }
        }

        // NEW 欄位字串
        std::string fams = join(rep.sbFamiliesUnique, ";");
        std::string cnts = countsToString(rep.sbFamilyCounts);
        std::string cks = joinD(rep.sbMemberCkCaps, ";");
        std::string mems = join(rep.members, ";");

        ofs << mergedName << ","
            << rep.groupSize << ","
            << rep.mbffCell << ","
            << rep.sbCell << ","                 // 兼容舊欄位（第一個 family）
            << fams << ","
            << cnts << ","
            << cks << ","
            << std::fixed << std::setprecision(6)
            << rep.ckCapBeforeSum << ","
            << rep.ckCapAfter << ","
            << rep.ckCapSaving << ","
            << mems << "\n";
    }
    ofs.close();
    std::cout << "[DPC] CK-cap report written: " << path << "\n";
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



     // for bankingCompatibleTable / MergedFF / MergeMapping


void DensityPeakClustering::analyzeSingleBitMergeCandidates() {
    // === 初始化 ===
    remainingSingleBitFFs.clear();
    mergeMap_.clear();
    mergedFFResults_.clear();
    ckCapReports_.clear();

    if (!libParser_) {
        std::cerr << "[DPC] Error: libParser is not set!\n";
        return;
    }

    std::cout << "\n=== [DPC] Analyze Single-Bit FF Merge Candidates (mixed-SBFF via common compatible MBFF, keep hierarchy) ===\n";

    const auto& allCells = libParser_->getAllCells();

    std::unordered_set<std::string> usedInstances;
    int totalMergeCount = 0, fourbitFF = 0, twobitFF = 0;

    // 小工具：從 cellName 抓位寬（保留你原本規則）
    auto getBitWidthFromName = [](const std::string& cellType) -> int {
        std::vector<size_t> underscores;
        for (size_t i = 0; i < cellType.size(); ++i) if (cellType[i] == '_') underscores.push_back(i);
        if (underscores.size() >= 2) {
            std::string token = cellType.substr(underscores[0] + 1, underscores[1] - underscores[0] - 1);
            std::regex rx(R"((\d+))"); std::smatch m;
            if (std::regex_search(token, m, rx)) return std::stoi(m[1]);
        }
        return 1;
        };

    auto distSq = [](double x1, double y1, double x2, double y2) {
        const double dx = x1 - x2, dy = y1 - y2; return dx * dx + dy * dy;
        };

    auto calcCenter = [&](const std::vector<std::string>& group,
        const std::map<std::string, std::pair<double, double>>& coords) -> std::pair<int, int> {
            double sx = 0, sy = 0; int n = 0;
            for (const auto& s : group) { auto it = coords.find(s); if (it != coords.end()) { sx += it->second.first; sy += it->second.second; ++n; } }
            if (!n) return { 0,0 };
            return { (int)std::lround(sx / n), (int)std::lround(sy / n) };
        };

    // 你的 greedy：從 tmpList 中反覆取最近若干顆形成 group，並移除
    auto findGroupsRemoveUsedGreedy = [&](std::vector<std::string>& tmpList, int bitSize,
        const std::map<std::string, std::pair<double, double>>& coords)
        -> std::vector<std::vector<std::string>> {
        std::vector<std::vector<std::string>> groups;

        // 只保留座標存在者
        std::vector<std::string> filtered;
        filtered.reserve(tmpList.size());
        for (auto& s : tmpList) if (coords.find(s) != coords.end()) filtered.push_back(s);

        // 就近（x+y）排序
        std::sort(filtered.begin(), filtered.end(), [&](const std::string& a, const std::string& b) {
            auto A = coords.at(a), B = coords.at(b); return (A.first + A.second) < (B.first + B.second);
            });

        std::vector<char> usedLocal(filtered.size(), 0);
        for (size_t i = 0;i < filtered.size();++i) {
            if (usedLocal[i]) continue;
            std::vector<std::pair<double, size_t>> neigh;
            for (size_t j = 0;j < filtered.size();++j) {
                if (i == j || usedLocal[j]) continue;
                auto Ai = coords.at(filtered[i]), Aj = coords.at(filtered[j]);
                neigh.push_back({ distSq(Ai.first,Ai.second,Aj.first,Aj.second), j });
            }
            if ((int)neigh.size() < bitSize - 1) continue;
            std::sort(neigh.begin(), neigh.end());
            std::vector<std::string> g; g.reserve(bitSize);
            g.push_back(filtered[i]);
            for (int k = 0;k < bitSize - 1;++k) g.push_back(filtered[neigh[k].second]);
            usedLocal[i] = 1; for (int k = 0;k < bitSize - 1;++k) usedLocal[neigh[k].second] = 1;
            groups.push_back(std::move(g));
        }

        // 從 tmpList 中剔除用掉者
        for (const auto& g : groups)
            for (const auto& s : g)
                tmpList.erase(std::remove(tmpList.begin(), tmpList.end(), s), tmpList.end());
        return groups;
        };

    // 選型：延續 Beta/Area 與 Gamma/Power 的權重，平手時以 CK‑cap saving 做最後 tie-break
    auto chooseBestMBFF = [&](const std::vector<std::string>& cands, int bit,
        double c_sb_perbit) -> std::string {
            if (cands.empty()) return {};
            std::string best; double aBest = std::numeric_limits<double>::infinity();
            double pBest = std::numeric_limits<double>::infinity();
            double ckSaveBest = -std::numeric_limits<double>::infinity();

            for (const auto& mb : cands) {
                if (getBitWidthFromName(mb) != bit) continue;
                auto it = allCells.find(mb); if (it == allCells.end()) continue;
                const auto& cell = it->second;
                double area = cell.area;
                double leak = cell.cellLeakagePower;
                double ck_mb = libParser_->getClockPinCap(mb);
                double ckSave = c_sb_perbit * bit - ck_mb; // 越大越好

                if (weights_.Beta > weights_.Gamma) {
                    // 先比 Area，再比 Power；最後比 CK 總節省
                    if (area < aBest || (area == aBest && (leak < pBest || (leak == pBest && ckSave > ckSaveBest)))) {
                        best = mb; aBest = area; pBest = leak; ckSaveBest = ckSave;
                    }
                }
                else if (weights_.Gamma > weights_.Beta) {
                    // 先比 Power，再比 Area；最後比 CK 總節省
                    if (leak < pBest || (leak == pBest && (area < aBest || (area == aBest && ckSave > ckSaveBest)))) {
                        best = mb; aBest = area; pBest = leak; ckSaveBest = ckSave;
                    }
                }
                else {
                    // 權重接近：Power→Area→CK
                    if (leak < pBest || (leak == pBest && (area < aBest || (area == aBest && ckSave > ckSaveBest)))) {
                        best = mb; aBest = area; pBest = leak; ckSaveBest = ckSave;
                    }
                }
            }
            return best;
        };

    // === 逐個 cluster（同 clock domain 已由 DPC 分群保證）===
    for (const auto& cluster : clusters_) {

        // 蒐集該 cluster 內 instance → {x,y} 與 sb-family（退化）
        std::map<std::string, std::pair<double, double>> coords;
        std::unordered_map<std::string, std::string> instToSB; // inst -> sb family name
        for (int idx : cluster.members) {
            if (idx < 0 || idx >= (int)points_.size()) continue;
            const auto& pt = points_[idx];
            if (usedInstances.count(pt.instanceName)) continue;

            std::string sbff = libParser_->getSingleBitDegenerate(pt.cellType);
            if (sbff.empty()) sbff = pt.cellType;

            // 只處理在 compatible-list 內有定義的 family
            if (bankingCompatibleTable.find(sbff) == bankingCompatibleTable.end()) continue; // CompatibleList.h
            instToSB[pt.instanceName] = sbff;
            coords[pt.instanceName] = { pt.x, pt.y };
        }
        if (coords.size() < 2) continue;

        // 依「階層前綴（最後一個 '/' 之前）」拆桶（保留原本層級限制）
        std::map<std::string, std::vector<std::string>> prefixGroups;
        for (const auto& kv : coords) {
            const std::string& inst = kv.first;
            auto pos = inst.find_last_of('/');
            std::string prefix = (pos != std::string::npos ? inst.substr(0, pos) : "");
            prefixGroups[prefix].push_back(inst);
        }

        for (auto& pg : prefixGroups) {
            // 濾掉全域已用
            std::vector<std::string> instList;
            instList.reserve(pg.second.size());
            for (const auto& nm : pg.second)
                if (!usedInstances.count(nm)) instList.push_back(nm);

            if (instList.size() < 2) {
                for (const auto& nm : instList) remainingSingleBitFFs.push_back(nm);
                continue;
            }

            // 產生 4→2 groups（同你原本 greedy）
            auto tmp4 = instList;
            auto groups4 = findGroupsRemoveUsedGreedy(tmp4, 4, coords);
            auto groups2 = findGroupsRemoveUsedGreedy(tmp4, 2, coords);

            auto handleGroups = [&](const std::vector<std::vector<std::string>>& groups, int bit) {
                for (const auto& g : groups) {
                    // 若任一成員已被用掉就跳過
                    bool anyUsed = false; for (const auto& s : g) if (usedInstances.count(s)) { anyUsed = true; break; }
                    if (anyUsed) continue;

                    // === 核心差異：對 group 內每顆 SBFF 的 compatible-list 取「交集」 ===
                    // 1) 蒐集每顆的 white list（CompatibleList.h 提供 bankingCompatibleTable）
                    std::vector<const std::vector<std::string>*> whites;
                    whites.reserve(g.size());
                    for (const auto& s : g) {
                        const std::string& sb = instToSB.at(s);
                        const auto itW = bankingCompatibleTable.find(sb);
                        if (itW == bankingCompatibleTable.end()) { whites.clear(); break; }
                        whites.push_back(&(itW->second));
                    }
                    if (whites.empty()) continue;

                    // 2) 交集
                    std::vector<std::string> commonMBFF = *whites[0];
                    auto intersect_inplace = [&](const std::vector<std::string>& b) {
                        std::vector<std::string> tmp; tmp.reserve(commonMBFF.size());
                        for (const auto& x : commonMBFF)
                            if (std::find(b.begin(), b.end(), x) != b.end()) tmp.push_back(x);
                        commonMBFF.swap(tmp);
                        };
                    for (size_t i = 1;i < whites.size();++i) intersect_inplace(*whites[i]);
                    if (commonMBFF.empty()) continue;

                    // 3) 僅保留位寬相符的 MBFF
                    std::vector<std::string> bitMatched;
                    bitMatched.reserve(commonMBFF.size());
                    for (const auto& mb : commonMBFF)
                        if (getBitWidthFromName(mb) == bit) bitMatched.push_back(mb);
                    if (bitMatched.empty()) continue;

                    // 4) 取消「singleBitDegenerate == sbff」的硬條件（允許異型 SBFF 混用）
                    //    但可做弱檢查：bestFF 的退化型需屬於 group 內至少一種 single-bit family（sanity）
                    // CK-cap：以 group 內 SBFF 的 CK 值「平均」當作單顆 c_sb，較合理
                    double c_sb_sum = 0.0;
                    for (const auto& s : g) {
                        const std::string& sb = instToSB.at(s);
                        c_sb_sum += libParser_->getClockPinCap(sb);
                    }
                    double c_sb_avg = (g.empty() ? 0.0 : (c_sb_sum / (double)g.size()));

                    // 5) 按權重與 CK saving 選出最佳 MBFF
                    std::string bestFF = chooseBestMBFF(bitMatched, bit, c_sb_avg);
                    if (bestFF.empty()) continue;

                    // 弱檢查：bestFF 的單位退化是否屬於 group 內任一 sb-family（若取得到）
                    bool sanityOK = true;
                    if (const auto* cell = libParser_->getCell(bestFF)) {
                        const std::string& deg = cell->singleBitDegenerate;
                        if (!deg.empty()) {
                            bool hit = false;
                            for (const auto& s : g) if (instToSB.at(s) == deg) { hit = true; break; }
                            sanityOK = hit; // 至少要覆蓋到其中一種 family
                        }
                    }
                    if (!sanityOK) continue;

                    // 6) 建立 MergedFF 結果，命名保留階層：prefix/merged_#
                    ++totalMergeCount;
                    MergedFF merged;
                    merged.mbffType = bestFF;
                    merged.bitwidth = bit;
                    merged.mergedFFs = g;

                    std::string base = "merged_" + std::to_string(totalMergeCount);
                    merged.newInstanceName = (pg.first.empty() ? base : (pg.first + "/" + base));

                    // 位置用幾何中心
                    auto cen = calcCenter(g, coords);
                    merged.newX = cen.first; merged.newY = cen.second;

                    // 面積/功耗
                    if (const auto it = allCells.find(bestFF); it != allCells.end()) {
                        merged.area = (float)it->second.area;
                        merged.power = (float)it->second.cellLeakagePower;
                    }

                    // D/Q pin 映射（簡單 D0/Q0 對應 group[0]…）
                    for (int i = 0;i < (int)g.size();++i) {
                        std::string d = "D" + std::to_string(i);
                        std::string q = "Q" + std::to_string(i);
                        merged.mbffPinToOrigPin[d] = g[i] + "/D";
                        merged.mbffPinToOrigPin[q] = g[i] + "/Q";
                        merged.mbffPinToOrigFF[d] = g[i];
                        merged.mbffPinToOrigFF[q] = g[i];
                    }

                    c_sb_sum = 0.0;
                    std::unordered_map<std::string, int> famCnt;
                    std::vector<std::string> sbFamiliesPerMember;
                    std::vector<double>     sbCkPerMember;
                    sbFamiliesPerMember.reserve(g.size());
                    sbCkPerMember.reserve(g.size());

                    for (const auto& s : g) {
                        const std::string& sb = instToSB.at(s);                   // 這顆的 single-bit family/type
                        double c_sb = libParser_->getClockPinCap(sb);             // 這顆對應 family 的 CK cap
                        c_sb_sum += c_sb;
                        ++famCnt[sb];
                        sbFamiliesPerMember.push_back(sb);
                        sbCkPerMember.push_back(c_sb);
                    }

                    CkCapReport rep;
                    rep.groupSize = bit;
                    // 舊欄位：為了相容保留（放第一個 family；若你想顯示 "MIXED" 也可）
                    rep.sbCell = sbFamiliesPerMember.empty() ? "" : sbFamiliesPerMember.front();
                    rep.mbffCell = bestFF;
                    rep.ckCapBeforeSum = c_sb_sum;
                    rep.ckCapAfter = libParser_->getClockPinCap(bestFF);
                    rep.ckCapBefore = c_sb_sum;                 // 舊欄位維持相同語意
                    rep.ckCapSaving = rep.ckCapBeforeSum - rep.ckCapAfter;
                    rep.members = g;

                    // NEW: unique families + counts + 每顆 CK
                    for (const auto& kv : famCnt) {
                        rep.sbFamiliesUnique.push_back(kv.first);
                        rep.sbFamilyCounts.push_back(kv);           // pair<family, count>
                    }
                    rep.sbMemberCkCaps = std::move(sbCkPerMember);

                    // push
                    ckCapReports_.push_back(std::move(rep));
                    // 8) 標記 used、統計
                    mergedFFResults_.push_back(std::move(merged));
                    for (const auto& s : g) usedInstances.insert(s);
                    if (bit == 4) ++fourbitFF; else if (bit == 2) ++twobitFF;
                }
                };

            // 先 4 再 2（與原流程一致）
            handleGroups(groups4, 4);
            handleGroups(groups2, 2);

            // 剩餘未用的收回 remaining
            for (const auto& nm : tmp4) if (!usedInstances.count(nm)) remainingSingleBitFFs.push_back(nm);
        }
    }

    // 建回 mergeMap_（供後續生成/輸出使用）
    for (const auto& m : mergedFFResults_) {
        for (const auto& s : m.mergedFFs) mergeMap_.addMapping(s, m.newInstanceName);
    }

    std::cout << "\n[DPC] Found " << mergedFFResults_.size()
        << " merged instances. (4-bit: " << fourbitFF
        << ", 2-bit: " << twobitFF << ")\n";
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
