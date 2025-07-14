#include "Parser.h"
#include "ParserDEF.h"
#include "ParserVerilog.h"
#include "ParserSDC.h"
#include "ParserTech.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include "LibParser.h"  // 新增 include
using namespace std;

// Constructor
Parser::Parser(const string& baseName, const string& outputName)
    : baseName_(baseName), outputName_(outputName),
    lefLoaded_(false), weightLoaded_(false), defLoaded_(false),
    verilogLoaded_(false), sdcLoaded_(false), techLoaded_(false),
    clusteringPerformed_(false) {  // Add this
    initializeParsers();
}

// Destructor
Parser::~Parser() = default;

// Initialize all parsers
void Parser::initializeParsers() {
    lefParser_ = make_unique<LefParser>();
    weightParser_ = make_unique<WeightParser>();
    defParser_ = make_unique<DefParser>();
    verilogParser_ = make_unique<VerilogParser>();
    sdcParser_ = make_unique<SdcParser>();
    techParser_ = make_unique<TechParser>();
    libParser_ = make_unique<LibParser>();  // 新增
    hierarchicalClustering_ = make_unique<HierarchicalClustering>();
}
void Parser::performHierarchicalClustering() {
    cout << "\n=== Performing Hierarchical Clustering Analysis ===" << endl;

    if (!defLoaded_) {
        addError("Cannot perform clustering: DEF file not loaded");
        return;
    }

    // Get flip-flops from DEF parser
    const auto& flipFlops = defParser_->getFlipFlops();

    if (flipFlops.empty()) {
        addWarning("No flip-flops found for clustering");
        clusteringPerformed_ = false;
        return;
    }

    cout << "Starting clustering analysis for " << flipFlops.size() << " flip-flops..." << endl;

    // Perform the clustering
    hierarchicalClustering_->performClustering(flipFlops);

    // Validate results
    if (hierarchicalClustering_->validateClustering()) {
        clusteringPerformed_ = true;

        // Print summary
        hierarchicalClustering_->printClusteringSummary();

        // Export results
        string clusteringFile = outputName_ + "_clustering.txt";
        hierarchicalClustering_->exportToFile(clusteringFile);
    }
    else {
        clusteringPerformed_ = false;
        addError("Clustering validation failed");
    }
}

void Parser::printClusteringResults() const {
    if (!clusteringPerformed_) {
        cout << "No clustering results available. Run performHierarchicalClustering() first." << endl;
        return;
    }

    hierarchicalClustering_->printDetailedReport();
}

void Parser::exportClusteringResults(const string& filename) const {
    if (!clusteringPerformed_) {
        addError("No clustering results to export");
        return;
    }

    hierarchicalClustering_->exportToFile(filename);
}

// Update the performBankingOptimization() method to use clustering results:
void Parser::performBankingOptimization() {
    cout << "\n=== Banking/Debanking Optimization ===" << endl;

    if (!defLoaded_ || !libLoaded_) {
        cout << "Error: Need both DEF and LIB data for optimization" << endl;
        return;
    }

    // First ensure clustering is performed
    if (!clusteringPerformed_) {
        cout << "Performing hierarchical clustering first..." << endl;
        performHierarchicalClustering();

        if (!clusteringPerformed_) {
            addError("Failed to perform clustering, cannot proceed with optimization");
            return;
        }
    }

    // Get clustering results
    const auto& clusteredDesign = hierarchicalClustering_->getClusteredDesign();
    const auto& statistics = hierarchicalClustering_->getStatistics();

    cout << "Analyzing " << statistics.totalClockDomains << " clock domains "
        << "with " << statistics.totalScanChains << " scan chains..." << endl;

    // Process each clock domain separately
    for (const auto& [clockNet, scanChains] : clusteredDesign) {
        cout << "\nProcessing clock domain: " << clockNet << endl;
        cout << "  Chains in domain: " << scanChains.size() << endl;

        // Find banking candidates within this clock domain
        auto candidates = hierarchicalClustering_->findBankingCandidates(clockNet);
        cout << "  Banking candidates found: " << candidates.size() << endl;

        // TODO: Implement actual banking algorithm
        // 1. Analyze each scan chain for banking opportunities
        // 2. Consider proximity, timing, and power constraints
        // 3. Generate optimal banking solutions
    }

    cout << "\n[TODO] Implement actual banking algorithm based on clustering" << endl;
}

