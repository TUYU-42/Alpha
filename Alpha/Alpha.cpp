#include "Parser.h"
#include "DataStructures.h"
#include "ParserLEF.h"
#include "ParserWeights.h"
#include "ParserDEF.h"
#include "ParserVerilog.h"
#include "ParserSDC.h"
#include "ParserTech.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>
#include <chrono>

using namespace std;
using namespace std::chrono;

void printUsage(const char* programName) {
    cout << "Usage: " << programName << " [options]" << endl;
    cout << "Options:" << endl;
    cout << "  -h, --help              Show this help message" << endl;
    cout << "  -b, --base <basename>   Set base filename (default: testcase1)" << endl;
    cout << "  -o, --output <name>     Set output filename (default: output)" << endl;
    cout << "  -f, --file <file>       Parse specific file" << endl;
    cout << "  -v, --verbose           Enable verbose output" << endl;
    cout << "  -q, --quiet             Suppress non-essential output" << endl;
    cout << "  --lef-only              Parse only LEF files" << endl;
    cout << "  --weight-only           Parse only Weight files" << endl;
    cout << "  --def-only              Parse only DEF files" << endl;
    cout << "  --verilog-only          Parse only Verilog files" << endl;
    cout << "  --sdc-only              Parse only SDC files" << endl;
    cout << "  --tech-only             Parse only Technology files" << endl;
    cout << "  --stats                 Show detailed statistics" << endl;
    cout << "  --no-analysis           Skip analysis phase" << endl;
    cout << "  --no-output             Skip output file generation" << endl;
    cout << "  --benchmark             Show timing information" << endl;
}

void printBanner() {
    cout << "=========================================" << endl;
    cout << "  ICCAD Contest Problem B Parser    " << endl;
    cout << "   File Parser        " << endl;
    cout << "=========================================" << endl;
}

void printTimestamp() {
    auto now = system_clock::now();
    auto time_t = system_clock::to_time_t(now);

#ifdef _WIN32
    // Windows 安全版本
    struct tm timeinfo;
    if (localtime_s(&timeinfo, &time_t) == 0) {
        cout << "Execution started at: " << put_time(&timeinfo, "%Y-%m-%d %H:%M:%S") << endl;
    }
    else {
        cout << "Execution started at: [time unavailable]" << endl;
    }
#else
    // Unix/Linux 版本
    struct tm* timeinfo = localtime(&time_t);
    if (timeinfo) {
        cout << "Execution started at: " << put_time(timeinfo, "%Y-%m-%d %H:%M:%S") << endl;
    }
    else {
        cout << "Execution started at: [time unavailable]" << endl;
    }
#endif
}

