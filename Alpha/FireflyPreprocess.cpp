#include "FireflyPreprocess.h"
#include <algorithm>
#include <sstream>
#include <iostream>
FireflyPreprocess::FireflyPreprocess(
	const std::map<std::string, std::vector<FlipFlopInfo>>& clockDomains,
	const DefData& def,
	const LefData& lef,
	const std::unordered_map<std::string, std::vector<std::string>>& bankingCompat
) : clockDomains_(clockDomains), def_(def), lef_(lef), bankingCompat_(bankingCompat) {
}
// 放在檔案內任意位置（static helper）
static std::string ensureHierPrefix(const std::vector<std::string>& fullInstPaths) {
	std::string pfx = FireflyPreprocess::commonModulePrefix(fullInstPaths);
	if (!pfx.empty()) return pfx;
	// fallback：用第一顆的 bucket（最深 hier_top_mod_*）
	if (!fullInstPaths.empty()) return FireflyPreprocess::extractHierBucket(fullInstPaths.front());
	return "";
}


std::string FireflyPreprocess::extractHierBucket(const std::string& path) {
	// Rule: if tokens like hier_top_mod_5/4/3/2/1 exist, all FFs to be banked must share the same deepest one.
	// Implementation: take the deepest token that starts with "hier_top_mod_"; bucket is prefix up to that token.
	if (path.empty()) return "";
	std::vector<std::string> toks; toks.reserve(32);
	std::stringstream ss(path);
	std::string seg; while (std::getline(ss, seg, '/')) toks.push_back(seg);


	int deepestIdx = -1; // index in toks
	for (int i = 0; i < (int)toks.size(); ++i) {
		const std::string& t = toks[i];
		if (t.rfind("hier_top_mod_", 0) == 0) deepestIdx = i;
	}
	if (deepestIdx < 0) return ""; // unspecified -> no restriction


	// Join up to deepestIdx
	std::ostringstream out;
	for (int i = 0; i <= deepestIdx; ++i) {
		if (i) out << '/';
		out << toks[i];
	}
	return out.str();
}
// FireflyPreprocess.cpp

std::string FireflyPreprocess::commonModulePrefix(const std::vector<std::string>& names) {
	if (names.empty()) return "";
	// split every full path by '/'
	std::vector<std::vector<std::string>> toks(names.size());
	for (size_t i = 0; i < names.size(); ++i) {
		std::stringstream ss(names[i]);
		std::string seg;
		while (std::getline(ss, seg, '/')) toks[i].push_back(seg);
		if (!toks[i].empty()) toks[i].pop_back(); // drop leaf instance token
	}
	// longest common prefix
	size_t k = 0;
	for (;; ++k) {
		if (toks[0].size() <= k) break;
		const std::string& cur = toks[0][k];
		bool same = true;
		for (size_t i = 1; i < toks.size(); ++i) {
			if (toks[i].size() <= k || toks[i][k] != cur) { same = false; break; }
		}
		if (!same) break;
	}
	if (k == 0) return "";
	std::ostringstream out;
	for (size_t i = 0; i < k; ++i) { if (i) out << '/'; out << toks[0][i]; }
	return out.str();
}


std::vector<FFGroup> FireflyPreprocess::buildGroupsByClockAndHierarchy() const {
	std::map<FFGroupKey, std::vector<FFView>> tmp;


	for (const auto& kv : clockDomains_) {
		const std::string& clk = kv.first;
		for (const FlipFlopInfo& ff : kv.second) {
			FFView v; v.instName = ff.instName; v.cellType = ff.cellType; v.clockNet = clk; v.x = ff.x; v.y = ff.y; v.orient = ff.orient;
			const std::string bucket = extractHierBucket(ff.instName);
			FFGroupKey key{ clk, bucket };
			tmp[key].push_back(v);
		}
	}

	std::vector<FFGroup> groups; groups.reserve(tmp.size());
	for (const auto& kv : tmp) {
		FFGroup g; g.key = kv.first; g.members = kv.second; groups.push_back(g);
	}
	return groups;
}
static bool hasCompatMBFF(const std::unordered_map<std::string, std::vector<std::string>>& compat,
	const std::vector<std::string>& singles,
	std::string& outMBFFLib) {
	// Very simple policy: pick the first MBFF lib that appears in all singles' compat lists.
	if (singles.empty()) return false;
	std::set<std::string> inter;
	bool first = true;
	for (const std::string& s : singles) {
		// We only have the cell type, but compat table is keyed by library name. Caller should pass cellType strings.
		std::map<std::string, std::string> dummy; // unused
		const std::vector<std::string>& lst = compat.count(s) ? compat.at(s) : std::vector<std::string>();
		if (first) { inter = std::set<std::string>(lst.begin(), lst.end()); first = false; }
		else {
			std::set<std::string> next;
			for (const std::string& x : lst) if (inter.count(x)) next.insert(x);
			inter.swap(next);
		}
		if (inter.empty()) return false;
	}
	if (inter.empty()) return false;
	outMBFFLib = *inter.begin();
	return true;
}
std::vector<SeedCandidate>
FireflyPreprocess::buildSeedCandidates(const std::vector<FFGroup>& groups, bool enable4Bit) const {
	std::vector<SeedCandidate> seeds;

	for (const FFGroup& g : groups) {
		if (g.members.size() < 2) continue;

		std::vector<FFView> ffs = g.members;
		std::sort(ffs.begin(), ffs.end(), [](const FFView& a, const FFView& b) {
			if (a.y != b.y) return a.y < b.y;
			return a.x < b.x;
			});

		// 2-bit greedy
		std::vector<int> used(ffs.size(), 0);
		for (size_t i = 0; i < ffs.size(); ++i) {
			if (used[i]) continue;
			int bestJ = -1; double bestD = 1e100;
			for (size_t j = i + 1; j < ffs.size(); ++j) {
				if (used[j]) continue;
				double d = manhattan(ffs[i].x, ffs[i].y, ffs[j].x, ffs[j].y);
				if (d < bestD) { bestD = d; bestJ = (int)j; }
			}
			if (bestJ >= 0) {
				std::vector<std::string> types = { ffs[i].cellType, ffs[bestJ].cellType };
				std::string mbff;
				if (hasCompatMBFF(bankingCompat_, types, mbff)) {
					SeedCandidate c;
					c.singles = { ffs[i].instName, ffs[bestJ].instName };
					c.targetLib = mbff;
					c.hierPrefix = ensureHierPrefix(c.singles); // ★ 這行
					seeds.push_back(c);
					used[i] = used[bestJ] = 1;
				}
			}
		}

		// 4-bit（可選）
		if (enable4Bit && ffs.size() >= 4) {
			for (size_t k = 0; k + 3 < ffs.size(); k += 4) {
				std::vector<int> idx = { (int)k, (int)k + 1, (int)k + 2, (int)k + 3 };
				std::vector<std::string> types = {
					ffs[idx[0]].cellType, ffs[idx[1]].cellType, ffs[idx[2]].cellType, ffs[idx[3]].cellType
				};
				std::string mbff;
				if (hasCompatMBFF(bankingCompat_, types, mbff)) {
					SeedCandidate c;
					c.singles = { ffs[idx[0]].instName, ffs[idx[1]].instName, ffs[idx[2]].instName, ffs[idx[3]].instName };
					c.targetLib = mbff;
					c.hierPrefix = ensureHierPrefix(c.singles); // ★ 這行
					seeds.push_back(c);
				}
			}
		}
	}
	return seeds;
}
