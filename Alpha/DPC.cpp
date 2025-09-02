#include "DPC.h"
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
#include <cmath>
#include <limits>
#ifdef _OPENMP
#include <omp.h>
#endif
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
        std::string mergedName = mergeMap_.getMergedName(rep.members.front());
        if (mergedName.empty()) mergedName = "(unknown)";

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
        gridCell_ = std::max(1.0, params_.gridCellMul * cutoffDistance_);
        rhoRadius_ = std::max(1.0, params_.rhoRadiusMul * cutoffDistance_);
        deltaStartRadius_ = std::max(1.0, params_.deltaStartMul * cutoffDistance_);


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
    const int N = (int)points_.size();
    if (params_.kMode == DpcParams::KMode::CeilNOver4) return (N + 3) / 4;

    // Sigma: rho*delta >= mean + lambda*std
    std::vector<double> s; s.reserve(N);
    for (const auto& p : points_) s.push_back(p.rho * p.delta);
    if (s.empty()) return 1;
    double mean = std::accumulate(s.begin(), s.end(), 0.0) / s.size();
    double var = 0.0; for (double v : s) var += (v - mean) * (v - mean);
    double stdv = std::sqrt(var / std::max(1, (int)s.size() - 1));
    double thr = mean + params_.kSigmaLambda * stdv;

    int k = 0; for (double v : s) if (v >= thr) ++k;
    if (k <= 0) k = 1; // 不要默默變 8
    // 合理上限：sqrt(N) 或 N/4 取小者
    int cap = std::max(1, (int)std::sqrt((double)N));
    return std::min(k, cap);
}