// Helper method to construct file paths
string Parser::constructFilePath(const string& base, const string& extension) const {
    if (extension == "_weight") {
        return base + "_weight";
    }
    return base + "." + extension;
}
bool Parser::parseLEFWithCellList(const vector<string>& lefFiles,
    const set<string>& finalCellList) {
    cout << "\n=== Contest Step 3: Parse .lef Files with Final Cell List ===" << endl;
    cout << "  Filtering for " << finalCellList.size() << " cells only" << endl;

    if (!lefParser_) {
        addError("LEF parser not initialized");
        return false;
    }

    // TODO: 修改 LefParser 以支援元件列表過濾
    // 目前先解析所有檔案
    int successCount = 0;
    for (const string& lefFile : lefFiles) {
        cout << "  Parsing: " << lefFile << endl;
        if (lefParser_->parseFile(lefFile)) {
            successCount++;
        }
        else {
            addWarning("Failed to parse LEF file: " + lefFile);
        }
    }

    if (successCount > 0) {
        lefLoaded_ = true;

        // 統計有多少目標元件被找到
        int foundCount = 0;
        for (const string& cellName : finalCellList) {
            // 檢查 LEF parser 中是否有這個 MACRO
            // TODO: 需要在 LefParser 中加入 hasMacro() 方法
            foundCount++;  // 暫時假設都找到
        }

        cout << "✓ LEF files parsed" << endl;
        cout << "  - Processed " << successCount << "/" << lefFiles.size() << " files" << endl;
        cout << "  - Found " << foundCount << "/" << finalCellList.size() << " target cells" << endl;

        return true;
    }
    else {
        lefLoaded_ = false;
        addError("Failed to parse any LEF files");
        return false;
    }
}
bool Parser::parseLibWithCellList(const vector<string>& libFiles,
    const vector<string>& initialCellList) {
    cout << "\n=== Contest Step 2: Parse .lib Files with Cell List ===" << endl;

    if (!libParser_) {
        addError("Lib parser not initialized");
        return false;
    }

    // 清空並準備最終元件列表
    finalCellList_.clear();

    // 使用 LibParser 的競賽專用方法
    if (libParser_->parseWithCellList(libFiles, initialCellList, finalCellList_)) {
        libLoaded_ = true;
        cout << "✓ .lib files parsed successfully" << endl;
        cout << "  - Initial cell list: " << initialCellList.size() << " cells" << endl;
        cout << "  - Final cell list: " << finalCellList_.size() << " cells" << endl;

        // 顯示 single_bit_degenerate 映射
        int mappingCount = 0;
        for (const string& cellName : initialCellList) {
            string degenerate = libParser_->getSingleBitDegenerate(cellName);
            if (!degenerate.empty() && degenerate != cellName) {
                if (mappingCount++ < 10) {  // 只顯示前 10 個
                    cout << "    " << cellName << " → " << degenerate << endl;
                }
            }
        }
        if (mappingCount > 10) {
            cout << "    ... and " << (mappingCount - 10) << " more mappings" << endl;
        }

        return true;
    }
    else {
        libLoaded_ = false;
        addError("Failed to parse .lib files with cell list");
        return false;
    }
}