void analyzeFlipFlopDesign(const DefParser* defParser) {
    if (!defParser || !defParser->isLoaded()) {
        cout << "DEF parser not loaded or available" << endl;
        return;
    }

    cout << "\n=== Flip-Flop Design Analysis ===" << endl;

    // 確保我們有最新的數據
    const auto& flipFlops = defParser->getFlipFlops();
    const auto& components = defParser->getComponents();

    cout << "Data verification:" << endl;
    cout << "  DefParser->getFlipFlopCount(): " << defParser->getFlipFlopCount() << endl;
    cout << "  flipFlops.size(): " << flipFlops.size() << endl;
    cout << "  components.size(): " << components.size() << endl;

    // Cell type statistics
    map<string, int> cellTypeStats;
    int totalFF = 0, totalLogic = 0, totalBuffer = 0;

    for (const auto& comp : components) {
        cellTypeStats[comp.cellType]++;

        // 使用相同的檢測邏輯
        if (DefUtils::isFlipFlopCell(comp.cellType)) {
            totalFF++;
        }
        else if (comp.cellType.find("AND") != string::npos ||
            comp.cellType.find("OR") != string::npos ||
            comp.cellType.find("NAND") != string::npos ||
            comp.cellType.find("NOR") != string::npos ||
            comp.cellType.find("AN2") != string::npos ||
            comp.cellType.find("OR2") != string::npos) {
            totalLogic++;
        }
        else if (comp.cellType.find("BUF") != string::npos ||
            comp.cellType.find("INV") != string::npos) {
            totalBuffer++;
        }
    }

    cout << "\nDesign Statistics:" << endl;
    cout << "  Total Components: " << components.size() << endl;
    cout << "  Flip-Flops: " << totalFF << " ("
        << fixed << setprecision(1) << (components.size() > 0 ? 100.0 * totalFF / components.size() : 0.0) << "%)" << endl;
    cout << "  Logic Gates: " << totalLogic << " ("
        << fixed << setprecision(1) << (components.size() > 0 ? 100.0 * totalLogic / components.size() : 0.0) << "%)" << endl;
    cout << "  Buffers/Inverters: " << totalBuffer << " ("
        << fixed << setprecision(1) << (components.size() > 0 ? 100.0 * totalBuffer / components.size() : 0.0) << "%)" << endl;

    // 驗證分析結果
    cout << "\nFlip-flop analysis verification:" << endl;
    cout << "  Components with FF pattern: " << totalFF << endl;
    cout << "  Stored flip-flops: " << flipFlops.size() << endl;

    if (totalFF != static_cast<int>(flipFlops.size())) {
        cout << "    Mismatch detected! Re-analyzing..." << endl;
        // 強制重新分析
        const_cast<DefParser*>(defParser)->analyzeFlipFlops();
        cout << "  After re-analysis: " << defParser->getFlipFlopCount() << endl;
    }

    // Clock domain analysis
    if (!flipFlops.empty()) {
        cout << "\nClock Domain Analysis:" << endl;
        map<string, vector<string>> clockDomains;
        for (const auto& ff : flipFlops) {
            clockDomains[ff.clockNet].push_back(ff.instName);
        }

        for (const auto& domain : clockDomains) {
            cout << "  Clock '" << domain.first << "': " << domain.second.size() << " flip-flops" << endl;
        }
    }

    // Top cell types
    cout << "\nTop 10 Cell Types:" << endl;
    vector<pair<string, int>> sortedCells(cellTypeStats.begin(), cellTypeStats.end());
    sort(sortedCells.begin(), sortedCells.end(),
        [](const pair<string, int>& a, const pair<string, int>& b) {
            return a.second > b.second;
        });

    for (size_t i = 0; i < min(sortedCells.size(), size_t(10)); ++i) {
        bool isFF = DefUtils::isFlipFlopCell(sortedCells[i].first);
        cout << "  " << left << setw(25) << sortedCells[i].first
            << right << setw(6) << sortedCells[i].second
            << (isFF ? " (FF)" : "") << endl;
    }
}

void analyzeTimingConstraints(const SdcParser* sdcParser) {
    if (!sdcParser || !sdcParser->isLoaded()) return;

    cout << "\n=== Timing Constraints Analysis ===" << endl;

    const auto& clocks = sdcParser->getClocks();
    const auto& constraints = sdcParser->getConstraints();

    cout << "Clock Information:" << endl;
    for (const auto& clock : clocks) {
        double frequency = (clock.period > 0) ? 1000.0 / clock.period : 0.0; // MHz
        cout << "  " << clock.name << ": " << clock.period << " ns ("
            << fixed << setprecision(1) << frequency << " MHz)" << endl;
    }

    cout << "\nConstraint Summary:" << endl;
    map<string, int> constraintTypes;
    for (const auto& constraint : constraints) {
        constraintTypes[constraint.type]++;
    }

    for (const auto& type : constraintTypes) {
        cout << "  " << type.first << ": " << type.second << endl;
    }
}

void generateDesignReport(const string& outputName,
    const DefParser* defParser,
    const VerilogParser* verilogParser,
    const SdcParser* sdcParser,
    const LefParser* lefParser) {
    ofstream report(outputName + "_report.txt");
    if (!report.is_open()) {
        cerr << "Warning: Could not create design report file" << endl;
        return;
    }

    try {
        report << "ICCAD Contest Problem B - Design Analysis Report" << endl;
        report << "Generated: " << __DATE__ << " " << __TIME__ << endl;
        report << "=============================================" << endl << endl;

        if (defParser && defParser->isLoaded()) {
            report << "DEF File Analysis:" << endl;
            report << "  Components: " << defParser->getComponentCount() << endl;
            report << "  Nets: " << defParser->getNetCount() << endl;
            report << "  Pins: " << defParser->getPinCount() << endl;
            report << "  Flip-Flops: " << defParser->getFlipFlopCount() << endl;
            report << endl;
        }

        if (verilogParser && verilogParser->isLoaded()) {
            report << "Verilog File Analysis:" << endl;
            report << "  Modules: " << verilogParser->getModuleCount() << endl;
            report << "  Instances: " << verilogParser->getInstanceCount() << endl;

            auto cellStats = verilogParser->getCellTypeStatistics();
            report << "  Cell Types: " << cellStats.size() << endl;
            report << endl;
        }

        if (sdcParser && sdcParser->isLoaded()) {
            report << "SDC File Analysis:" << endl;
            report << "  Commands: " << sdcParser->getCommandCount() << endl;
            report << "  Clocks: " << sdcParser->getClockCount() << endl;
            report << "  Constraints: " << sdcParser->getConstraintCount() << endl;
            report << endl;
        }

        if (lefParser && lefParser->isLoaded()) {
            report << "LEF File Analysis:" << endl;
            report << "  Macros: " << lefParser->getMacroCount() << endl;
            report << "  Layers: " << lefParser->getLayerCount() << endl;
            report << "  Sites: " << lefParser->getSiteCount() << endl;
            report << "  Vias: " << lefParser->getViaCount() << endl;
            report << endl;
        }

        report.close();
        cout << "✓ Design report generated: " << outputName << "_report.txt" << endl;
    }
    catch (const exception& e) {
        report.close();
        cerr << "Error generating design report: " << e.what() << endl;
    }
}