double DensityPeakClustering::estimateOptimalCutoffDistance() const {
    if (distMat_.empty() || points_.size() < 2) return 10.0;

    std::vector<double> dists;
    const int N = (int)points_.size();
    dists.reserve((size_t)N * (N - 1) / 2);
    for (int i = 0; i < N; ++i) for (int j = i + 1; j < N; ++j) dists.push_back(distMat_[i][j]);
    if (dists.empty()) return 10.0;

    std::sort(dists.begin(), dists.end());

    // 用分位數，百分比來自 params_.neighborPercent（建議 0.3%~5%）
    double p = std::max(0.0005, std::min(0.05, params_.neighborPercent)); // 0.05%~5%
    size_t idx = (size_t)std::llround(p * (dists.size() - 1));
    if (idx >= dists.size()) idx = dists.size() - 1;
    return dists[idx];
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
    // --- 失敗原因統計 ---
    struct MergeStats { int failRow = 0, failCompat = 0, failDist = 0, failCK = 0, failHPWL = 0, pass2 = 0, pass4 = 0, fb22 = 0; } stats;

    // === 初始化 ===
    remainingSingleBitFFs.clear();
    mergeMap_.clear();
    mergedFFResults_.clear();
    ckCapReports_.clear();

    if (!libParser_) {
        std::cerr << "[DPC] Error: libParser is not set!\n";
        return;
    }

    // === 顯示目前 preset（用來確認 A/B/C 有生效）===
    std::cout << "[DPC] Params:"
        << " sameRowTolMul=" << params_.sameRowTolMul
        << " dist4=(" << params_.dist4_rhoMul << "," << params_.dist4_dcMul << ")"
        << " dist2=(" << params_.dist2_rhoMul << "," << params_.dist2_dcMul << ")"
        << " hpwlThr(D,Q)=(" << params_.hpwlThrDMul << "," << params_.hpwlThrQMul << ")"
        << " wQ=" << params_.hpwlWeightQ
        << " ckSaveMinFrac=" << params_.ckSaveMinFrac
        << std::endl;

    std::cout << "\n=== [DPC] Analyze Single-Bit FF Merge Candidates "
        "(mixed-SBFF via common compatible MBFF, keep hierarchy) ===\n";

    const auto& allCells = libParser_->getAllCells();

    std::unordered_set<std::string> usedInstances;
    int totalMergeCount = 0, fourbitFF = 0, twobitFF = 0;

    // cellName 抓位寬
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

    // 幾何快取：inst -> (x, y, w, h)
    std::unordered_map<std::string, std::tuple<double, double, double, double>> instGeom;
    for (const auto& p : points_) instGeom[p.instanceName] = std::make_tuple(p.x, p.y, (double)p.width, (double)p.height);

    auto getHeightOf = [&](const std::string& inst)->double {
        auto it = instGeom.find(inst);
        if (it == instGeom.end()) return 0.0;
        return std::get<3>(it->second);
        };

    // HPWL 工具與 net 端點（若無 net DB，回空；會用位移距離替代）
    auto hpwl_of_points = [&](const std::vector<std::pair<double, double>>& pts)->double {
        if (pts.empty()) return 0.0;
        double minx = 1e100, maxx = -1e100, miny = 1e100, maxy = -1e100;
        for (auto& q : pts) {
            minx = std::min(minx, q.first); maxx = std::max(maxx, q.first);
            miny = std::min(miny, q.second); maxy = std::max(maxy, q.second);
        }
        return (maxx - minx) + (maxy - miny);
        };
    auto getDNetOtherPts = [&](const std::string& /*ff*/)->std::vector<std::pair<double, double>> {
        std::vector<std::pair<double, double>> pts; return pts;
        };
    auto getQNetOtherPts = [&](const std::string& /*ff*/)->std::vector<std::pair<double, double>> {
        std::vector<std::pair<double, double>> pts; return pts;
        };

    // 同 row 守門（用 y 差與 cell 高度）
    auto isSameRowGroup = [&](const std::vector<std::string>& gg)->bool {
        std::vector<double> hs; hs.reserve(gg.size());
        for (auto& s : gg) { double h = getHeightOf(s); if (h > 0) hs.push_back(h); }
        if (hs.empty()) return true;
        std::sort(hs.begin(), hs.end());
        double rowH = hs[hs.size() / 2];
        double tol = params_.sameRowTolMul * rowH;
        double y0 = std::get<1>(instGeom.at(gg.front()));
        for (auto& s : gg) {
            double y = std::get<1>(instGeom.at(s));
            if (std::fabs(y - y0) > tol) return false;
        }
        return true;
        };

    // 4 -> (2,2) 最短總距離配對
   // C++14 OK
    auto splitBestPairs4 = [&](const std::vector<std::string>& g4)
        -> std::vector<std::vector<std::string>>
        {
            if (g4.size() != 4) return {};

            auto D = [&](int i, int j) -> double {
                double xi, yi, wi, hi;
                double xj, yj, wj, hj;

                // 假設 instGeom: std::unordered_map<std::string, std::tuple<double,double,double,double>> 或同型
                {
                    const auto& ti = instGeom.at(g4[i]);
                    std::tie(xi, yi, wi, hi) = ti;
                }
                {
                    const auto& tj = instGeom.at(g4[j]);
                    std::tie(xj, yj, wj, hj) = tj;
                }

                const double dx = xi - xj;
                const double dy = yi - yj;
                return std::sqrt(dx * dx + dy * dy);
                };

            const double c01_23 = D(0, 1) + D(2, 3);
            const double c02_13 = D(0, 2) + D(1, 3);
            const double c03_12 = D(0, 3) + D(1, 2);

            if (c01_23 <= c02_13 && c01_23 <= c03_12) return { { g4[0], g4[1] }, { g4[2], g4[3] } };
            if (c02_13 <= c03_12)                     return { { g4[0], g4[2] }, { g4[1], g4[3] } };
            return                                       { { g4[0], g4[3] }, { g4[1], g4[2] } };
        };

    // 枚舉排列
    auto allPerms = [&](int bit) {
        std::vector<std::vector<int>> perms;
        std::vector<int> p(bit); std::iota(p.begin(), p.end(), 0);
        do { perms.push_back(p); } while (std::next_permutation(p.begin(), p.end()));
        return perms;
        };

    // greedy 分群（沿用你原本的，保留做 4-bit 起手）
    auto findGroupsRemoveUsedGreedy = [&](std::vector<std::string>& tmpList, int bitSize,
        const std::map<std::string, std::pair<double, double>>& coords)
        -> std::vector<std::vector<std::string>> {
        std::vector<std::vector<std::string>> groups;
        std::vector<std::string> filtered; filtered.reserve(tmpList.size());
        for (auto& s : tmpList) if (coords.find(s) != coords.end()) filtered.push_back(s);
        std::sort(filtered.begin(), filtered.end(), [&](const std::string& a, const std::string& b) {
            auto A = coords.at(a), B = coords.at(b); return (A.first + A.second) < (B.first + B.second);
            });
        std::vector<char> usedLocal(filtered.size(), 0);
        for (size_t i = 0; i < filtered.size(); ++i) {
            if (usedLocal[i]) continue;
            std::vector<std::pair<double, size_t>> neigh;
            for (size_t j = 0; j < filtered.size(); ++j) {
                if (i == j || usedLocal[j]) continue;
                auto Ai = coords.at(filtered[i]), Aj = coords.at(filtered[j]);
                neigh.push_back({ distSq(Ai.first,Ai.second,Aj.first,Aj.second), j });
            }
            if ((int)neigh.size() < bitSize - 1) continue;
            std::sort(neigh.begin(), neigh.end());
            std::vector<std::string> g; g.reserve(bitSize);
            g.push_back(filtered[i]);
            for (int k = 0; k < bitSize - 1; ++k) g.push_back(filtered[neigh[k].second]);
            usedLocal[i] = 1; for (int k = 0; k < bitSize - 1; ++k) usedLocal[neigh[k].second] = 1;
            groups.push_back(std::move(g));
        }
        for (const auto& g : groups)
            for (const auto& s : g)
                tmpList.erase(std::remove(tmpList.begin(), tmpList.end(), s), tmpList.end());
        return groups;
        };

    // CK 優先挑型 —— 也套用 ckSaveMinFrac
    auto chooseBestMBFF = [&](const std::vector<std::string>& cands, int bit,
        double c_sb_perbit) -> std::string {
            if (cands.empty()) return {};
            std::string best;
            double bestSave = -std::numeric_limits<double>::infinity();
            double bestArea = std::numeric_limits<double>::infinity();
            double bestLeak = std::numeric_limits<double>::infinity();

            for (const auto& mb : cands) {
                if (getBitWidthFromName(mb) != bit) continue;
                auto it = allCells.find(mb); if (it == allCells.end()) continue;
                const auto& cell = it->second;
                double area = cell.area;
                double leak = cell.cellLeakagePower;
                double ck_mb = libParser_->getClockPinCap(mb);
                double ckSave = c_sb_perbit * bit - ck_mb;                   // ≈ ΣCsb − Cmb
                double minSave = params_.ckSaveMinFrac * (c_sb_perbit * bit + 1e-12);
                if (ckSave < minSave) continue;                              // <== 相對門檻
                if (ckSave > bestSave || (ckSave == bestSave && (area < bestArea || (area == bestArea && leak < bestLeak)))) {
                    best = mb; bestSave = ckSave; bestArea = area; bestLeak = leak;
                }
            }
            return best;
        };

    // ===== 新增：2-bit 候選 pair 打分 + 不重疊最大匹配 =====
    auto L1_dist = [&](const std::string& A, const std::string& B)->double {
        const auto& a = instGeom.at(A);
        const auto& b = instGeom.at(B);
        return std::fabs(std::get<0>(a) - std::get<0>(b)) + std::fabs(std::get<1>(a) - std::get<1>(b));
        };

    struct PairCand {
        std::string a, b;
        double score;   // 用 CK 節省當分數
        double dist;    // 當 tie-break
        double ckSave;  // 記錄用
    };

    auto build2BitPairs = [&](const std::vector<std::string>& pool,
        const std::map<std::string, std::pair<double, double>>& coords,
        const std::unordered_map<std::string, std::string>& instToSB,
        double distLimitL1)->std::vector<PairCand> {
            std::vector<PairCand> out;
            out.reserve(pool.size());
            for (size_t i = 0; i < pool.size(); ++i) {
                for (size_t j = i + 1; j < pool.size(); ++j) {
                    const std::string& A = pool[i];
                    const std::string& B = pool[j];

                    // 同 row 守門
                    if (!isSameRowGroup({ A,B })) { ++stats.failRow; continue; }

                    // 幾何距離門檻（L1）
                    double d = L1_dist(A, B);
                    if (d > distLimitL1) { ++stats.failDist; continue; }

                    // 白名單交集（位寬=2）
                    const std::string& sbA = instToSB.at(A);
                    const std::string& sbB = instToSB.at(B);
                    auto itA = bankingCompatibleTable.find(sbA);
                    auto itB = bankingCompatibleTable.find(sbB);
                    if (itA == bankingCompatibleTable.end() || itB == bankingCompatibleTable.end()) { ++stats.failCompat; continue; }

                    std::vector<std::string> mb2; mb2.reserve(8);
                    for (const auto& mb : itA->second) {
                        if (std::find(itB->second.begin(), itB->second.end(), mb) != itB->second.end()) {
                            if (getBitWidthFromName(mb) == 2) mb2.push_back(mb);
                        }
                    }
                    if (mb2.empty()) { ++stats.failCompat; continue; }

                    // 估 CK 節省（取最佳者）
                    double c_sb_avg = 0.5 * (libParser_->getClockPinCap(instToSB.at(A)) +
                        libParser_->getClockPinCap(instToSB.at(B)));
                    double bestSave = -1e100;
                    for (const auto& mb : mb2) {
                        double ck_mb = libParser_->getClockPinCap(mb);
                        bestSave = std::max(bestSave, 2.0 * c_sb_avg - ck_mb);
                    }
                    double minSave = params_.ckSaveMinFrac * std::max(1e-12, 2.0 * c_sb_avg);
                    if (bestSave < minSave) { ++stats.failCK; continue; }

                    out.push_back({ A,B,bestSave,d,bestSave });
                }
            }
            std::sort(out.begin(), out.end(), [](const PairCand& x, const PairCand& y) {
                if (x.score != y.score) return x.score > y.score;
                return x.dist < y.dist;
                });
            return out;
        };

    auto greedyPickPairs = [&](const std::vector<PairCand>& pairs)
        -> std::vector<std::vector<std::string>> {
        std::unordered_set<std::string> used;
        std::vector<std::vector<std::string>> groups2; groups2.reserve(pairs.size());
        for (const auto& p : pairs) {
            if (used.count(p.a) || used.count(p.b)) continue;
            used.insert(p.a); used.insert(p.b);
            groups2.push_back({ p.a, p.b });
        }
        return groups2;
        };

    // 2-opt：在合法候選內重組 2-bit 配對以最大化總 CK 節省
    auto improvePairs2Opt = [&](std::vector<std::vector<std::string>>& groups2,
        const std::vector<PairCand>& allPairs,
        int maxIters = 200) {
            auto key = [](const std::string& x, const std::string& y) {
                return (x < y) ? (x + "|" + y) : (y + "|" + x);
                };
            std::unordered_map<std::string, double> w;
            w.reserve(allPairs.size() * 2);
            for (const auto& p : allPairs) {
                w[key(p.a, p.b)] = p.score;   // 分數 = CK 節省
            }
            auto scoreOf = [&](const std::string& u, const std::string& v) -> double {
                auto it = w.find(key(u, v));
                return (it == w.end()) ? -1e100 : it->second; // 非候選視為極差，避免違規重組
                };

            const int n = static_cast<int>(groups2.size());
            if (n <= 1) return;

            for (int it = 0; it < maxIters; ++it) {
                bool improved = false;
                for (int i = 0; i < n && !improved; ++i) {
                    for (int j = i + 1; j < n && !improved; ++j) {
                        const auto& P1 = groups2[i];
                        const auto& P2 = groups2[j];
                        if (P1.size() != 2 || P2.size() != 2) continue;
                        const std::string& a1 = P1[0];
                        const std::string& b1 = P1[1];
                        const std::string& a2 = P2[0];
                        const std::string& b2 = P2[1];
                        const double cur = scoreOf(a1, b1) + scoreOf(a2, b2);
                        const double s1 = scoreOf(a1, a2) + scoreOf(b1, b2);
                        const double s2 = scoreOf(a1, b2) + scoreOf(b1, a2);
                        if (s1 > cur && s1 >= s2) {
                            groups2[i] = { a1, a2 };
                            groups2[j] = { b1, b2 };
                            improved = true;
                        }
                        else if (s2 > cur) {
                            groups2[i] = { a1, b2 };
                            groups2[j] = { b1, a2 };
                            improved = true;
                        }
                    }
                }
                if (!improved) break; // 收斂
            }
        };
    // ===== 2-bit 配對副程式結束 =====

    // === 逐 cluster（同 clock domain 已由 DPC 保證）===
    for (const auto& cluster : clusters_) {
        // 蒐集 instance → {x,y} 與 sb-family（退化）
        std::map<std::string, std::pair<double, double>> coords;
        std::unordered_map<std::string, std::string> instToSB;
        for (int idx : cluster.members) {
            if (idx < 0 || idx >= (int)points_.size()) continue;
            const auto& pt = points_[idx];
            if (usedInstances.count(pt.instanceName)) continue;

            std::string sbff = libParser_->getSingleBitDegenerate(pt.cellType);
            if (sbff.empty()) sbff = pt.cellType;
            if (bankingCompatibleTable.find(sbff) == bankingCompatibleTable.end()) continue;
            instToSB[pt.instanceName] = sbff;
            coords[pt.instanceName] = { pt.x, pt.y };
        }
        if (coords.size() < 2) continue;

        // 依階層前綴拆桶
        std::map<std::string, std::vector<std::string>> prefixGroups;
        for (const auto& kv : coords) {
            const std::string& inst = kv.first;
            auto pos = inst.find_last_of('/');
            std::string prefix = (pos != std::string::npos ? inst.substr(0, pos) : "");
            prefixGroups[prefix].push_back(inst);
        }

        for (auto& pg : prefixGroups) {
            // 濾掉已用
            std::vector<std::string> instList; instList.reserve(pg.second.size());
            for (const auto& nm : pg.second) if (!usedInstances.count(nm)) instList.push_back(nm);
            if (instList.size() < 2) {
                for (const auto& nm : instList) remainingSingleBitFFs.push_back(nm);
                continue;
            }

            // 先 4 後 2：4b 沿用原 greedy，2b 改為候選打分+最大匹配 (+2-opt)
            auto tmp4 = instList;
            auto groups4 = findGroupsRemoveUsedGreedy(tmp4, 4, coords);

            const double dc = (cutoffDistance_ > 0 ? cutoffDistance_ : 1.0);
            const double rR = (rhoRadius_ > 0 ? rhoRadius_ : 4.0 * dc);
            const double distLimitRel2 = std::min(params_.dist2_rhoMul * rR, params_.dist2_dcMul * dc);
            auto pairCands = build2BitPairs(tmp4, coords, instToSB, distLimitRel2);
            auto groups2 = greedyPickPairs(pairCands);
            // 使用 2-opt 對 2-bit 配對做局部最佳化（僅在合法候選內重組）
            improvePairs2Opt(groups2, pairCands);

            // 嘗試合併（含 anchor+perm + ΔHPWL 守門）
            auto tryMergeGroup = [&](const std::vector<std::string>& g, int bit)->bool {
                // 幾何先決
                if (!isSameRowGroup(g)) { ++stats.failRow; return false; }

                // 兼容名單交集
                std::vector<const std::vector<std::string>*> whites; whites.reserve(g.size());
                for (const auto& s : g) {
                    const std::string& sb = instToSB.at(s);
                    const auto itW = bankingCompatibleTable.find(sb);
                    if (itW == bankingCompatibleTable.end()) { whites.clear(); break; }
                    whites.push_back(&(itW->second));
                }
                if (whites.empty()) { ++stats.failCompat; return false; }

                std::vector<std::string> commonMBFF = *whites[0];
                auto intersect_inplace = [&](const std::vector<std::string>& b) {
                    std::vector<std::string> tmp; tmp.reserve(commonMBFF.size());
                    for (const auto& x : commonMBFF)
                        if (std::find(b.begin(), b.end(), x) != b.end()) tmp.push_back(x);
                    commonMBFF.swap(tmp);
                    };
                for (size_t i = 1; i < whites.size(); ++i) intersect_inplace(*whites[i]);
                if (commonMBFF.empty()) { ++stats.failCompat; return false; }

                std::vector<std::string> bitMatched;
                bitMatched.reserve(commonMBFF.size());
                for (const auto& mb : commonMBFF) if (getBitWidthFromName(mb) == bit) bitMatched.push_back(mb);
                if (bitMatched.empty()) { ++stats.failCompat; return false; }

                // 距離上限（4b 更嚴）
                auto max_pairwise_dist = [&](const std::vector<std::string>& gg)->double {
                    double best = 0.0;
                    for (size_t i = 0; i < gg.size(); ++i) {
                        auto Ai = coords.at(gg[i]);
                        for (size_t j = i + 1; j < gg.size(); ++j) {
                            auto Aj = coords.at(gg[j]); double dx = Ai.first - Aj.first, dy = Ai.second - Aj.second;
                            best = std::max(best, std::sqrt(dx * dx + dy * dy));
                        }
                    } return best;
                    };
                const double dcL = (cutoffDistance_ > 0 ? cutoffDistance_ : 1.0);
                const double rRL = (rhoRadius_ > 0 ? rhoRadius_ : 4.0 * dcL);
                double distLimit = (bit == 4)
                    ? std::min(params_.dist4_rhoMul * rRL, params_.dist4_dcMul * dcL)
                    : std::min(params_.dist2_rhoMul * rRL, params_.dist2_dcMul * dcL);
                if (max_pairwise_dist(g) > distLimit) { ++stats.failDist; return false; }

                // CK-cap 準備
                double c_sb_sum = 0.0;
                for (const auto& s : g) c_sb_sum += libParser_->getClockPinCap(instToSB.at(s));
                double c_sb_avg = c_sb_sum / std::max(1, (int)g.size());

                // CK 優先挑型（含相對門檻）
                std::string bestFF = chooseBestMBFF(bitMatched, bit, c_sb_avg);
                if (bestFF.empty()) { ++stats.failCK; return false; }

                // 退化型覆蓋 sanity
                if (const auto* cell = libParser_->getCell(bestFF)) {
                    const std::string& deg = cell->singleBitDegenerate;
                    if (!deg.empty()) {
                        bool hit = false; for (auto& s : g) if (instToSB.at(s) == deg) { hit = true; break; }
                        if (!hit) { ++stats.failCompat; return false; }
                    }
                }

                // 最終 CK 把關（ΣCsb − Cmb ≥ 最小比例）
                double ck_mb = libParser_->getClockPinCap(bestFF);
                double ck_save = c_sb_sum - ck_mb;
                double min_save = params_.ckSaveMinFrac * std::max(1e-12, c_sb_sum);
                if (ck_save < min_save) { ++stats.failCK; return false; }

                // ΔHPWL 守門 + anchor+perm 搜尋 —— 使用 preset
                std::unordered_map<std::string, double> hpwlD_before, hpwlQ_before;
                for (auto& s : g) {
                    auto dpts = getDNetOtherPts(s); dpts.push_back(coords.at(s));
                    auto qpts = getQNetOtherPts(s); qpts.push_back(coords.at(s));
                    hpwlD_before[s] = dpts.size() > 1 ? hpwl_of_points(dpts) : 0.0;
                    hpwlQ_before[s] = qpts.size() > 1 ? hpwl_of_points(qpts) : 0.0;
                }
                auto perms = allPerms(bit);
                double wQ = params_.hpwlWeightQ;
                double thrD = params_.hpwlThrDMul * dcL;
                double thrQ = params_.hpwlThrQMul * dcL;

                double bestCost = 1e100; int bestAnchor = -1; std::vector<int> bestPerm;

                for (int anchor = 0; anchor < bit; ++anchor) {
                    auto A0 = coords.at(g[anchor]);
                    for (auto& perm : perms) {
                        bool pass = true; double cost = 0.0;
                        for (int j = 0; j < bit; ++j) {
                            const std::string& s = g[perm[j]];
                            auto dpts = getDNetOtherPts(s); dpts.push_back(A0);
                            auto qpts = getQNetOtherPts(s); qpts.push_back(A0);
                            double d_after = dpts.size() > 1 ? hpwl_of_points(dpts)
                                : std::hypot(A0.first - coords.at(s).first, A0.second - coords.at(s).second);
                            double q_after = qpts.size() > 1 ? hpwl_of_points(qpts)
                                : std::hypot(A0.first - coords.at(s).first, A0.second - coords.at(s).second);
                            double d_inc = d_after - hpwlD_before[s];
                            double q_inc = q_after - hpwlQ_before[s];
                            if (d_inc > thrD || q_inc > thrQ) { pass = false; break; }
                            cost += d_inc + wQ * q_inc;
                        }
                        if (pass && cost < bestCost) { bestCost = cost; bestAnchor = anchor; bestPerm = perm; }
                    }
                }
                if (bestAnchor < 0) { ++stats.failHPWL; return false; }

                // === 建立輸出（採最佳 anchor+perm） ===
                ++totalMergeCount;
                MergedFF merged;
                merged.mbffType = bestFF;
                merged.bitwidth = bit;
                merged.mergedFFs = g;
                std::string base = "merged_" + std::to_string(totalMergeCount);
                merged.newInstanceName = (pg.first.empty() ? base : (pg.first + "/" + base));
                auto A0 = coords.at(g[bestAnchor]);
                merged.newX = (int)std::lround(A0.first);
                merged.newY = (int)std::lround(A0.second);

                auto it = allCells.find(bestFF);
                if (it != allCells.end()) {
                    merged.area = static_cast<float>(it->second.area);
                    merged.power = static_cast<float>(it->second.cellLeakagePower);
                }

                for (int i = 0; i < bit; ++i) {
                    std::string d = "D" + std::to_string(i);
                    std::string q = "Q" + std::to_string(i);
                    const std::string& src = g[bestPerm[i]];
                    merged.mbffPinToOrigPin[d] = src + "/D";
                    merged.mbffPinToOrigPin[q] = src + "/Q";
                    merged.mbffPinToOrigFF[d] = src;
                    merged.mbffPinToOrigFF[q] = src;
                }

                // 報表
                std::unordered_map<std::string, int> famCnt;
                std::vector<std::string> sbFamiliesPerMember;
                std::vector<double>      sbCkPerMember;
                sbFamiliesPerMember.reserve(g.size());
                sbCkPerMember.reserve(g.size());
                for (const auto& s : g) {
                    const std::string& sb = instToSB.at(s);
                    ++famCnt[sb];
                    sbFamiliesPerMember.push_back(sb);
                    sbCkPerMember.push_back(libParser_->getClockPinCap(sb));
                }
                CkCapReport rep;
                rep.groupSize = bit;
                rep.sbCell = sbFamiliesPerMember.empty() ? "" : sbFamiliesPerMember.front();
                rep.mbffCell = bestFF;
                rep.ckCapBeforeSum = c_sb_sum;
                rep.ckCapAfter = libParser_->getClockPinCap(bestFF);
                rep.ckCapBefore = c_sb_sum;
                rep.ckCapSaving = rep.ckCapBeforeSum - rep.ckCapAfter;
                rep.members = g;
                for (const auto& kv : famCnt) { rep.sbFamiliesUnique.push_back(kv.first); rep.sbFamilyCounts.push_back(kv); }
                rep.sbMemberCkCaps = std::move(sbCkPerMember);
                ckCapReports_.push_back(std::move(rep));

                mergedFFResults_.push_back(std::move(merged));
                for (const auto& s : g) usedInstances.insert(s);
                if (bit == 4) { ++fourbitFF; ++stats.pass4; }
                else if (bit == 2) { ++twobitFF; ++stats.pass2; }
                return true;
                };

            auto handleGroups = [&](const std::vector<std::vector<std::string>>& groups, int bit) {
                for (const auto& g : groups) {
                    bool anyUsed = false; for (const auto& s : g) if (usedInstances.count(s)) { anyUsed = true; break; }
                    if (anyUsed) continue;

                    if (bit == 4) {
                        bool ok = tryMergeGroup(g, 4);
                        if (!ok) {
                            // 4 -> (2,2) 回退
                            auto pairs = splitBestPairs4(g);
                            for (auto& p : pairs) {
                                bool anyUsed2 = false; for (auto& s : p) if (usedInstances.count(s)) { anyUsed2 = true; break; }
                                if (!anyUsed2) { ++stats.fb22; (void)tryMergeGroup(p, 2); }
                            }
                        }
                    }
                    else {
                        (void)tryMergeGroup(g, 2);
                    }
                }
                };

            // 先 4 再 2
            handleGroups(groups4, 4);
            handleGroups(groups2, 2);

            // 未用者回收
            for (const auto& nm : tmp4) if (!usedInstances.count(nm)) remainingSingleBitFFs.push_back(nm);
        }
    }

    // 建回 mergeMap_
    for (const auto& m : mergedFFResults_) {
        for (const auto& s : m.mergedFFs) mergeMap_.addMapping(s, m.newInstanceName);
    }

    std::cout << "\n[DPC] Found " << mergedFFResults_.size()
        << " merged instances. (4-bit: " << fourbitFF
        << ", 2-bit: " << twobitFF << ")\n";

    std::cout << "[DPC] merge stats: "
        << "row=" << stats.failRow
        << ", compat=" << stats.failCompat
        << ", dist=" << stats.failDist
        << ", ck=" << stats.failCK
        << ", hpwl=" << stats.failHPWL
        << ", pass2=" << stats.pass2
        << ", pass4=" << stats.pass4
        << ", fb22=" << stats.fb22
        << std::endl;
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

    const double dc = (cutoffDistance_ > 1e-9 ? cutoffDistance_ : 1.0);
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

    std::vector<double> ds; ds.reserve(samples);
    unsigned seed = 1234567u;
    auto rnd = [&]() { seed = 1664525u * seed + 1013904223u; return seed; };
    while (ds.size() < samples) {
        int i = (int)(rnd() % N), j = (int)(rnd() % N);
        if (i == j) continue;
        ds.push_back(boxManhattanDistance(points_[i], points_[j]));
    }
    std::sort(ds.begin(), ds.end());

    double p = std::max(0.0005, std::min(0.05, params_.neighborPercent));
    size_t idx = (size_t)std::llround(p * (ds.size() - 1));
    if (idx >= ds.size()) idx = ds.size() - 1;
    return ds[idx];
}



