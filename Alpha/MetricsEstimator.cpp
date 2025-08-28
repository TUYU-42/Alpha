#include "MetricsEstimator.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <iomanip>
#include <cctype>     // for ::toupper
#include <string>
#include <vector>

using std::string;

FFAreaPowerEstimator::FFAreaPowerEstimator(
    const std::unordered_map<std::string, LefMacroInfo>& macroMap,
    const LibParser* lib)
    : macroMap_(macroMap), lib_(lib) {
}

// 判斷是不是 FF（寬鬆）：先看 .lib 的 hasFF 或退化資訊；否則用名稱啟發式
bool FFAreaPowerEstimator::isFF(const std::string& cellType) const {
    // 1) 若 LIB 有詳細資訊，優先
    if (lib_) {
        if (const LibCell* c = lib_->getCell(cellType)) {
            if (c->hasFF || !c->singleBitDegenerate.empty()) return true;
        }
        // 位寬 >= 1 再配合名稱啟發式
        if (lib_->getCellBitWidth(cellType) >= 1) {
            string up = cellType;
            std::transform(up.begin(), up.end(), up.begin(), ::toupper);
            if (up.find("FF") != string::npos || up.find("DFF") != string::npos ||
                up.find("SDFF") != string::npos || up.find("FSD") != string::npos ||
                up.find("FLOP") != string::npos || up.find("REG") != string::npos ||
                up.find("LATCH") != string::npos) {
                return true;
            }
        }
    }
    // 2) 後援：名稱啟發式
    string up = cellType;
    std::transform(up.begin(), up.end(), up.begin(), ::toupper);
    return (up.find("FF") != string::npos ||
        up.find("DFF") != string::npos ||
        up.find("SDFF") != string::npos ||
        up.find("FSD") != string::npos ||
        up.find("FLOP") != string::npos ||
        up.find("REG") != string::npos ||
        up.find("LATCH") != string::npos);
}

// 嚴格：只有當 .lib 的位寬 > 1 且有 singleBitDegenerate（可退化為單 bit）才視為 MBFF
bool FFAreaPowerEstimator::isMBFF(const std::string& cellType) const {
    if (!lib_) return false;                  // 沒 .lib 不猜
    int w = lib_->getCellBitWidth(cellType);
    if (w <= 1) return false;                 // 位寬不大於 1 → SBFF
    if (const LibCell* c = lib_->getCell(cellType)) {
        if (!c->singleBitDegenerate.empty())  // 有退化資訊才承認是 MBFF
            return true;
    }
    return false;
}

// 從 .lib 或 LEF 取 cell 面積（um^2）
double FFAreaPowerEstimator::getCellAreaUm2(const std::string& cellType) const {
    if (lib_) {
        if (const LibCell* c = lib_->getCell(cellType)) {
            if (c->area > 0.0) return c->area; // 多半是 um^2
        }
    }
    // fallback：LEF SIZE X BY Y（um）
    auto it = macroMap_.find(cellType);
    if (it != macroMap_.end()) {
        const auto& m = it->second;
        if (m.sizeX > 0.0 && m.sizeY > 0.0) return m.sizeX * m.sizeY;
    }
    return 0.0;
}

// 從 .lib 取 cellLeakagePower（單位依 .lib）
double FFAreaPowerEstimator::getCellLeakage(const std::string& cellType) const {
    if (lib_) {
        if (const LibCell* c = lib_->getCell(cellType)) {
            if (c->cellLeakagePower > 0.0) return c->cellLeakagePower;
        }
    }
    return 0.0;
}