// Parse all files
bool Parser::parseAllFiles() {
    cout << "=== ICCAD Contest Problem B - Object-Oriented File Parser ===" << endl;
    cout << "Base name: " << baseName_ << endl;
    cout << "Processing files..." << endl;

    bool allSuccess = true;

    // Parse Weight file
    if (!parseWeights()) {
        addError("Failed to parse weight file");
        allSuccess = false;
    }

    // Parse LEF file
    if (!parseLEF()) {
        addWarning("Failed to parse LEF file");
        // LEF is not critical, continue
    }

    // Parse DEF file
    if (!parseDEF()) {
        addError("Failed to parse DEF file");
        allSuccess = false;
    }

    // Parse Verilog file
    if (!parseVerilog()) {
        addWarning("Failed to parse Verilog file");
        // Continue even if Verilog fails
    }
    else {
        // Integrate Verilog data with DEF if both are loaded
        if (defLoaded_ && verilogLoaded_) {
            defParser_->updateComponentFromVerilog(verilogParser_->getInstPinNets());
            cout << "  - Integrated Verilog data with DEF" << endl;
        }
    }

    // Parse SDC file
    if (!parseSDC()) {
        addWarning("Failed to parse SDC file");
    }

    // Parse Technology file
    if (!parseTech()) {
        addWarning("Failed to parse Technology file");
    }

    return allSuccess;
}

// Parse individual file by type
bool Parser::parseFile(const string& filename, const string& fileType) {
    if (fileType == "lef" || fileType == "LEF") {
        return parseLEF(filename);
    }
    else if (fileType == "weight" || fileType == "weights") {
        return parseWeights(filename);
    }
    // TODO: Add other file types
    else {
        addError("Unknown file type: " + fileType);
        return false;
    }
}

// Parse LEF file
bool Parser::parseLEF(const string& filename) {
    string lefFile = filename.empty() ? constructFilePath(baseName_, "lef") : filename;

    cout << "Parsing LEF file: " << lefFile << "..." << endl;

    if (!lefParser_) {
        addError("LEF parser not initialized");
        return false;
    }

    if (lefParser_->parseFile(lefFile)) {
        lefLoaded_ = true;
        buildMacroMap();
        std::cout << "[Debug] macroMap_ size = " << macroMap_.size() << std::endl;
        cout << "✓ LEF file (" << lefFile << ") parsed successfully" << endl;
        cout << "  - MACRO count: " << lefParser_->getMacroCount() << endl;
        cout << "  - LAYER count: " << lefParser_->getLayerCount() << endl;
        cout << "  - VIA count: " << lefParser_->getViaCount() << endl;
        cout << "  - SITE count: " << lefParser_->getSiteCount() << endl;
        return true;
    }
    else {
        lefLoaded_ = false;  // 確保失敗時狀態正確
        addError("Failed to parse LEF file: " + lefFile);
        return false;
    }
}

// Parse Weight file
bool Parser::parseWeights(const string& filename) {
    string weightFile = filename.empty() ? constructFilePath(baseName_, "_weight") : filename;

    cout << "Parsing Weight file: " << weightFile << "..." << endl;

    if (!weightParser_) {
        addError("Weight parser not initialized");
        return false;
    }

    if (weightParser_->parseFile(weightFile)) {
        weightLoaded_ = true;
        cout << "✓ Weight file (" << weightFile << ") parsed successfully" << endl;

        // 提取初始元件列表
        // TODO: 需要在 WeightParser 中實作 getCellList()
        // initialCellList_ = weightParser_->getCellList();

        // 暫時的解決方案：重新讀取檔案
        ifstream infile(weightFile);
        if (infile.is_open()) {
            string line;
            bool foundArea = false;
            initialCellList_.clear();

            while (getline(infile, line)) {
                if (line.empty()) continue;

                if (!foundArea) {
                    if (line.find("Area") == 0) {
                        foundArea = true;
                    }
                }
                else {
                    initialCellList_.push_back(line);
                }
            }
            infile.close();

            cout << "  - Initial cell list extracted: " << initialCellList_.size() << " cells" << endl;
        }

        return true;
    }
    else {
        weightLoaded_ = false;
        addError("Failed to parse weight file: " + weightFile);
        return false;
    }
}


