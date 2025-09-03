#ifndef PARSER_H
#define PARSER_H

#include "DataStructures.h"
#include "ParserLEF.h"
#include "ParserWeights.h"
#include "HierarchicalClustering.h"
#include"LibParser.h"
#include <string>
#include <vector>
#include <memory>
#include <set>
#include <unordered_map>
// Forward declarations
class DefParser;
class VerilogParser;
class SdcParser;
class TechParser;
class LibParser;  // 新增 LibParser

class Parser {
private:
    // Parser instances
    std::unique_ptr<LefParser> lefParser_;
    std::unique_ptr<WeightParser> weightParser_;
    std::unique_ptr<DefParser> defParser_;
    std::unique_ptr<VerilogParser> verilogParser_;
    std::unique_ptr<SdcParser> sdcParser_;
    std::unique_ptr<TechParser> techParser_;
    std::unique_ptr<LibParser> libParser_;  // 新增
    std::string inputVerilogFile_;  // 新增
    // File paths
    std::string baseName_;
    std::string outputName_;

    // Status tracking
    bool lefLoaded_;
    bool weightLoaded_;
    bool defLoaded_;
    bool verilogLoaded_;
    bool sdcLoaded_;
    bool techLoaded_;
    bool libLoaded_;  // 新增

    // ??專用資料
    std::vector<std::string> initialCellList_;  // 從 weight 檔案取得
    std::set<std::string> finalCellList_;       // ?過 lib ?理後的最終列表
    std::unique_ptr<HierarchicalClustering> hierarchicalClustering_;
    bool clusteringPerformed_;

    // Helper methods
    std::string constructFilePath(const std::string& base, const std::string& extension) const;
    void initializeParsers();

    std::map<std::string, std::vector<FlipFlopInfo>> ffGroupsByType_;
    std::set<std::string> ffCellTypes_;  // All known FF cell types from .lib
    std::vector<MBFFInstance> bankingList_;
public:
    // Constructor & Destructor
    explicit Parser(const std::string& baseName = "testcase1",
        const std::string& outputName = "output");
    ~Parser();
    static const std::vector<LefSiteInfo> emptyLefSites_;

    // Disable copy
    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;

    // Enable move
    Parser(Parser&&) = default;
    Parser& operator=(Parser&&) = default;

    // ??專用介面
    bool parseLibWithCellList(const std::vector<std::string>& libFiles,
        const std::vector<std::string>& initialCellList);
    bool parseLEFWithCellList(const std::vector<std::string>& lefFiles,
        const std::set<std::string>& finalCellList);

    // Cell list management
    void setInitialCellList(const std::vector<std::string>& cellList) {
        initialCellList_ = cellList;
    }
    void setFinalCellList(const std::set<std::string>& cellList) {
        finalCellList_ = cellList;
    }

    const std::vector<std::string>& getInitialCellList() const {
        return initialCellList_;
    }
    const std::set<std::string>& getFinalCellList() const {
        return finalCellList_;
    }
    void setBankingList(const std::vector<MBFFInstance>& bankingList) {
        bankingList_ = bankingList;
    }
    // Main parsing interface
    bool parseAllFiles();
    bool parseFile(const std::string& filename, const std::string& fileType);

    // Individual file parsing
    bool parseLEF(const std::string& filename = "");
    bool parseWeights(const std::string& filename = "");
    bool parseDEF(const std::string& filename = "");
    bool parseVerilog(const std::string& filename = "");
    bool parseSDC(const std::string& filename = "");
    bool parseTech(const std::string& filename = "");
    bool parseLib(const std::string& filename = "");  // 新增

    // Data access methods (const versions)
    const LefParser* getLefParser() const { return lefParser_.get(); }
    const WeightParser* getWeightParser() const { return weightParser_.get(); }
    const DefParser* getDefParser() const { return defParser_.get(); }
    const VerilogParser* getVerilogParser() const { return verilogParser_.get(); }
    const SdcParser* getSdcParser() const { return sdcParser_.get(); }
    const TechParser* getTechParser() const { return techParser_.get(); }
    const LibParser* getLibParser() const { return libParser_.get(); }  // 新增
    const std::unordered_map<std::string, LefMacroInfo>& getMacroMap() const { return macroMap_; }

