#pragma once
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include "DataStructures.h"


// Lightweight view used by preprocessing
struct FFView {
	std::string instName;
	std::string cellType;
	std::string clockNet;
	int x = 0, y = 0;
	std::string orient;
};


struct FFGroupKey {
	std::string clock;
	std::string hierBucket; // e.g., ".../hier_top_mod_3"
	bool operator<(const FFGroupKey& o) const {
		if (clock != o.clock) return clock < o.clock;
		return hierBucket < o.hierBucket;
	}
};


struct FFGroup {
	FFGroupKey key;
	std::vector<FFView> members;
};


// A simple seed candidate (will be refined by Firefly)
struct SeedCandidate {
	std::vector<std::string> singles; // instance names (1,2, or 4 items)
	std::string targetLib; // candidate MBFF lib name
	std::string hierPrefix;
};


class FireflyPreprocess {
public:
	// clockDomains: clock -> list of FFs (from HierarchicalClustering::getClockDomains())
	FireflyPreprocess(
		const std::map<std::string, std::vector<FlipFlopInfo>>& clockDomains,
		const DefData& def,
		const LefData& lef,
		const std::unordered_map<std::string, std::vector<std::string>>& bankingCompat
	);


	// 1) Group FFs by {clock, hier_bucket}
	std::vector<FFGroup> buildGroupsByClockAndHierarchy() const;


	// 2) Produce greedy seeds (pairs/quads) within group; respect compatibility
	std::vector<SeedCandidate> buildSeedCandidates(const std::vector<FFGroup>& groups, bool enable4Bit = true) const;


	// helper: compute the hierarchy bucket of an instance path
	static std::string extractHierBucket(const std::string& instFullPath);

	static std::string commonModulePrefix(const std::vector<std::string>& instFullPaths);
private:
	const std::map<std::string, std::vector<FlipFlopInfo>>& clockDomains_;
	const DefData& def_;
	const LefData& lef_;
	const std::unordered_map<std::string, std::vector<std::string>>& bankingCompat_;


	static double manhattan(int x1, int y1, int x2, int y2) { return std::abs(x1 - x2) + std::abs(y1 - y2); }
	static bool isSameBucketOrUnspecified(const std::string& a, const std::string& b) {
		if (a.empty() || b.empty()) return true;
		return a == b;
	}
};