std::vector<DensityPeakClustering::DpcParams> makeFivePresets() {
    using DpcParams = DensityPeakClustering::DpcParams;
    std::vector<DpcParams> v;

    // A) TNS_SAFE —— 最保守（Tapeout 前夜用它）
    {
        DpcParams P;
        P.neighborPercent = 0.012;
        P.kMode = DpcParams::KMode::Sigma;
        P.kSigmaLambda = 1.35;
        P.rhoRadiusMul = 3.0;
        P.deltaStartMul = 1.5;
        P.gridCellMul = 2.5;

        P.sameRowTolMul = 0.50;
        P.dist4_rhoMul = 0.70; P.dist4_dcMul = 1.60;
        P.dist2_rhoMul = 1.10; P.dist2_dcMul = 2.20;

        P.hpwlThrDMul = 1.20;
        P.hpwlThrQMul = 2.00;
        P.hpwlWeightQ = 2.20;

        P.ckSaveMinFrac = 0.020;
        v.push_back(P);
    }

    // B) BALANCED —— 標準預設（安全 + 省功耗兼顧）
    {
        DpcParams P;
        P.neighborPercent = 0.010;
        P.kMode = DpcParams::KMode::Sigma;
        P.kSigmaLambda = 1.10;
        P.rhoRadiusMul = 4.0;
        P.deltaStartMul = 2.0;
        P.gridCellMul = 2.0;

        P.sameRowTolMul = 0.60;
        P.dist4_rhoMul = 0.80; P.dist4_dcMul = 1.80;
        P.dist2_rhoMul = 1.20; P.dist2_dcMul = 2.50;

        P.hpwlThrDMul = 1.50;
        P.hpwlThrQMul = 2.50;
        P.hpwlWeightQ = 1.80;

        P.ckSaveMinFrac = 0.010;
        v.push_back(P);
    }

    //B2 test （Balanced 放鬆版）
    {
        DpcParams P;
        // === DPC 分群：跟 B 很像，只微調 ===
        P.neighborPercent = 0.010;
        P.kMode = DpcParams::KMode::Sigma;
        P.kSigmaLambda = 1.05;   // from 1.10 → 1.05：中心稍微多一點（但不激進）
        P.rhoRadiusMul = 3.8;    // from 4.0：略縮 ρ 半徑，計算更局部
        P.deltaStartMul = 2.0;
        P.gridCellMul = 2.2;    // from 2.0：大一點的格，速度略快

        // === Merge 守門（放鬆）===
        P.sameRowTolMul = 0.70;  // from 0.60：允許跨 row 輕微不齊

        // 2b/4b 群距上限：放寬 ~15–20%
        P.dist4_rhoMul = 0.90;   // from 0.80
        P.dist4_dcMul = 2.00;   // from 1.80
        P.dist2_rhoMul = 1.35;   // from 1.20
        P.dist2_dcMul = 2.80;   // from 2.50

        // ΔHPWL 上限 & 權重：放寬，且降低 Q 權重（但仍 >1）
        P.hpwlThrDMul = 1.80;   // from 1.50
        P.hpwlThrQMul = 3.00;   // from 2.50
        P.hpwlWeightQ = 1.60;   // from 1.80

        // CK-cap 最小節省比例：放寬，允許臨界但仍有正收益
        P.ckSaveMinFrac = 0.007; // from 0.010（0.7% of ΣCsb）
        v.push_back(P);
    }


    // C) POWER_SAVE —— 激進合併（允許多走線、換更多 MBFF）
    {
        DpcParams P;
        P.neighborPercent = 0.008;
        P.kMode = DpcParams::KMode::Sigma;
        P.kSigmaLambda = 0.90;
        P.rhoRadiusMul = 3.5;
        P.deltaStartMul = 1.5;
        P.gridCellMul = 1.8;

        P.sameRowTolMul = 0.65;
        P.dist4_rhoMul = 0.90; P.dist4_dcMul = 2.10;
        P.dist2_rhoMul = 1.40; P.dist2_dcMul = 3.00;

        P.hpwlThrDMul = 2.00;
        P.hpwlThrQMul = 3.20;
        P.hpwlWeightQ = 1.50;

        P.ckSaveMinFrac = 0.005;
        v.push_back(P);
    }

    // D) SPEED —— 大設計加速（較粗網格、較窄半徑）
    {
        DpcParams P;
        P.neighborPercent = 0.010;
        P.kMode = DpcParams::KMode::Sigma;
        P.kSigmaLambda = 1.10;
        P.rhoRadiusMul = 3.2;
        P.deltaStartMul = 2.0;
        P.gridCellMul = 3.0;

        P.sameRowTolMul = 0.55;
        P.dist4_rhoMul = 0.75; P.dist4_dcMul = 1.70;
        P.dist2_rhoMul = 1.15; P.dist2_dcMul = 2.30;

        P.hpwlThrDMul = 1.50;
        P.hpwlThrQMul = 2.40;
        P.hpwlWeightQ = 1.80;

        P.ckSaveMinFrac = 0.010;
        v.push_back(P);
    }

    // C++ —— 更激進（確認 TNS/合法化 OK 才用）
    {
        DpcParams P;
        // DPC 更在地 + 多中心
        P.neighborPercent = 0.008;
        P.kMode = DpcParams::KMode::Sigma;
        P.kSigmaLambda = 0.80;
        P.rhoRadiusMul = 3.2;
        P.deltaStartMul = 1.6;
        P.gridCellMul = 1.6;

        // Merge 守門（較鬆，但仍保 Q 風險）
        P.sameRowTolMul = 0.78;
        P.dist4_rhoMul = 1.10;
        P.dist4_dcMul = 2.40;
        P.dist2_rhoMul = 1.70;
        P.dist2_dcMul = 3.40;

        P.hpwlThrDMul = 2.40;
        P.hpwlThrQMul = 3.80;
        P.hpwlWeightQ = 1.30;

        P.ckSaveMinFrac = 0.002;
        v.push_back(P);
    }

    // C_BOLD —— 更進取的合併（更局部 + 更寬距離/HPWL）
    {
        DpcParams P;
        // —— DPC：更局部、中心更多 —— 
        P.neighborPercent = 0.006;        // C: 0.008 → 更小 dc，更局部
        P.kMode = DpcParams::KMode::Sigma;
        P.kSigmaLambda = 0.80;            // C: 0.90 → 中心挑更鬆
        P.rhoRadiusMul = 3.2;             // C: 3.5 → 更近鄰的密度
        P.deltaStartMul = 1.8;            // C: 1.5~2.0 之間取中
        P.gridCellMul = 1.8;             // 與 C 相同（速度/品質平衡）

        // —— Merge 幾何守門：明顯放寬（仍限制在局部）——
        P.sameRowTolMul = 0.80;           // C: 0.65 → 跨 row 輕微不齊可接受
        P.dist4_rhoMul = 1.00;           // C: 0.90
        P.dist4_dcMul = 2.30;           // C: 2.10
        P.dist2_rhoMul = 1.55;           // C: 1.40
        P.dist2_dcMul = 3.20;           // C: 3.00

        // —— ΔHPWL 守門：再鬆一點，但仍加重 Q —— 
        P.hpwlThrDMul = 2.40;             // C: 2.00
        P.hpwlThrQMul = 3.60;             // C: 3.20
        P.hpwlWeightQ = 1.45;             // C: 1.50（仍>1，避免Q惡化）

        // —— CK-cap 最小節省：收進邊界案，但仍要求正節省 —— 
        P.ckSaveMinFrac = 0.004;          // C: 0.005

        v.push_back(P);
    }
    {  // 建議名稱：TNS_OPT
        DpcParams P;

        // 分群：讓 cluster 變大、鄰域加寬
        P.neighborPercent = 0.1;              // 提高 dc（鄰居分位數）
        P.kMode = DpcParams::KMode::Sigma;
        P.kSigmaLambda = 0.1;                   // 取更少中心 → 大群聚
        P.rhoRadiusMul = 5.0;
        P.deltaStartMul = 3.0;
        P.gridCellMul = 2.5;

        // 幾何守門：大幅放寬（允許跨 row、遠距配對）
        P.sameRowTolMul = 1.50;                  // 容許 ≈1.5× row 高度的 y 差
        P.dist4_rhoMul = 3.50;  P.dist4_dcMul = 5.00;  // 4-bit 允許更遠
        P.dist2_rhoMul = 4.50;  P.dist2_dcMul = 7.00;  // 2-bit 更遠

        // ΔHPWL 與 Q 權重：幾乎不擋（只為了不無限大）
        P.hpwlThrDMul = 6.0;
        P.hpwlThrQMul = 8.0;
        P.hpwlWeightQ = 1.00;                  // 不特別加重 Q

        // CK-cap 最小節省：不要求（只要不變負）
        P.ckSaveMinFrac = 0.000;

        v.push_back(P);
    }

    return v;
}