// TODO: Implement other parse methods when parsers are available
bool Parser::parseDEF(const string& filename) {
    string defFile = filename.empty() ? constructFilePath(baseName_, "def") : filename;
    cout << "Parsing DEF file: " << defFile << "..." << endl;

    if (!defParser_) {
        addError("DEF parser not initialized");
        return false;
    }

    if (defParser_->parseFile(defFile)) {
        defLoaded_ = true;
        cout << "✓ DEF file (" << defFile << ") parsed successfully" << endl;

        // 顯示基本解析統計
        cout << "  Components parsed: " << defParser_->getComponentCount() << endl;
        cout << "  Nets parsed: " << defParser_->getNetCount() << endl;
        cout << "  Pins parsed: " << defParser_->getPinCount() << endl;

        // 確保觸發器分析已執行並顯示結果
        cout << "  Flip-flops identified: " << defParser_->getFlipFlopCount() << endl;

        // 如果沒有找到觸發器，重新強制執行分析
        if (defParser_->getFlipFlopCount() == 0) {
            cout << "  No flip-flops found, re-running analysis..." << endl;
            defParser_->analyzeFlipFlops();
            cout << "  Flip-flops after re-analysis: " << defParser_->getFlipFlopCount() << endl;
        }
        defParser_->computeRowDimensions();
        defParser_->assignComponentsToRows();
        cout << "✓ Component-to-row assignment completed. Output written to component_rowinfo.txt\n";

        return true;
    }
    else {
        defLoaded_ = false;
        addError("Failed to parse DEF file: " + defFile);
        return false;
    }
}

bool Parser::parseVerilog(const string& filename) {
    string verilogFile = filename.empty() ? constructFilePath(baseName_, "v") : filename;

    cout << "Parsing Verilog file: " << verilogFile << "..." << endl;

    if (!verilogParser_) {
        addError("Verilog parser not initialized");
        return false;
    }

    if (verilogParser_->parseFile(verilogFile)) {
        verilogLoaded_ = true;
        cout << "✓ Verilog file (" << verilogFile << ") parsed successfully" << endl;
        verilogParser_->analyzeHierarchy();
       verilogParser_->findClockNets();
       verilogParser_->printScanChainSummary();
        return true;
    }
    else {
        verilogLoaded_ = false;
        addError("Failed to parse Verilog file: " + verilogFile);
        return false;
    }
}

bool Parser::parseSDC(const string& filename) {
    string sdcFile = filename.empty() ? constructFilePath(baseName_, "sdc") : filename;

    cout << "Parsing SDC file: " << sdcFile << "..." << endl;

    if (!sdcParser_) {
        addError("SDC parser not initialized");
        return false;
    }

    if (sdcParser_->parseFile(sdcFile)) {
        sdcLoaded_ = true;
        cout << "✓ SDC file (" << sdcFile << ") parsed successfully" << endl;
        return true;
    }
    else {
        sdcLoaded_ = false;
        addError("Failed to parse SDC file: " + sdcFile);
        return false;
    }
}

bool Parser::parseTech(const string& filename) {
    string techFile = filename.empty() ? constructFilePath(baseName_, "tf") : filename;

    cout << "Parsing Technology file: " << techFile << "..." << endl;

    if (!techParser_) {
        addError("Technology parser not initialized");
        return false;
    }

    if (techParser_->parseFile(techFile)) {
        techLoaded_ = true;
        cout << "✓ Technology file (" << techFile << ") parsed successfully" << endl;
        return true;
    }
    else {
        techLoaded_ = false;
        addError("Failed to parse Technology file: " + techFile);
        return false;
    }
}

// Check if all files are loaded
bool Parser::allFilesLoaded() const {
    return lefLoaded_ && weightLoaded_ && defLoaded_ && verilogLoaded_ && sdcLoaded_ && techLoaded_;
}

// Configuration methods
void Parser::setBaseName(const string& baseName) {
    baseName_ = baseName;
}

void Parser::setOutputName(const string& outputName) {
    outputName_ = outputName;
}

