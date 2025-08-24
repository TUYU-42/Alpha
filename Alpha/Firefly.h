#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <random>
#include "DataStructures.h"
#include"FireflyPreprocess.h"
#include<iostream>
#include<sstream>


struct FireflySolution {
	// Mapping of new MBFF instance -> MergedFF record
	std::map<std::string, MergedFF> merges;
	// Optional: size_cell ops (instance -> new lib)
	std::map<std::string, std::string> sizeOps;
	double score = 1e100; // lower is better
};


class FireflyEngine {
public:
	FireflyEngine(const Weights& w, double tnsWeightSpatial = 1.0);


	void setCompatTables(
		const std::unordered_map<std::string, std::vector<std::string>>* banking,
		const std::unordered_map<std::string, std::vector<std::string>>* debanking
	);


	void setSeeds(const std::vector<SeedCandidate>& seeds);


	// Run firefly metaheuristic (TNS-guided). Inputs supply geometry + sizes.
	FireflySolution run(const DefData& def, const LefData& lef, int maxIter = 100, int popSize = 24, unsigned rngSeed = 7);


	// Exporters matching existing pipeline (so Alpha.cpp can mirror DPC calls)
	std::vector<PlacedComponent> generatePlacementComponents(const DefData& defData, const LefData& lefData) const;
	std::vector<NewFlipFlopInfo> generatePlacementFFsNew(const DefData& defData, const LefData& lefData) const;


	const std::vector<MergedFF>& mergedFFResults() const { return mergedFFResults_; }


	// merge map utilities (implemented here to avoid linking DPC.cpp)
	void clearMergeMap();
	void addMergeMap(const std::string& singleFF, const std::string& mbffName);


private:
	Weights w_;
	double spatialTnsW_;
	const std::unordered_map<std::string, std::vector<std::string>>* banking_ = nullptr;
	const std::unordered_map<std::string, std::vector<std::string>>* debanking_ = nullptr;
	std::vector<SeedCandidate> seeds_;


	// Results
	std::vector<MergedFF> mergedFFResults_;
	MergeMapping mergeMap_;


	// internals
	double evalCost(const FireflySolution& s, const DefData& def, const LefData& lef) const;
	static double bboxRadius(const std::vector<std::pair<int, int>>& pts);
	static std::pair<int, int> centroid(const std::vector<std::pair<int, int>>& pts);


	// firefly operators
	FireflySolution randomInit(std::mt19937& gen, const DefData& def) const;
	FireflySolution attract(const FireflySolution& a, const FireflySolution& b, std::mt19937& gen) const;

	std::unordered_map<std::string, std::pair<int, int>> xyOfInst_;   // inst -> (x,y)
	std::unordered_map<std::string, std::string>        libOfInst_;  // inst -> cellType
	std::unordered_map<std::string, std::pair<int, int>> libSize_;    // lib -> (w,h)

	// cap merges per solution to avoid huge R
	int maxMergesPerSol_ = 1200;   // 可調：先 800~1500 再視情況

	void buildCaches(const DefData& def, const LefData& lef);
};
