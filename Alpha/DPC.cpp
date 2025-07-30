#include "dpc.h"
#include "DataStructures.h"
#include <iomanip>
#include <iostream>
#include <fstream>
#include <numeric>
#include <set>
#include <map>

DensityPeakClustering::DensityPeakClustering() {}
DensityPeakClustering::~DensityPeakClustering() {}

std::map<std::string, std::vector<DPCCluster>>
DensityPeakClustering::clusterByScanChain(
    const std::map<std::string, std::vector<ScanChainClustered>>& scanChains,
    const std::map<std::string, FlipFlopInfo>& ffLookup,
    const LibParser* libParser,   // 新增這個參數！
    bool autoTune)
{
    std::map<std::string, std::vector<DPCCluster>> result;
    for (const auto& [clockNet, chains] : scanChains) {
        // --- 1. 收集同一 clockNet 下所有 flip-flop ---
        std::vector<FlipFlopInfo> allFFs;
        for (const auto& chain : chains) {
            for (const auto& node : chain.nodes) {
                auto it = ffLookup.find(node.instanceName);
                if (it != ffLookup.end()) {
                    allFFs.push_back(it->second);
                }
            }
        }
        // --- 2. 一次丟進 DPC ---
        performClustering(allFFs, autoTune);

        // --- 3. 輸出所有分群結果 ---
        result[clockNet] = clusters_;
        exportClusteringSummary(clockNet, "my_dpc_output.txt", libParser); // 多傳一個 libParser
    }
    return result;
}






// 可以這樣讓它啥都不做，或者直接不用呼叫：
void DensityPeakClustering::performClusteringOnScanChain(
    const ScanChain& chain,
    const std::map<std::string, FlipFlopInfo>& ffLookup,
    bool autoTune)
{
    // 空實作，因為不需要
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
    computeClusterGeometry();
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
            pt.width = info.sizeX * 1000;
            pt.height = info.sizeY * 1000;
        }
        else {
            pt.width = 0;
            pt.height = 0;
        }
        points_.push_back(pt);
    }
    //std::cout << "[DPC] Loaded " << points_.size() << " points from flip-flop info (with macroMap).\n";
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


void DensityPeakClustering::assignClusters(const std::vector<int>& centerIndices) {
    int N = points_.size();
    // 1. 先把中心點設 clusterId
    for (int idx : centerIndices)
        points_[idx].clusterId = idx;

    // 2. 其他點「沿著 nearestHigher」爬到中心，複製 center 的 clusterId
    for (int i = 0; i < N; ++i) {
        if (points_[i].isCenter) continue;
        int cur = i;
        while (!points_[cur].isCenter) {
            cur = points_[cur].nearestHigher;
            // 預防有死鍊
            if (cur == -1) break;
        }
        points_[i].clusterId = (cur == -1) ? -1 : cur;
    }
}


void DensityPeakClustering::computeClusterGeometry() {
    clusters_.clear();
    // (1) 依 clusterId 分組
    std::map<int, DPCCluster> clusterMap;
    for (int i = 0; i < points_.size(); ++i) {
        int cid = points_[i].clusterId;
        if (cid < 0) continue;
        clusterMap[cid].clusterId = cid;
        clusterMap[cid].members.push_back(i);
        if (points_[cid].isCenter) {
            clusterMap[cid].centerIdx = cid;
        }
    }
    // (2) 計算每群 geometry
    for (auto& [cid, cl] : clusterMap) {
        double sumX = 0, sumY = 0;
        double minX = 1e20, maxX = -1e20, minY = 1e20, maxY = -1e20;
        for (int idx : cl.members) {
            sumX += points_[idx].x;
            sumY += points_[idx].y;
            minX = std::min(minX, points_[idx].x);
            maxX = std::max(maxX, points_[idx].x);
            minY = std::min(minY, points_[idx].y);
            maxY = std::max(maxY, points_[idx].y);
        }
        cl.avgX = sumX / cl.members.size();
        cl.avgY = sumY / cl.members.size();
        cl.minX = minX; cl.maxX = maxX; cl.minY = minY; cl.maxY = maxY;
        // 計算 radius（中心點到最遠成員的距離）
        double r = 0;
        for (int idx : cl.members) {
            double dx = points_[idx].x - cl.avgX;
            double dy = points_[idx].y - cl.avgY;
            r = std::max(r, std::sqrt(dx * dx + dy * dy));
        }
        cl.radius = r;
        clusters_.push_back(cl);
    }
}


