#include"Firefly.h"
#include <algorithm>
#include <iostream>
#include <set>
#include <cmath>
#include<unordered_map>
#include<utility>
#include<sstream>
#include<iomanip>


using namespace std;
void MergeMapping::addMapping(const std::string& single, const std::string& multi) {
	singleToMultiBitName[single] = multi;
	std::vector<std::string>& vec = multiBitToSingles[multi];
	if (std::find(vec.begin(), vec.end(), single) == vec.end()) vec.push_back(single);
}
// 檔案頂端 helper
static inline std::string fullNameOf(const MergedFF& m) {
	return m.hierPrefix.empty() ? m.newInstanceName : (m.hierPrefix + "/" + m.newInstanceName);
}



bool MergeMapping::isMerged(const std::string& single) const {
	return singleToMultiBitName.find(single) != singleToMultiBitName.end();
}


std::string MergeMapping::getMergedName(const std::string& single) const {
	std::unordered_map<std::string, std::string>::const_iterator it = singleToMultiBitName.find(single);
	return (it == singleToMultiBitName.end()) ? std::string() : it->second;
}


std::vector<std::string> MergeMapping::getSingleBits(const std::string& mbffName) const {
	std::unordered_map<std::string, std::vector<std::string> >::const_iterator it = multiBitToSingles.find(mbffName);
	if (it == multiBitToSingles.end()) return {};
	return it->second;
}


void MergeMapping::removeMapping(const std::string& single) {
	std::unordered_map<std::string, std::string>::iterator it = singleToMultiBitName.find(single);
	if (it == singleToMultiBitName.end()) return;
	const std::string mb = it->second; singleToMultiBitName.erase(it);
	std::vector<std::string>& vec = multiBitToSingles[mb];
	vec.erase(std::remove(vec.begin(), vec.end(), single), vec.end());
	if (vec.empty()) multiBitToSingles.erase(mb);
}


void MergeMapping::clear() {
	singleToMultiBitName.clear();
	multiBitToSingles.clear();
}
void MergeMapping::printMappings() const {
	for (std::unordered_map<std::string, std::vector<std::string> >::const_iterator it = multiBitToSingles.begin(); it != multiBitToSingles.end(); ++it) {
		std::cerr << "[MergeMap] " << it->first << " <= ";
		const std::vector<std::string>& v = it->second;
		for (size_t i = 0; i < v.size(); ++i) std::cerr << (i ? "," : "") << v[i];
		std::cerr << "\n";
	}
}


std::vector<std::pair<int, std::string> > MergeMapping::getBitIndexedPairs(const std::string& mbffName) const {
	std::vector<std::pair<int, std::string> > out;
	std::unordered_map<std::string, std::vector<std::string> >::const_iterator it = multiBitToSingles.find(mbffName);
	if (it == multiBitToSingles.end()) return out;
	const std::vector<std::string>& vec = it->second;
	for (size_t i = 0; i < vec.size(); ++i) out.push_back(std::make_pair((int)i, vec[i]));
	return out;
}


// ---- Firefly ----
FireflyEngine::FireflyEngine(const Weights& w, double tnsSpatial)
	: w_(w), spatialTnsW_(tnsSpatial) {
}


void FireflyEngine::setCompatTables(
	const std::unordered_map<std::string, std::vector<std::string> >* banking,
	const std::unordered_map<std::string, std::vector<std::string> >* debanking
) {
	banking_ = banking;
	debanking_ = debanking;
}


void FireflyEngine::setSeeds(const std::vector<SeedCandidate>& seeds) {
	seeds_ = seeds;
}

// helpers to find FF info from DEF
// 取代 findFFXY / findFFLib / libWidth / libHeight
inline std::pair<int, int> XY(const std::unordered_map<std::string, std::pair<int, int>>& mp,
	const std::string& n) {
	auto it = mp.find(n);
	return (it == mp.end()) ? std::make_pair(0, 0) : it->second;
}
inline std::string LibOf(const std::unordered_map<std::string, std::string>& mp,
	const std::string& n) {
	auto it = mp.find(n);
	return (it == mp.end()) ? std::string() : it->second;
}
inline std::pair<int, int> LibSize(const std::unordered_map<std::string, std::pair<int, int>>& mp,
	const std::string& lib) {
	auto it = mp.find(lib);
	return (it == mp.end()) ? std::make_pair(0, 0) : it->second;
}