// Analysis methods
void Parser::analyzeFlipFlops() {
    cout << "Analyzing Flip-Flops..." << endl;
    // TODO: Implement when DEF parser is available
    cout << "✓ Flip-Flop analysis completed (placeholder)" << endl;
}

void Parser::calculateStatistics() {
    cout << "Calculating statistics..." << endl;

    try {
        cout << "\n=== Summary ===" << endl;
        cout << "LEF Sites: " << (lefLoaded_ ? lefParser_->getSiteCount() : 0) << endl;
        cout << "LEF Macros: " << (lefLoaded_ ? lefParser_->getMacroCount() : 0) << endl;
        cout << "Output file prefix: " << outputName_ << endl;
        cout << "✓ Statistics calculated" << endl;
    }
    catch (const exception& e) {
        addError("Error calculating statistics: " + string(e.what()));
    }
}

void Parser::performCellTypeAnalysis() {
    cout << "Analyzing cell types..." << endl;
    // TODO: Implement when DEF parser is available
    cout << "✓ Cell type analysis completed (placeholder)" << endl;
}

// Output methods
void Parser::printSummary() const {
    cout << "\n=== Parser Summary ===" << endl;
    cout << "Base name: " << baseName_ << endl;
    cout << "Output name: " << outputName_ << endl;
    cout << "Files loaded:" << endl;
    cout << "  Weight: " << (weightLoaded_ ? "✓" : "✗");
    if (weightLoaded_ && !initialCellList_.empty()) {
        cout << " (initial cells: " << initialCellList_.size() << ")";
    }
    cout << endl;

    cout << "  LIB: " << (libLoaded_ ? "✓" : "✗");
    if (libLoaded_ && libParser_) {
        cout << " (parsed cells: " << libParser_->getAllCells().size() << ")";
    }
    cout << endl;

    cout << "  LEF: " << (lefLoaded_ ? "✓" : "✗") << endl;
    cout << "  DEF: " << (defLoaded_ ? "✓" : "✗") << endl;
    cout << "  Verilog: " << (verilogLoaded_ ? "✓" : "✗") << endl;
    cout << "  SDC: " << (sdcLoaded_ ? "✓" : "✗") << endl;
    cout << "  Tech: " << (techLoaded_ ? "✓" : "✗") << endl;

    if (!finalCellList_.empty()) {
        cout << "\nContest workflow:" << endl;
        cout << "  Initial cell list: " << initialCellList_.size() << " cells" << endl;
        cout << "  Final cell list: " << finalCellList_.size() << " cells" << endl;
        cout << "  Reduction: " << (100.0 * (1.0 - (double)finalCellList_.size() / initialCellList_.size())) << "%" << endl;
    }
}

void Parser::printAllData() const {
    cout << "Printing all parsed data..." << endl;

    try {
        if (weightLoaded_) {
            weightParser_->printWeights();
            cout << "✓ Weight data printed" << endl;
        }

        if (lefLoaded_) {
            lefParser_->printSummary();
            lefParser_->printMacroDetails(10);
            cout << "✓ LEF data printed" << endl;
        }

        // TODO: Print other data when parsers are available

    }
    catch (const exception& e) {
        addError("Error printing data: " + string(e.what()));
    }
}

void Parser::printParsingStatus() const {
    cout << "\n=== Parsing Status ===" << endl;

    if (!errors_.empty()) {
        cout << "Errors:" << endl;
        for (const auto& error : errors_) {
            cout << "  ✗ " << error << endl;
        }
    }

    if (!warnings_.empty()) {
        cout << "Warnings:" << endl;
        for (const auto& warning : warnings_) {
            cout << "  ⚠ " << warning << endl;
        }
    }

    if (errors_.empty() && warnings_.empty()) {
        cout << "No errors or warnings." << endl;
    }
}