void DensityPeakClustering::exportClusteringSummary(
    const std::string& chainId,
    const std::string& filename,
    const LibParser* libParser) const
{
    std::ofstream ofs(filename, std::ios::app); // append 模式
    ofs << "=== DPC Clustering Summary ===\n";
    ofs << "  Scan Chain: " << chainId << "\n";
    ofs << "  Total clusters: " << clusters_.size() << "\n";
    for (int i = 0; i < clusters_.size(); ++i) {
        const auto& c = clusters_[i];
        ofs << "  Cluster #" << i
            << " | clusterId: " << c.clusterId
            << " | Size: " << c.members.size()
            << " | Center: " << points_[c.centerIdx].instanceName
            << " | AvgX=" << c.avgX << ", AvgY=" << c.avgY
            << " | Radius=" << c.radius
            << " | X=[" << c.minX << "," << c.maxX << "]"
            << " | Y=[" << c.minY << "," << c.maxY << "]"
            << "\n";
        ofs << "    Members:\n";
        std::map<std::string, std::vector<std::string>> typeToInstances;
        for (int idx : c.members) {
            const auto& pt = points_[idx];
            ofs << "      - " << pt.instanceName
                << " | cellType: " << pt.cellType
                << " | x=" << pt.x
                << " | y=" << pt.y
                << " | rho=" << pt.rho
                << " | delta=" << pt.delta
                << " | isCenter: " << (pt.isCenter ? "Y" : "N")
                << "\n";
            typeToInstances[pt.cellType].push_back(pt.instanceName);
        }
        ofs << "    [Banking Candidates]\n";
        for (const auto& [cellType, instList] : typeToInstances) {
            const LibCell* cell = libParser ? libParser->getCell(cellType) : nullptr;
            if (!cell || cell->bitWidth != 1) continue;
            int total = instList.size();
            int group_id = 1;
            // 先 4-bit
            for (int i = 0; i + 4 <= total; i += 4) {
                std::string mb4 = libParser ? libParser->getmultibitff4(cellType) : "";
                ofs << "      [MBFF-4] ";
                for (int j = 0; j < 4; ++j) ofs << instList[i + j] << " ";
                ofs << "| mbff_cell: " << (mb4.empty() ? "[無對應4bit]" : mb4)
                    << " | group#" << group_id++ << "\n";
            }
            // 剩下 2-bit
            int remain = total % 4;
            for (int i = total - remain; i + 2 <= total; i += 2) {
                std::string mb2 = libParser ? libParser->getmultibitff2(cellType) : "";
                ofs << "      [MBFF-2] ";
                for (int j = 0; j < 2; ++j) ofs << instList[i + j] << " ";
                ofs << "| mbff_cell: " << (mb2.empty() ? "[無對應2bit]" : mb2)
                    << " | group#" << group_id++ << "\n";
            }
        }
        ofs << "\n";
    }
    ofs << "\n";
}