    // Data access methods (non-const versions)
    LefParser* getLefParser() { return lefParser_.get(); }
    WeightParser* getWeightParser() { return weightParser_.get(); }
    DefParser* getDefParser() { return defParser_.get(); }
    VerilogParser* getVerilogParser() { return verilogParser_.get(); }
    SdcParser* getSdcParser() { return sdcParser_.get(); }
    TechParser* getTechParser() { return techParser_.get(); }
    LibParser* getLibParser() { return libParser_.get(); }  // 新增

    // Status methods
    bool isLefLoaded() const { return lefLoaded_; }
    bool isWeightLoaded() const { return weightLoaded_; }
    bool isDefLoaded() const { return defLoaded_; }
    bool isVerilogLoaded() const { return verilogLoaded_; }
    bool isSdcLoaded() const { return sdcLoaded_; }
    bool isTechLoaded() const { return techLoaded_; }
    bool isLibLoaded() const { return libLoaded_; }  // 新增
    bool allFilesLoaded() const;

    // Configuration methods
    void setBaseName(const std::string& baseName);
    void setOutputName(const std::string& outputName);
    const std::string& getBaseName() const { return baseName_; }
    const std::string& getOutputName() const { return outputName_; }

    // Analysis methods
    void analyzeFlipFlops();
    void calculateStatistics();
    void performCellTypeAnalysis();
    void performBankingOptimization();  // 新增：多位元正反器?化

    // Output methods
    void printSummary() const;
    void printAllData() const;
    void printParsingStatus() const;
    bool writeOutputFiles() const;

    // Utility methods
    void clear();
    void reset();

    // Error handling
    std::vector<std::string> getErrors() const;
    std::vector<std::string> getWarnings() const;
    void performHierarchicalClustering();
    const HierarchicalClustering* getHierarchicalClustering() const {
        return hierarchicalClustering_.get();
    }
    bool isClusteringPerformed() const { return clusteringPerformed_; }
    void printClusteringResults() const;
    void exportClusteringResults(const std::string& filename) const;
    bool parseAllLibraries(const std::vector<std::string>& libFiles);
    void identifyFFInstances();
    void groupFFInstancesByType();
    const std::map<std::string, std::vector<FlipFlopInfo>>& getFFGroupsByType() const {
        return ffGroupsByType_;
    }
    bool isFFCellType(const std::string& cellType) const {
        return ffCellTypes_.find(cellType) != ffCellTypes_.end();
    }

    // Get macro map
    Weights getWeights() const;

    DefData& getDefData();  // Non-const version for updates
    const DefData& getDefData() const;  // Const version

    // Get LEF sites
    const std::vector<LefSiteInfo>& getLefSites() const;
    const std::string& inputDefPath() const { return inputDefPath_; }
    const std::string& inputVerilogPath() const { return inputVerilogPath_; }
    DefParser* def() const { return defParser_.get(); }
    // Legalization method
    bool performLegalization();
    void setInputVerilogFile(const std::string& filename) { inputVerilogFile_ = filename; }
private:
    // Error tracking
    mutable std::vector<std::string> errors_;
    mutable std::vector<std::string> warnings_;
    // macro MAP
    std::unordered_map<std::string, LefMacroInfo> macroMap_;
    void buildMacroMap();
    std::string inputDefPath_;
    std::string inputVerilogPath_;

    void addError(const std::string& error) const;
    void addWarning(const std::string& warning) const;


};

// Utility functions
namespace ParserUtils {
    std::string extractBaseName(const std::string& filename);
    std::vector<std::string> getExpectedFiles(const std::string& baseName);
    bool fileExists(const std::string& filename);
    std::string getFileExtension(const std::string& filename);
}

#endif // PARSER_H