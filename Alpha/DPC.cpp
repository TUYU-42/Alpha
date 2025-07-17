#include "dpc.h"
#include "DataStructures.h"

void DensityPeakClustering::performClustering(const std::vector<FlipFlopInfo>& flipFlops, bool autoTune) {
	loadPointsFromFF(flipFlops);
}

void DensityPeakClustering::loadPointsFromFF(const std::vector<FlipFlopInfo>& flipFlops) {
    points_.clear();
    for (const auto& ff : flipFlops) {
        DPCPoint pt;
        pt.instanceName = ff.instName;
        pt.cellType = ff.cellType;
        pt.x = static_cast<double>(ff.x);    // 如果 x/y 是 int 直接轉 double
        pt.y = static_cast<double>(ff.y);
        // pt.rowName = 你有的話可以加
        // pt.width/height = 你有這資訊也可以補
        points_.push_back(pt);
    }
    std::cout << "[DPC] Loaded " << points_.size() << " points from flip-flop info.\n";
}