std::vector<MBFFInstance>
DensityPeakClustering::generateBankingResults(
    const std::map<std::string, std::vector<DPCCluster>>& clusterResult,
    const std::map<std::string, FlipFlopInfo>& ffLookup,
    const LibParser* libParser,
    const WeightParser* weights_) const
{
    std::vector<MBFFInstance> bankingList;
    std::vector<std::string> remainingSingleBitFFs; // 記錄剩餘的1-bit FF

    if (!libParser) {
        std::cerr << "[DPC] Error: libParser is not set!\n";
        return bankingList;
    }

    std::cout << "\n=== [DPC] Generate Banking Results ===\n";

    const auto& allCells = libParser->getAllCells();
    int totalClustersWithMerges = 0;
    int totalMergePairs = 0;

    auto getBitWidthFromName = [](const std::string& name) -> int {
        if (name.find("16_") != std::string::npos) return 16;
        if (name.find("8_") != std::string::npos) return 8;
        if (name.find("4_") != std::string::npos) return 4;
        if (name.find("2_") != std::string::npos) return 2;
        return 1;
        };

    // 計算兩個 FF 之間的歐式距離
    auto calculateDistance = [&ffLookup](const std::string& ff1, const std::string& ff2) -> double {
        auto it1 = ffLookup.find(ff1);
        auto it2 = ffLookup.find(ff2);
        if (it1 == ffLookup.end() || it2 == ffLookup.end()) return 1e9;

        double dx = it1->second.x - it2->second.x;
        double dy = it1->second.y - it2->second.y;
        return std::sqrt(dx * dx + dy * dy);
        };

    // 通用的找最近N個FF組合的函數
    auto findClosestNGroup = [&](const std::vector<std::string>& instList, int N) -> std::vector<std::string> {
        if (instList.size() < N) return {};

        double minTotalDist = 1e9;
        std::vector<std::string> bestGroup;

        // 生成所有N個FF的組合
        std::vector<int> indices(instList.size());
        std::iota(indices.begin(), indices.end(), 0);

        std::vector<bool> selector(instList.size(), false);
        std::fill(selector.begin(), selector.begin() + N, true);

        do {
            std::vector<std::string> group;
            for (int i = 0; i < instList.size(); ++i) {
                if (selector[i]) {
                    group.push_back(instList[i]);
                }
            }

            // 計算這個組合的總距離（所有pair之間的距離和）
            double totalDist = 0;
            for (int a = 0; a < N; ++a) {
                for (int b = a + 1; b < N; ++b) {
                    totalDist += calculateDistance(group[a], group[b]);
                }
            }

            if (totalDist < minTotalDist) {
                minTotalDist = totalDist;
                bestGroup = group;
            }
        } while (std::prev_permutation(selector.begin(), selector.end()));

        return bestGroup;
        };

    // 確定最優的分組策略
    auto determineOptimalGroupingStrategy = [&](int totalCount, const std::vector<std::string>& mbffCandidates) -> std::vector<int> {
        // 找出可用的最大bit width
        std::vector<int> availableBitWidths;
        for (const auto& mbff : mbffCandidates) {
            int bitWidth = getBitWidthFromName(mbff);
            if (std::find(availableBitWidths.begin(), availableBitWidths.end(), bitWidth) == availableBitWidths.end()) {
                availableBitWidths.push_back(bitWidth);
            }
        }
        std::sort(availableBitWidths.rbegin(), availableBitWidths.rend()); // 降序排列

        std::vector<int> strategy;
        int remaining = totalCount;

        // 貪心策略：優先使用最大的bit width
        for (int bitWidth : availableBitWidths) {
            if (bitWidth == 1) continue; // 跳過1-bit，因為我們不合併單個FF

            while (remaining >= bitWidth) {
                strategy.push_back(bitWidth);
                remaining -= bitWidth;
            }
        }

        return strategy;
        };

    // 從列表中移除指定的元素
    auto removeFromList = [](std::vector<std::string>& list, const std::vector<std::string>& toRemove) {
        for (const auto& item : toRemove) {
            list.erase(std::remove(list.begin(), list.end(), item), list.end());
        }
        };

    // 遍歷所有 clockNet 的 cluster 結果
    for (const auto& [clockNet, clusters] : clusterResult) {
        std::cout << "\nProcessing Clock Net: " << clockNet << "\n";

        for (const auto& cluster : clusters) {
            std::map<std::string, std::vector<std::string>> sbffToInstances;

            // 根據 cluster.members 收集 flip-flop 資訊
            for (int idx : cluster.members) {
                if (idx < points_.size()) {
                    const auto& pt = points_[idx];
                    auto ffIt = ffLookup.find(pt.instanceName);
                    if (ffIt != ffLookup.end()) {
                        std::string sbff = libParser->getSingleBitDegenerate(ffIt->second.cellType);
                        if (sbff.empty()) sbff = ffIt->second.cellType;
                        sbffToInstances[sbff].push_back(pt.instanceName);
                    }
                }
            }

            bool clusterPrinted = false;

            for (const auto& [sbff, originalInstList] : sbffToInstances) {
                if (originalInstList.size() < 2) {
                    // 單個FF直接加入剩餘列表
                    remainingSingleBitFFs.insert(remainingSingleBitFFs.end(),
                        originalInstList.begin(), originalInstList.end());
                    continue;
                }

                std::vector<std::string> mbffCandidates;
                for (const auto& [cellName, cell] : allCells) {
                    if (cell.singleBitDegenerate == sbff)
                        mbffCandidates.push_back(cellName);
                }

                if (mbffCandidates.empty()) {
                    remainingSingleBitFFs.insert(remainingSingleBitFFs.end(),
                        originalInstList.begin(), originalInstList.end());
                    continue;
                }

                if (!clusterPrinted) {
                    std::cout << "\nCluster #" << cluster.clusterId << ":\n";
                    clusterPrinted = true;
                    totalClustersWithMerges++;
                }

                std::cout << "  SBFF Type: " << sbff << "\n";
                std::cout << "    Instance Count: " << originalInstList.size() << "\n";

                // 複製列表進行處理
                std::vector<std::string> instList = originalInstList;
                int groupId = 1;

                // 確定最優的分組策略
                std::vector<int> groupingStrategy = determineOptimalGroupingStrategy(instList.size(), mbffCandidates);

                std::cout << "    Optimal grouping strategy for " << instList.size() << " FFs: ";
                for (int bitWidth : groupingStrategy) {
                    std::cout << bitWidth << "-bit ";
                }
                std::cout << "\n";

                // 按照策略進行分組
                for (int targetBitWidth : groupingStrategy) {
                    if (instList.size() < targetBitWidth) continue;

                    // 找最近的N個FF
                    std::vector<std::string> groupN = findClosestNGroup(instList, targetBitWidth);
                    if (groupN.empty()) continue;

                    // 找最佳N-bit MBFF
                    std::string bestFF;
                    double bestMetric = 1e9;
                    for (const auto& mbff : mbffCandidates) {
                        const auto& cell = allCells.at(mbff);
                        if (getBitWidthFromName(mbff) == targetBitWidth) {
                            double metric = (weights_ && weights_->getBeta() / weights_->getGamma() > 3000)
                                ? cell.cellLeakagePower : cell.area;
                            if (metric < bestMetric) {
                                bestMetric = metric;
                                bestFF = mbff;
                            }
                        }
                    }

                    if (bestFF.empty()) continue;

                    std::cout << "      → [";
                    for (int i = 0; i < groupN.size(); ++i) {
                        if (i > 0) std::cout << ", ";
                        std::cout << groupN[i];
                    }
                    std::cout << "] → " << bestFF << " (" << targetBitWidth << "-bit, group#" << groupId++ << ")\n";
                    totalMergePairs++;

                    // 創建 MBFFInstance
                    MBFFInstance mbffInst;
                    static int global_mbff_id = 1;

                    mbffInst.newInstanceName = "merged_" + std::to_string(global_mbff_id++) +
                        "_" + std::to_string(targetBitWidth) + "bit";

                    mbffInst.mbffCellType = bestFF;
                    mbffInst.mergedFFs = groupN;
                    mbffInst.bitWidth = targetBitWidth;

                    // 計算質心座標
                    int sumX = 0, sumY = 0;
                    for (const auto& name : groupN) {
                        auto ffIt = ffLookup.find(name);
                        if (ffIt != ffLookup.end()) {
                            sumX += ffIt->second.x;
                            sumY += ffIt->second.y;
                        }
                    }

                    mbffInst.x = sumX / targetBitWidth;
                    mbffInst.y = sumY / targetBitWidth;

                    //pin map
                    const auto& mbffMacro = macroMap_->at(mbffInst.mbffCellType).pins;
                    for (int i = 0; i < mbffInst.mergedFFs.size(); ++i) {
                        std::string d_pin = "D" + std::to_string(i);
                        std::string q_pin = "Q" + std::to_string(i);
                        mbffInst.mbffPinToOrigPin[d_pin] = mbffInst.mergedFFs[i] + "/D";
                        mbffInst.mbffPinToOrigPin[q_pin] = mbffInst.mergedFFs[i] + "/Q";
                        mbffInst.mbffPinToOrigFF[d_pin] = mbffInst.mergedFFs[i];
                        mbffInst.mbffPinToOrigFF[q_pin] = mbffInst.mergedFFs[i];
                    }

                    bankingList.push_back(mbffInst);
                    removeFromList(instList, groupN);
                }

                // 剩餘的單個FF加入剩餘列表
                if (!instList.empty()) {
                    std::cout << "    Remaining single-bit FFs: ";
                    for (const auto& ff : instList) {
                        std::cout << ff << " ";
                    }
                    std::cout << "\n";
                    remainingSingleBitFFs.insert(remainingSingleBitFFs.end(),
                        instList.begin(), instList.end());
                }
            }
        }
    }

    // ============= 新增部分：將剩餘的1-bit FF加入結果 =============
    std::cout << "\n=== Processing Remaining Single-bit FFs ===\n";
    std::cout << "Total remaining single-bit FFs: " << remainingSingleBitFFs.size() << "\n";

    // 為每個剩餘的1-bit FF創建一個MBFFInstance
    for (const auto& ffName : remainingSingleBitFFs) {
        auto ffIt = ffLookup.find(ffName);
        if (ffIt != ffLookup.end()) {
            const auto& ffInfo = ffIt->second;

            MBFFInstance singleBitInst;
            singleBitInst.newInstanceName = ffName;   // 保持原本的instance name
            singleBitInst.mbffCellType = ffInfo.cellType; // 保持原本的cell type
            singleBitInst.mergedFFs.push_back(ffName);    // 只包含自己
            singleBitInst.bitWidth = 1;                   // 1-bit
            singleBitInst.orientation = ffInfo.orient;
            singleBitInst.x = ffInfo.x;                // 使用原本的座標
            singleBitInst.y = ffInfo.y;

            bankingList.push_back(singleBitInst);
        }
    }

    std::cout << "  All " << remainingSingleBitFFs.size()
        << " single-bit FFs added with original instance names\n";
    // =========================================================

    // ============= 統計不同bit width的banking結果 =============
    std::map<int, int> bitWidthCount;
    std::map<int, int> ffCount; // 統計每種bit width包含的FF總數

    for (const auto& mbff : bankingList) {
        bitWidthCount[mbff.bitWidth]++;
        ffCount[mbff.bitWidth] += mbff.bitWidth; // 每個instance包含的FF數量 = bitWidth
    }

    std::cout << "\n=== Banking Results Summary ===\n";
    std::cout << "Total banking instances: " << bankingList.size() << "\n";
    std::cout << "Bit-width distribution:\n";

    int totalOriginalFFs = 0;
    for (const auto& [bitWidth, count] : bitWidthCount) {
        int totalFFsInThisBitWidth = ffCount[bitWidth];
        totalOriginalFFs += totalFFsInThisBitWidth;

        std::cout << "  " << bitWidth << "-bit MBFF: " << count << " instances"
            << " (containing " << totalFFsInThisBitWidth << " FFs)\n";
    }

    std::cout << "Total FFs processed: " << totalOriginalFFs << "\n";

    // 計算banking效率
    int mergedFFs = totalOriginalFFs - bitWidthCount[1]; // 扣掉1-bit的
    double bankingEfficiency = (totalOriginalFFs > 0) ?
        (100.0 * mergedFFs / totalOriginalFFs) : 0.0;

    std::cout << "Banking efficiency: " << std::fixed << std::setprecision(1)
        << bankingEfficiency << "% (" << mergedFFs << "/" << totalOriginalFFs
        << " FFs successfully merged)\n";

    std::cout << "\n[DPC] Found " << totalClustersWithMerges << " clusters with mergeable FFs.\n";
    std::cout << "[DPC] Total mergeable FF pairs: " << totalMergePairs << "\n";

    return bankingList;
}