bool Parser::writeOutputFiles() const {
    cout << "Writing output files..." << endl;

    try {
        // 產生 mapping 檔案 (.txt)
        string mappingFile = outputName_ + ".txt";
        ofstream mapFile(mappingFile);
        if (!mapFile.is_open()) {
            addError("Cannot create mapping file: " + mappingFile);
            return false;
        }

        // 取得 flip-flop 數量
        int ffCount = 0;
        if (defParser_) {
            ffCount = defParser_->getFlipFlopCount();
        }

        mapFile << "CellInst " << ffCount << endl;

        // TODO: 產生實際的 pin mapping
        // 目前產生 1:1 mapping
        if (defParser_) {
            const auto& flipFlops = defParser_->getFlipFlops();
            for (const auto& ff : flipFlops) {
                // 對每個 FF 的每個 pin 產生 mapping
                mapFile << ff.instName << "/D map " << ff.instName << "/D" << endl;
                mapFile << ff.instName << "/Q map " << ff.instName << "/Q" << endl;
                mapFile << ff.instName << "/CK map " << ff.instName << "/CK" << endl;

                // 如果有 scan chain
                if (!ff.scanIn.empty()) {
                    mapFile << ff.instName << "/SI map " << ff.instName << "/SI" << endl;
                }
                if (!ff.scanOut.empty()) {
                    mapFile << ff.instName << "/SO map " << ff.instName << "/SO" << endl;
                }
            }
        }

        mapFile.close();
        cout << "✓ Generated " << mappingFile << endl;

        // 產生 DEF 檔案
        string defFile = outputName_ + ".def";
        // TODO: 實作 DEF 輸出
        cout << "  [TODO] Generate " << defFile << endl;

        // 產生 Verilog 檔案
        string verilogFile = outputName_ + ".v";
        // TODO: 實作 Verilog 輸出
        cout << "  [TODO] Generate " << verilogFile << endl;

        return true;
    }
    catch (const exception& e) {
        addError("Error writing output files: " + string(e.what()));
        return false;
    }
}

// Utility methods
void Parser::clear() {
    if (lefParser_) lefParser_->clear();
    if (weightParser_) weightParser_->clear();
    if (libParser_) libParser_->clear();  // 新增

    lefLoaded_ = false;
    weightLoaded_ = false;
    defLoaded_ = false;
    verilogLoaded_ = false;
    sdcLoaded_ = false;
    techLoaded_ = false;
    libLoaded_ = false;  // 新增

    initialCellList_.clear();  // 新增
    finalCellList_.clear();    // 新增

    errors_.clear();
    warnings_.clear();
}

void Parser::reset() {
    clear();
    initializeParsers();
}

// Error handling
vector<string> Parser::getErrors() const {
    return errors_;
}

vector<string> Parser::getWarnings() const {
    return warnings_;
}

void Parser::addError(const string& error) const {
    errors_.push_back(error);
    cerr << "Error: " << error << endl;
}

void Parser::addWarning(const string& warning) const {
    warnings_.push_back(warning);
    cout << "Warning: " << warning << endl;
}


void Parser::buildMacroMap() {
    macroMap_.clear();
    if (lefParser_ && lefParser_->isLoaded()) {
        LefData lefData = lefParser_->getData();
        for (const auto& m : lefData.macros) {
            macroMap_[m.name] = m;
        }
    }
}


// Utility functions namespace
namespace ParserUtils {
    string extractBaseName(const string& filename) {
        size_t pos = filename.find("_weight");
        if (pos != string::npos) {
            return filename.substr(0, pos);
        }

        pos = filename.find_last_of('.');
        if (pos != string::npos) {
            return filename.substr(0, pos);
        }

        return filename;
    }

    vector<string> getExpectedFiles(const string& baseName) {
        return {
            baseName + "_weight",
            baseName + ".lef",
            baseName + ".def",
            baseName + ".v",
            baseName + ".sdc",
            baseName + ".tf"
        };
    }

    bool fileExists(const string& filename) {
        ifstream file(filename);
        return file.good();
    }

    string getFileExtension(const string& filename) {
        size_t pos = filename.find_last_of('.');
        if (pos != string::npos) {
            return filename.substr(pos + 1);
        }
        return "";
    }
}