int main(int argc, char* argv[]) {
    auto startTime = high_resolution_clock::now();

    // Default parameters
    string baseName = "testcase1";
    string outputName = "output";
    bool verbose = false;
    bool quiet = false;
    bool showStats = false;
    bool noAnalysis = false;
    bool noOutput = false;
    bool benchmark = false;
    bool lefOnly = false, weightOnly = false, defOnly = false;
    bool verilogOnly =false , sdcOnly = false, techOnly = false;
    vector<string> specificFiles;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "-b" || arg == "--base") {
            if (i + 1 < argc) {
                baseName = argv[++i];
            }
            else {
                cerr << "Error: " << arg << " requires an argument" << endl;
                return 1;
            }
        }
        else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                outputName = argv[++i];
            }
            else {
                cerr << "Error: " << arg << " requires an argument" << endl;
                return 1;
            }
        }
        else if (arg == "-f" || arg == "--file") {
            if (i + 1 < argc) {
                specificFiles.push_back(argv[++i]);
            }
            else {
                cerr << "Error: " << arg << " requires an argument" << endl;
                return 1;
            }
        }
        else if (arg == "-v" || arg == "--verbose") verbose = true;
        else if (arg == "-q" || arg == "--quiet") quiet = true;
        else if (arg == "--stats") showStats = true;
        else if (arg == "--no-analysis") noAnalysis = true;
        else if (arg == "--no-output") noOutput = true;
        else if (arg == "--benchmark") benchmark = true;
        else if (arg == "--lef-only") lefOnly = true;
        else if (arg == "--weight-only") weightOnly = true;
        else if (arg == "--def-only") defOnly = true;
        else if (arg == "--verilog-only") verilogOnly = true;
        else if (arg == "--sdc-only") sdcOnly = true;
        else if (arg == "--tech-only") techOnly = true;
        else {
            // Try to infer base name from first argument
            if (i == 1 && specificFiles.empty()) {
                baseName = ParserUtils::extractBaseName(arg);
            }
        }
    }

    try {
        if (!quiet) {
            printBanner();
            printTimestamp();
            cout << endl;
        }

        // Create parser manager
        Parser parser(baseName, outputName);

        bool overallSuccess = true;

        // Parse files based on command line options
        if (lefOnly) {
            cout << "LEF-only mode" << endl;
            if (!parser.parseLEF()) {
                cerr << "Failed to parse LEF file" << endl;
                return 1;
            }
        }
        else if (weightOnly) {
            cout << "Weight-only mode" << endl;
            if (!parser.parseWeights()) {
                cerr << "Failed to parse weight file" << endl;
                return 1;
            }
        }
        else if (defOnly) {
            cout << "DEF-only mode" << endl;
            if (!parser.parseDEF()) {
                cerr << "Failed to parse DEF file" << endl;
                return 1;
            }
        }
        else if (verilogOnly) {
            cout << "Verilog-only mode" << endl;
            if (!parser.parseVerilog()) {
                cerr << "Failed to parse Verilog file" << endl;
                return 1;
            }
        }
        else if (sdcOnly) {
            cout << "SDC-only mode" << endl;
            if (!parser.parseSDC()) {
                cerr << "Failed to parse SDC file" << endl;
                return 1;
            }
        }
        else if (techOnly) {
            cout << "Technology-only mode" << endl;
            if (!parser.parseTech()) {
                cerr << "Failed to parse Technology file" << endl;
                return 1;
            }
        }
        else if (!specificFiles.empty()) {
            cout << "Parsing specific files:" << endl;
            for (const auto& file : specificFiles) {
                string ext = ParserUtils::getFileExtension(file);
                if (!parser.parseFile(file, ext)) {
                    cerr << "Failed to parse file: " << file << endl;
                    overallSuccess = false;
                }
            }
        }
        else {
            // Parse all files using the parser manager
            if (!parser.parseAllFiles()) {
                cerr << "Some files failed to parse" << endl;
                overallSuccess = false;
            }
        }
        // Analysis phase
        if (!noAnalysis && !quiet) {
            cout << "\n=== Analysis Phase ===" << endl;

            if (parser.isDefLoaded()) {
                analyzeFlipFlopDesign(parser.getDefParser());
            }

            if (parser.isSdcLoaded()) {
                analyzeTimingConstraints(parser.getSdcParser());
            }

            if (parser.isLefLoaded() && verbose) {
                cout << "\n=== LEF Macro Details ===" << endl;
                parser.getLefParser()->printMacroDetails(showStats ? 20 : 5);
            }
        }

        // Statistics display
        if (showStats || verbose) {
            cout << "\n=== Detailed Statistics ===" << endl;

            if (parser.isWeightLoaded()) {
                parser.getWeightParser()->printWeights();
            }

            if (parser.isLefLoaded()) {
                parser.getLefParser()->printSummary();
            }

            if (parser.isDefLoaded()) {
                parser.getDefParser()->printSummary();
            }

            if (parser.isVerilogLoaded()) {
                parser.getVerilogParser()->printSummary();
            }

            if (parser.isSdcLoaded()) {
                parser.getSdcParser()->printSummary();
            }

            if (parser.isTechLoaded()) {
                parser.getTechParser()->printSummary();
            }
        }

        // Output generation phase
        if (!noOutput) {
            cout << "\n=== Output Generation ===" << endl;

            if (!parser.writeOutputFiles()) {
                cerr << "Failed to write some output files" << endl;
                overallSuccess = false;
            }

            // Generate design report using Parser's methods
            generateDesignReport(outputName,
                parser.getDefParser(),
                parser.getVerilogParser(),
                parser.getSdcParser(),
                parser.getLefParser());
        }

        // Final summary
        auto endTime = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(endTime - startTime);

        cout << "\n=== Execution Summary ===" << endl;
        cout << "Base name: " << baseName << endl;
        cout << "Output prefix: " << outputName << endl;
        cout << "Files processed:" << endl;
        cout << "  LEF: " << (parser.isLefLoaded() ? "✓" : "✗") << endl;
        cout << "  Weight: " << (parser.isWeightLoaded() ? "✓" : "✗") << endl;
        cout << "  DEF: " << (parser.isDefLoaded() ? "✓" : "✗") << endl;
        cout << "  Verilog: " << (parser.isVerilogLoaded() ? "✓" : "✗") << endl;
        cout << "  SDC: " << (parser.isSdcLoaded() ? "✓" : "✗") << endl;
        cout << "  Technology: " << (parser.isTechLoaded() ? "✓" : "✗") << endl;

        if (benchmark) {
            cout << "Execution time: " << duration.count() << " ms" << endl;
        }

        // Show parsing status
        parser.printParsingStatus();

        cout << "\nStatus: " << (overallSuccess ? "SUCCESS" : "PARTIAL SUCCESS") << endl;

        // Show key metrics if available
        if (parser.isDefLoaded() && !quiet) {
            cout << "\nKey Design Metrics:" << endl;
            cout << "  Total Components: " << parser.getDefParser()->getComponentCount() << endl;
            cout << "  Flip-Flops: " << parser.getDefParser()->getFlipFlopCount() << endl;
            cout << "  Nets: " << parser.getDefParser()->getNetCount() << endl;
        }

        if (parser.isLefLoaded() && !quiet) {
            cout << "  Available Macros: " << parser.getLefParser()->getMacroCount() << endl;
        }

        if (parser.isSdcLoaded() && !quiet) {
            cout << "  Clock Domains: " << parser.getSdcParser()->getClockCount() << endl;
        }

        cout << "\n=== Processing completed! ===" << endl;

        return overallSuccess ? 0 : 1;

    }
    catch (const exception& e) {
        cerr << "Fatal error: " << e.what() << endl;
        return 1;
    }
    catch (...) {
        cerr << "Unknown fatal error occurred" << endl;
        return 1;
    }
}