std::pair<int, int> FireflyEngine::centroid(const std::vector<std::pair<int, int> >& pts) {
	long long sx = 0, sy = 0;
	for (size_t i = 0; i < pts.size(); ++i) { sx += pts[i].first; sy += pts[i].second; }
	int n = (int)pts.size();
	return n ? std::make_pair((int)(sx / n), (int)(sy / n)) : std::make_pair(0, 0);
}


double FireflyEngine::bboxRadius(const std::vector<std::pair<int, int> >& pts) {
	if (pts.empty()) return 0.0;
	int minx = pts[0].first, maxx = pts[0].first, miny = pts[0].second, maxy = pts[0].second;
	for (size_t i = 1; i < pts.size(); ++i) {
		minx = std::min(minx, pts[i].first);
		maxx = std::max(maxx, pts[i].first);
		miny = std::min(miny, pts[i].second);
		maxy = std::max(maxy, pts[i].second);
	}
	return (double)std::max(maxx - minx, maxy - miny);
}


// Scoring: w.Alpha * (spatial spread proxy for TNS) + w.Beta * power + w.Gamma * area.
// We do not have per-cell power in this pipeline; treat power~area as a proxy and prefer HOPT libs for TNS.
// This is intentionally simple but deterministic.
double FireflyEngine::evalCost(const FireflySolution& s, const DefData& def, const LefData& lef) const {
	double tnsProxy = 0.0, area = 0.0, powerProxy = 0.0;

	for (const auto& kv : s.merges) {
		const MergedFF& m = kv.second;

		// bbox on cached XY
		int minx = INT_MAX, maxx = INT_MIN, miny = INT_MAX, maxy = INT_MIN;
		for (const auto& inst : m.mergedFFs) {
			auto p = XY(xyOfInst_, inst);
			minx = std::min(minx, p.first);  maxx = std::max(maxx, p.first);
			miny = std::min(miny, p.second); maxy = std::max(maxy, p.second);
		}
		int rad = std::max(maxx - minx, maxy - miny);
		tnsProxy += (double)rad * spatialTnsW_;

		auto sz = LibSize(libSize_, m.mbffType);
		area += (double)std::max(1, sz.first) * std::max(1, sz.second);

		double p = 1.0;
		if (m.mbffType.find("HOPT") != std::string::npos) p = 1.2;
		else if (m.mbffType.find("SLOPT") != std::string::npos) p = 0.8;
		powerProxy += p;
	}
	for (const auto& op : s.sizeOps) {
		const std::string& newLib = op.second;
		if (newLib.find("HOPT") != std::string::npos) tnsProxy *= 0.95;
		powerProxy += (newLib.find("HOPT") != std::string::npos) ? 0.2 : 0.05;
	}
	return w_.Alpha * tnsProxy + w_.Beta * powerProxy + w_.Gamma * area;
}



FireflySolution FireflyEngine::randomInit(std::mt19937& gen, const DefData& def) const {
	(void)def;
	FireflySolution s;

	std::vector<size_t> idx(seeds_.size());
	for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
	std::shuffle(idx.begin(), idx.end(), gen);

	int used = 0, id = 0;
	for (size_t t = 0; t < idx.size() && used < maxMergesPerSol_; ++t) {
		const SeedCandidate& c = seeds_[idx[t]];

		std::ostringstream base; base << "MBFF_FF_" << (++id);

		MergedFF m;
		// ★ 關鍵：完全比照 DPC，把階層在這裡就接進 newInstanceName
		if (c.hierPrefix.empty()) m.newInstanceName = base.str();
		else                      m.newInstanceName = c.hierPrefix + "/" + base.str();

		m.hierPrefix = c.hierPrefix;   // 可留作記錄，不再在輸出時使用
		m.mbffType = c.targetLib;
		m.mergedFFs = c.singles;
		m.bitwidth = (int)c.singles.size();
		m.orientation = "N";

		long long sx = 0, sy = 0;
		for (const auto& inst : c.singles) {
			auto p = XY(xyOfInst_, inst); sx += p.first; sy += p.second;
		}
		const int n = (int)c.singles.size();
		m.newX = n ? (int)(sx / n) : 0;
		m.newY = n ? (int)(sy / n) : 0;

		// 簡化 pin 映射
		for (size_t b = 0; b < c.singles.size(); ++b) {
			std::string d = "D" + std::to_string(b), q = "Q" + std::to_string(b);
			m.mbffPinToOrigFF[d] = c.singles[b];
			m.mbffPinToOrigPin[d] = c.singles[b] + "/D";
			m.mbffPinToOrigFF[q] = c.singles[b];
			m.mbffPinToOrigPin[q] = c.singles[b] + "/Q";
		}

		// ★ 注意：map 的 key 也直接用「已含階層」的新名字（與 DPC 一致）
		s.merges[m.newInstanceName] = m;
		++used;
	}
	return s;
}