FFMetrics FFAreaPowerEstimator::compute(const DefData& def,
    std::vector<FFDetail>* details) const {
    FFMetrics m;

    // 建 instance → Component 對照（拿位置/朝向）
    std::unordered_map<string, const ComponentInfo*> compLut;
    compLut.reserve(def.components.size());
    for (const auto& c : def.components) compLut[c.name] = &c;

    auto push_one = [&](const string& inst, const string& cellType,
        int bitWidthHint,
        const ComponentInfo* compPtr,
        const FlipFlopInfo* ffPtr) {
            if (!isFF(cellType)) return;

            // 位寬：以 .lib 為主，失敗時回到 1
            int bw = 1;
            if (lib_) {
                int w = lib_->getCellBitWidth(cellType);
                if (w > 0) bw = w;
            }
            // 若 .lib 沒給出有效位寬，可用呼叫者傳入的 hint（>0）兜底
            if (bw <= 0 && bitWidthHint > 0) bw = bitWidthHint;
            if (bw <= 0) bw = 1;

            m.ffInstCount++;
            if (isMBFF(cellType)) m.mbffInstCount++;
            else                  m.sbffInstCount++;

            double a = getCellAreaUm2(cellType);
            double p = getCellLeakage(cellType);
            double c = (lib_ ? lib_->getClockPinCap(cellType) : 0.0); // clock pin cap

            m.totalAreaUm2 += a;
            m.totalLeakage += p;
            m.totalCkCap += c;

            if (details) {
                FFDetail d;
                d.instName = inst;
                d.cellType = cellType;
                d.bitWidth = bw;
                d.areaUm2 = a;
                d.leakage = p;
                d.ckCap = c;

                if (compPtr) { d.x = compPtr->x; d.y = compPtr->y; d.orient = compPtr->orient; }
                else if (ffPtr) { d.x = ffPtr->x; d.y = ffPtr->y; d.orient = ffPtr->orient; }

                if (ffPtr) { d.clockNet = ffPtr->clockNet; d.scanIn = ffPtr->scanIn; d.scanOut = ffPtr->scanOut; }
                details->push_back(d);
            }
        };

    if (!def.flipFlops.empty()) {
        // 有 FlipFlopInfo，拿得到 bitWidth/clock/scan
        for (const auto& ff : def.flipFlops) {
            const ComponentInfo* compPtr = nullptr;
            auto it = compLut.find(ff.instName);
            if (it != compLut.end()) compPtr = it->second;

            int hint = (ff.bitWidth > 0 ? ff.bitWidth : 1);
            push_one(ff.instName, ff.cellType, hint, compPtr, &ff);
        }
    }
    else {
        // 沒有 FlipFlopInfo，就從 components 掃
        for (const auto& c : def.components) {
            int hint = 1;
            if (lib_) {
                int w = lib_->getCellBitWidth(c.cellType);
                if (w > 0) hint = w;
            }
            push_one(c.name, c.cellType, hint, &c, nullptr);
        }
    }

    return m;
}

static inline double pctImprove(double before, double after) {
    if (before <= 0.0) return 0.0;
    return (before - after) / before * 100.0;
}

void FFAreaPowerEstimator::printReport(const FFMetrics& before, const FFMetrics& after) {
    std::cout << "\n==== FF Area/Leakage/CKCap Estimation ====\n";
    std::cout << "Before  : FFs=" << before.ffInstCount
        << " (SB=" << before.sbffInstCount
        << ", MB=" << before.mbffInstCount << ")\n";
    std::cout << "          Area(um^2)=" << before.totalAreaUm2
        << ", Leakage=" << before.totalLeakage
        << ", CkCap(sum)=" << before.totalCkCap << "\n";
    std::cout << "After   : FFs=" << after.ffInstCount
        << " (SB=" << after.sbffInstCount
        << ", MB=" << after.mbffInstCount << ")\n";
    std::cout << "          Area(um^2)=" << after.totalAreaUm2
        << ", Leakage=" << after.totalLeakage
        << ", CkCap(sum)=" << after.totalCkCap << "\n";

    auto imp = [](double b, double a) {
        if (b <= 0.0) return 0.0;
        return (b - a) / b * 100.0;
        };

    double areaImp = imp(before.totalAreaUm2, after.totalAreaUm2);
    double leakImp = imp(before.totalLeakage, after.totalLeakage);
    double ckImp = imp(before.totalCkCap, after.totalCkCap);

    std::cout << "\nImprovement (%):\n";
    std::cout << "  Area    : " << areaImp << "%\n";
    std::cout << "  Leakage : " << leakImp << "%\n";
    std::cout << "  CkCap   : " << ckImp << "%\n";
    std::cout << "==========================================\n";
}

