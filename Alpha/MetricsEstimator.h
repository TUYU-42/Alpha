#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "DataStructures.h"
#include "LibParser.h"

// 單顆 FF 明細
// 原有的 FFDetail 再加一欄
struct FFDetail {
    std::string instName;
    std::string cellType;
    int         bitWidth = 1;
    double      areaUm2 = 0.0;
    double      leakage = 0.0;
    // ↓↓↓ 新增
    double      ckCap = 0.0;   // 來自 .lib 的 clock pin capacitance（單位依 .lib）
    // 位置/朝向/連網...
    int         x = 0, y = 0;
    std::string orient;
    std::string clockNet, scanIn, scanOut;
};

// 原有的 FFMetrics 再加一欄
struct FFMetrics {
    long long ffInstCount = 0;
    long long sbffInstCount = 0;
    long long mbffInstCount = 0;
    double    totalAreaUm2 = 0.0;
    double    totalLeakage = 0.0;
    // ↓↓↓ 新增
    double    totalCkCap = 0.0;  // sum of clock pin capacitance
};


class FFAreaPowerEstimator {
public:
    // macroMap: cellType -> LefMacroInfo（拿 sizeX*sizeY 當面積 fallback）
    // lib     : 已載入的 LibParser（拿 area/leakage/bitWidth 與判斷 FF）
    explicit FFAreaPowerEstimator(
        const std::unordered_map<std::string, LefMacroInfo>& macroMap,
        const LibParser* lib);

    // 回傳彙總；如提供 details*，會一併填逐顆 FF 的資料
    FFMetrics compute(const DefData& def,
        std::vector<FFDetail>* details = nullptr) const;

    // 列印到 stdout 的簡報（前後版本比較）
    static void printReport(const FFMetrics& before, const FFMetrics& after);

    // 輸出「單一版本」的詳細 TXT 報告（summary + by-type + per-FF）
    // 若 baseline != nullptr，會在檔頭加上 after/before 的比例（%）
    bool writeDetailedReport(const DefData& def,
        const std::string& filename,
        const FFMetrics* baseline = nullptr) const;

private:
    bool   isFF(const std::string& cellType) const;
    bool   isMBFF(const std::string& cellType) const;
    double getCellAreaUm2(const std::string& cellType) const;   // 先 LIB area，fallback LEF sizeX*sizeY
    double getCellLeakage(const std::string& cellType) const;   // 由 LIB cell_leakage_power

    const std::unordered_map<std::string, LefMacroInfo>& macroMap_;
    const LibParser* lib_;
};
