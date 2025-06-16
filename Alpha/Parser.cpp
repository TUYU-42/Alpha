#include "Parser.h"
#include "ParserDEF.h"
#include "ParserVerilog.h"
#include "ParserSDC.h"
#include "ParserTech.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>

using namespace std;

// Constructor
Parser::Parser(const string& baseName, const string& outputName)
    : baseName_(baseName), outputName_(outputName),
    lefLoaded_(false), weightLoaded_(false), defLoaded_(false),
    verilogLoaded_(false), sdcLoaded_(false), techLoaded_(false) {
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
}

// Helper method to construct file paths
string Parser::constructFilePath(const string& base, const string& extension) const {
    if (extension == "_weight") {
        return base + "_weight";
    }
    return base + "." + extension;
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
        return true;
    }
    else {
        weightLoaded_ = false;  // 確保失敗時狀態正確
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
    cout << "  LEF: " << (lefLoaded_ ? "✓" : "✗") << endl;
    cout << "  Weight: " << (weightLoaded_ ? "✓" : "✗") << endl;
    cout << "  DEF: " << (defLoaded_ ? "✓" : "✗") << endl;
    cout << "  Verilog: " << (verilogLoaded_ ? "✓" : "✗") << endl;
    cout << "  SDC: " << (sdcLoaded_ ? "✓" : "✗") << endl;
    cout << "  Tech: " << (techLoaded_ ? "✓" : "✗") << endl;
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
        // TODO: Implement output file writing when all parsers are available
        cout << "✓ Output files written (placeholder)" << endl;
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

    lefLoaded_ = false;
    weightLoaded_ = false;
    defLoaded_ = false;
    verilogLoaded_ = false;
    sdcLoaded_ = false;
    techLoaded_ = false;

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