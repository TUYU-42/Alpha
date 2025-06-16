#ifndef PARSER_H
#define PARSER_H

#include "DataStructures.h"
#include "ParserLEF.h"
#include "ParserWeights.h"
#include <string>
#include <vector>
#include <memory>

// Forward declarations for other parsers
class DefParser;
class VerilogParser;
class SdcParser;
class TechParser;

// Main Parser Manager Class
class Parser {
private:
    // Parser instances
    std::unique_ptr<LefParser> lefParser_;
    std::unique_ptr<WeightParser> weightParser_;
    std::unique_ptr<DefParser> defParser_;
    std::unique_ptr<VerilogParser> verilogParser_;
    std::unique_ptr<SdcParser> sdcParser_;
    std::unique_ptr<TechParser> techParser_;

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

    // Helper methods
    std::string constructFilePath(const std::string& base, const std::string& extension) const;
    void initializeParsers();

public:
    // Constructor & Destructor
    explicit Parser(const std::string& baseName = "testcase1",
        const std::string& outputName = "output");
    ~Parser();

    // Copy constructor and assignment operator (deleted for now)
    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;

    // Move constructor and assignment operator
    Parser(Parser&&) = default;
    Parser& operator=(Parser&&) = default;

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

    // Data access methods (const versions)
    const LefParser* getLefParser() const { return lefParser_.get(); }
    const WeightParser* getWeightParser() const { return weightParser_.get(); }
    const DefParser* getDefParser() const { return defParser_.get(); }
    const VerilogParser* getVerilogParser() const { return verilogParser_.get(); }
    const SdcParser* getSdcParser() const { return sdcParser_.get(); }
    const TechParser* getTechParser() const { return techParser_.get(); }

    // Data access methods (non-const versions)
    LefParser* getLefParser() { return lefParser_.get(); }
    WeightParser* getWeightParser() { return weightParser_.get(); }
    DefParser* getDefParser() { return defParser_.get(); }
    VerilogParser* getVerilogParser() { return verilogParser_.get(); }
    SdcParser* getSdcParser() { return sdcParser_.get(); }
    TechParser* getTechParser() { return techParser_.get(); }

    // Status methods
    bool isLefLoaded() const { return lefLoaded_; }
    bool isWeightLoaded() const { return weightLoaded_; }
    bool isDefLoaded() const { return defLoaded_; }
    bool isVerilogLoaded() const { return verilogLoaded_; }
    bool isSdcLoaded() const { return sdcLoaded_; }
    bool isTechLoaded() const { return techLoaded_; }
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

private:
    // Error tracking
    mutable std::vector<std::string> errors_;
    mutable std::vector<std::string> warnings_;

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