FireflySolution FireflyEngine::attract(const FireflySolution& a, const FireflySolution& b, std::mt19937& gen) const {
	FireflySolution out = a;
	std::uniform_real_distribution<double> coin(0.0, 1.0);

	// 拷入部分 b 的 merges，直到達到上限
	for (const auto& kv : b.merges) {
		if ((int)out.merges.size() >= maxMergesPerSol_) break;
		if (coin(gen) < 0.6) out.merges[kv.first] = kv.second;
	}
	// size ops（預留）
	for (const auto& op : b.sizeOps) {
		if (coin(gen) < 0.5) out.sizeOps[op.first] = op.second;
	}
	// 隨機丟一個，避免膨脹/同質化
	if (!out.merges.empty() && coin(gen) < 0.3) {
		auto it = out.merges.begin();
		std::advance(it, gen() % out.merges.size());
		out.merges.erase(it);
	}
	return out;
}


void FireflyEngine::buildCaches(const DefData& def, const LefData& lef) {
	xyOfInst_.clear(); libOfInst_.clear(); libSize_.clear();

	for (const auto& ff : def.flipFlops) {
		xyOfInst_[ff.instName] = { ff.x, ff.y };
		libOfInst_[ff.instName] = ff.cellType;
	}
	for (const auto& c : def.components) {
		// 不覆蓋 FF 的記錄
		if (!xyOfInst_.count(c.name)) {
			xyOfInst_[c.name] = { c.x, c.y };
			libOfInst_[c.name] = c.cellType;
		}
	}
	for (const auto& m : lef.macros) {
		int w = (int)(m.sizeX * def.units);
		int h = (int)(m.sizeY * def.units);
		libSize_[m.name] = { w, h };
	}
}


FireflySolution
FireflyEngine::run(const DefData& def, const LefData& lef, int maxIter, int popSize, unsigned rngSeed) {
	std::mt19937 gen(rngSeed);
	if (popSize < 8)  popSize = 12;
	if (maxIter < 40) maxIter = 40;

	buildCaches(def, lef);

	std::cerr << "[Firefly] seeds=" << seeds_.size()
		<< " pop=" << popSize
		<< " iters=" << maxIter
		<< " maxMergesPerSol=" << maxMergesPerSol_ << "\n";

	std::vector<FireflySolution> P; P.reserve(popSize);
	for (int i = 0; i < popSize; ++i) {
		auto s = randomInit(gen, def);
		s.score = evalCost(s, def, lef);
		P.push_back(std::move(s));
	}

	for (int it = 0; it < maxIter; ++it) {
		std::sort(P.begin(), P.end(),
			[](const auto& a, const auto& b) { return a.score < b.score; });
		if (it % 5 == 0) {
			std::cerr << "[Firefly] iter " << it
				<< " best=" << P.front().score
				<< " merges=" << P.front().merges.size() << "\n";
		}
		for (int i = 1; i < popSize; ++i) {
			for (int j = 0; j < i; ++j) {
				if (P[j].score + 1e-9 < P[i].score) {
					auto moved = attract(P[i], P[j], gen);
					moved.score = evalCost(moved, def, lef);
					if (moved.score < P[i].score) P[i] = std::move(moved);
				}
			}
		}
	}

	std::sort(P.begin(), P.end(),
		[](const auto& a, const auto& b) { return a.score < b.score; });
	const FireflySolution& best = P.front();

	mergedFFResults_.clear();
	mergeMap_.clear();

	// ★ 直接把 best 的 merges 倒到結果；mapping 也用 newInstanceName（已含階層）
	for (const auto& kv : best.merges) {
		const MergedFF& m = kv.second;
		mergedFFResults_.push_back(m);
		for (const auto& s : m.mergedFFs) addMergeMap(s, m.newInstanceName);
	}

	std::cerr << "[Firefly] Best score=" << best.score
		<< " merges=" << best.merges.size() << "\n";
	return best;
}