void DensityPeakClustering::exportBankingDebugReport(
    const std::vector<MBFFInstance>& bankingList,
    const std::string& filename,
    const std::map<std::string, FlipFlopInfo>& ffLookup,
    const LibParser* libParser) const
{
    std::ofstream ofs(filename);
    if (!ofs) {
        std::cerr << "[DPC][BankingDebug] Failed to open file: " << filename << std::endl;
        return;
    }

    int cnt16 = 0, cnt8 = 0, cnt4 = 0, cnt2 = 0;
    for (const auto& mb : bankingList) {
        if (mb.bitWidth == 16) ++cnt16;
        else if (mb.bitWidth == 8) ++cnt8;
        else if (mb.bitWidth == 4) ++cnt4;
        else if (mb.bitWidth == 2) ++cnt2;
    }

    // 計算剩餘的1-bit FF數量（需要從外部傳入或在類中維護）
    int remainingSingleBits = 0;

    // 遍歷所有原始FF，計算哪些沒有被合併
    std::set<std::string> mergedFFs;
    for (const auto& mb : bankingList) {
        for (const auto& ff : mb.mergedFFs) {
            mergedFFs.insert(ff);
        }
    }

    for (const auto& [name, info] : ffLookup) {
        if (mergedFFs.find(name) == mergedFFs.end()) {
            remainingSingleBits++;
        }
    }

    ofs << "=== DPC Banking Debug Report ===\n";
    ofs << "Total Merged MBFF Instances: " << bankingList.size() << "\n";
    ofs << "  [16bit MBFF]: " << cnt16 << "\n";
    ofs << "  [8bit MBFF]: " << cnt8 << "\n";
    ofs << "  [4bit MBFF]: " << cnt4 << "\n";
    ofs << "  [2bit MBFF]: " << cnt2 << "\n";
    ofs << "  [Remaining Single-bit FF]: " << remainingSingleBits << "\n\n";

    ofs << "Banking Summary:\n";
    ofs << "  Original FFs: " << ffLookup.size() << "\n";
    ofs << "  Merged into 16bit: " << cnt16 * 16 << " FFs\n";
    ofs << "  Merged into 8bit: " << cnt8 * 8 << " FFs\n";
    ofs << "  Merged into 4bit: " << cnt4 * 4 << " FFs\n";
    ofs << "  Merged into 2bit: " << cnt2 * 2 << " FFs\n";
    ofs << "  Remaining single: " << remainingSingleBits << " FFs\n";
    ofs << "  Banking efficiency: " << std::fixed << std::setprecision(1)
        << (100.0 * (cnt16 * 16 + cnt8 * 8 + cnt4 * 4 + cnt2 * 2) / ffLookup.size()) << "%\n\n";

    int idx = 1;
    for (const auto& mb : bankingList) {
        ofs << "MBFF #" << idx++ << "\n";
        ofs << "  Instance name   : " << mb.newInstanceName << "\n";
        ofs << "  MBFF cell type  : " << mb.mbffCellType
            << "  (bitWidth=" << mb.bitWidth;
        if (mb.bitWidth == 16) ofs << " [16bit])";
        else if (mb.bitWidth == 8) ofs << " [8bit])";
        else if (mb.bitWidth == 4) ofs << " [4bit])";
        else if (mb.bitWidth == 2) ofs << " [2bit])";
        else ofs << ")";
        ofs << "\n";
        ofs << "  Centroid        : (" << mb.x << ", " << mb.y << ")\n";
        ofs << "  Merged FFs      : ";
        for (const auto& ff : mb.mergedFFs)
            ofs << ff << " ";
        ofs << "\n";

        // Pin map debug info
        if (!mb.mbffPinToOrigPin.empty()) {
            ofs << "  [Pin Mapping]\n";
            for (const auto& [mbffPin, origPin] : mb.mbffPinToOrigPin) {
                ofs << "    " << mbffPin << " -> " << origPin << "\n";
            }
        }

        // 額外細節
        ofs << "  Original FF cell types  : ";
        std::set<std::string> types, degens;
        for (const auto& ff : mb.mergedFFs) {
            auto it = ffLookup.find(ff);
            if (it != ffLookup.end()) {
                types.insert(it->second.cellType);
                // 查 single_bit_degenerate
                if (libParser) {
                    std::string deg = libParser->getSingleBitDegenerate(it->second.cellType);
                    if (!deg.empty()) degens.insert(deg);
                }
            }
        }
        for (auto& t : types) ofs << t << " ";
        ofs << "\n";

        ofs << "  single_bit_degenerate  : ";
        for (auto& d : degens) ofs << d << " ";
        ofs << "\n";

        // 可選：cell 面積/功耗
        if (libParser) {
            const LibCell* cell = libParser->getCell(mb.mbffCellType);
            if (cell) {
                ofs << "  MBFF area     : " << cell->area << "\n";
                ofs << "  MBFF leakage  : " << cell->cellLeakagePower << "\n";
            }
        }

        // 計算組內 FF 最大/最小歐式距離
        double maxDist = 0, minDist = 1e9;
        for (size_t i = 0; i < mb.mergedFFs.size(); ++i) {
            for (size_t j = i + 1; j < mb.mergedFFs.size(); ++j) {
                auto it1 = ffLookup.find(mb.mergedFFs[i]);
                auto it2 = ffLookup.find(mb.mergedFFs[j]);
                if (it1 != ffLookup.end() && it2 != ffLookup.end()) {
                    double dx = it1->second.x - it2->second.x;
                    double dy = it1->second.y - it2->second.y;
                    double dist = std::sqrt(dx * dx + dy * dy);
                    maxDist = std::max(maxDist, dist);
                    minDist = std::min(minDist, dist);
                }
            }
        }
        ofs << "  Intra-FF dist  : min = " << minDist << ", max = " << maxDist << "\n";

        ofs << "-----------------------------------\n";
    }

    // 列出所有剩餘的單bit FF
    ofs << "\n=== Remaining Single-bit FFs ===\n";
    ofs << "Count: " << remainingSingleBits << "\n";
    for (const auto& [name, info] : ffLookup) {
        if (mergedFFs.find(name) == mergedFFs.end()) {
            ofs << "  " << name << " | " << info.cellType
                << " | (" << info.x << ", " << info.y << ")\n";
        }
    }
}


std::vector<MBFFInstance> DensityPeakClustering::clusterByAllFFs(
    const std::vector<FlipFlopInfo>& ffList,
    const LibParser* libParser,
    const WeightParser* weights)
{
    // 做 clustering
    performClustering(ffList, true); // 你也可以自己選 autoTune
    // 準備 lookup
    std::map<std::string, FlipFlopInfo> ffLookup;
    for (const auto& ff : ffList) ffLookup[ff.instName] = ff;
    // 組 fake cluster 結果
    std::map<std::string, std::vector<DPCCluster>> singleClusterMap;
    singleClusterMap["ALL_FF"] = clusters_;
    // 跑 banking
    return generateBankingResults(singleClusterMap, ffLookup, libParser, weights);
}