bool FFAreaPowerEstimator::writeDetailedReport(const DefData& def,
    const std::string& filename,
    const FFMetrics* baseline) const {
    std::vector<FFDetail> rows;
    FFMetrics cur = compute(def, &rows);

    // 依 cellType 彙總
    struct Agg { int cnt = 0; double area = 0.0; double pwr = 0.0; int mb = 0; int sb = 0; double ck = 0.0; };
    std::map<string, Agg> byType;
    for (const auto& d : rows) {
        auto& a = byType[d.cellType];
        a.cnt++;
        if (isMBFF(d.cellType)) a.mb++; else a.sb++;   // 這裡用嚴格 MBFF 判斷
        a.area += d.areaUm2;
        a.pwr += d.leakage;
        a.ck += d.ckCap;
    }

    // 依 cellType, instName 排序
    std::sort(rows.begin(), rows.end(),
        [](const FFDetail& x, const FFDetail& y) {
            if (x.cellType != y.cellType) return x.cellType < y.cellType;
            return x.instName < y.instName;
        });

    std::ofstream ofs(filename);
    if (!ofs) return false;

    ofs << "==== FF Area/Leakage/CKCap Detailed Report ====\n";
    ofs << "Units: area=um^2, leakage=.lib unit (sum as-is), ckcap=.lib unit (sum as-is)\n\n";

    // 若有 baseline，輸出 ratio（after/before）
    if (baseline) {
        double areaRatio = (baseline->totalAreaUm2 > 0.0)
            ? (cur.totalAreaUm2 / baseline->totalAreaUm2 * 100.0) : 0.0;
        double pwrRatio = (baseline->totalLeakage > 0.0)
            ? (cur.totalLeakage / baseline->totalLeakage * 100.0) : 0.0;
        double ckRatio = (baseline->totalCkCap > 0.0)
            ? (cur.totalCkCap / baseline->totalCkCap * 100.0) : 0.0;

        ofs << "[Compared to baseline]\n";
        ofs << "Area ratio (after/before) % = " << areaRatio << "%\n";
        ofs << "Leakage ratio (after/before) % = " << pwrRatio << "%\n";
        ofs << "CkCap ratio (after/before) % = " << ckRatio << "%\n\n";
    }

    // Summary
    ofs << "[Summary]\n";
    ofs << "FF count=" << cur.ffInstCount
        << " (SB=" << cur.sbffInstCount
        << ", MB=" << cur.mbffInstCount << ")\n";
    ofs << "Total Area(um^2)=" << std::fixed << std::setprecision(3) << cur.totalAreaUm2 << "\n";
    ofs << "Total Leakage   =" << std::fixed << std::setprecision(6) << cur.totalLeakage << "\n";
    ofs << "Total CkCap     =" << std::fixed << std::setprecision(6) << cur.totalCkCap << "\n\n";

    // By Cell Type
    ofs << "[By Cell Type]\n";
    ofs << std::left
        << std::setw(36) << "CellType"
        << std::right
        << std::setw(10) << "Count"
        << std::setw(8) << "SB"
        << std::setw(8) << "MB"
        << std::setw(18) << "Area(um^2)"
        << std::setw(18) << "Leakage"
        << std::setw(18) << "CkCap"
        << "\n";
    ofs << std::string(36 + 10 + 8 + 8 + 18 + 18 + 18, '-') << "\n";
    for (const auto& kv : byType) {
        const auto& t = kv.first;
        const auto& a = kv.second;
        ofs << std::left << std::setw(36) << t
            << std::right << std::setw(10) << a.cnt
            << std::setw(8) << a.sb
            << std::setw(8) << a.mb
            << std::setw(18) << std::fixed << std::setprecision(3) << a.area
            << std::setw(18) << std::fixed << std::setprecision(6) << a.pwr
            << std::setw(18) << std::fixed << std::setprecision(6) << a.ck
            << "\n";
    }
    ofs << "\n";

    // Per-FF details
    ofs << "[Per-FF Details]\n";
    ofs << std::left
        << std::setw(32) << "Instance"
        << std::setw(36) << "CellType"
        << std::right
        << std::setw(6) << "BW"
        << std::setw(16) << "Area(um^2)"
        << std::setw(16) << "Leakage"
        << std::setw(16) << "CkCap"
        << std::setw(9) << "X"
        << std::setw(9) << "Y"
        << std::setw(8) << "Orient"
        << std::setw(16) << "Clock"
        << std::setw(16) << "ScanIn"
        << std::setw(16) << "ScanOut"
        << "\n";
    ofs << std::string(32 + 36 + 6 + 16 + 16 + 16 + 9 + 9 + 8 + 16 + 16 + 16, '-') << "\n";

    for (const auto& d : rows) {
        ofs << std::left << std::setw(32) << d.instName
            << std::setw(36) << d.cellType
            << std::right << std::setw(6) << d.bitWidth
            << std::setw(16) << std::fixed << std::setprecision(3) << d.areaUm2
            << std::setw(16) << std::fixed << std::setprecision(6) << d.leakage
            << std::setw(16) << std::fixed << std::setprecision(6) << d.ckCap
            << std::setw(9) << d.x
            << std::setw(9) << d.y
            << std::setw(8) << (d.orient.empty() ? "N" : d.orient)
            << std::setw(16) << (d.clockNet.empty() ? "-" : d.clockNet)
            << std::setw(16) << (d.scanIn.empty() ? "-" : d.scanIn)
            << std::setw(16) << (d.scanOut.empty() ? "-" : d.scanOut)
            << "\n";
    }

    ofs << "\n==== End of Report ====\n";
    ofs.close();
    return true;
}