void FireflyEngine::clearMergeMap() { mergeMap_.clear(); }
void FireflyEngine::addMergeMap(const std::string& singleFF, const std::string& mbffName) { mergeMap_.addMapping(singleFF, mbffName); }


std::vector<PlacedComponent>
FireflyEngine::generatePlacementComponents(const DefData& defData, const LefData& lefData) const {
	std::unordered_map<std::string, std::pair<int, int>> cellSizes;
	for (const auto& m : lefData.macros) {
		int w = (int)(m.sizeX * defData.units);
		int h = (int)(m.sizeY * defData.units);
		cellSizes[m.name] = std::make_pair(w, h);
	}

	// 建一份 mergeMap（single -> multi，全都用已含階層的新名字）
	MergeMapping mm;
	for (const auto& m : mergedFFResults_) {
		for (const auto& s : m.mergedFFs) mm.addMapping(s, m.newInstanceName);
	}

	std::vector<PlacedComponent> result;
	result.reserve(defData.components.size() + mergedFFResults_.size());

	// 1) 新 MBFF：名字即為階層/MBFF_FF_x
	for (const auto& m : mergedFFResults_) {
		auto it = cellSizes.find(m.mbffType);
		const int w = (it != cellSizes.end()) ? it->second.first : 0;
		const int h = (it != cellSizes.end()) ? it->second.second : 0;
		result.push_back(PlacedComponent(
			m.newInstanceName, m.mbffType, m.newX, m.newY, m.orientation, w, h, true, true));
	}

	// 2) 未合併的單顆 FF
	std::set<std::string> allFFs;
	for (const auto& ff : defData.flipFlops) allFFs.insert(ff.instName);
	for (const auto& ff : defData.flipFlops) {
		if (mm.isMerged(ff.instName)) continue;
		auto it = cellSizes.find(ff.cellType);
		const int w = (it != cellSizes.end()) ? it->second.first : 0;
		const int h = (it != cellSizes.end()) ? it->second.second : 0;
		result.push_back(PlacedComponent(
			ff.instName, ff.cellType, ff.x, ff.y, ff.orient, w, h, false, true));
	}

	// 3) 其他非 FF
	for (const auto& c : defData.components) {
		if (allFFs.count(c.name)) continue;
		auto it = cellSizes.find(c.cellType);
		const int w = (it != cellSizes.end()) ? it->second.first : 0;
		const int h = (it != cellSizes.end()) ? it->second.second : 0;
		result.push_back(PlacedComponent(
			c.name, c.cellType, c.x, c.y, c.orient, w, h, false, false));
	}
	return result;
}



std::vector<NewFlipFlopInfo>
FireflyEngine::generatePlacementFFsNew(const DefData& defData, const LefData& lefData) const {
	std::vector<NewFlipFlopInfo> result;

	std::unordered_map<std::string, std::pair<int, int>> cellSizes;
	for (const auto& m : lefData.macros) {
		int w = (int)(m.sizeX * defData.units);
		int h = (int)(m.sizeY * defData.units);
		cellSizes[m.name] = std::make_pair(w, h);
	}

	for (const auto& m : mergedFFResults_) {
		NewFlipFlopInfo ff;
		ff.instName = m.newInstanceName;  // ★ 直接用已含階層的新名字
		ff.cellType = m.mbffType;
		ff.x = m.newX;
		ff.y = m.newY;
		ff.orient = m.orientation;
		ff.orientation = m.orientation;
		ff.isMultiBit = true;
		ff.bitWidth = (int)m.mergedFFs.size();
		auto it = cellSizes.find(m.mbffType);
		if (it != cellSizes.end()) { ff.width = it->second.first; ff.height = it->second.second; }
		result.push_back(ff);
	}
	return result;
}
