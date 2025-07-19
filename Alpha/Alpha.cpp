#include "Parser.h"
#include "DataStructures.h"
#include "place.h"
#include "dpc.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>
#include <chrono>

using namespace std;
using namespace std::chrono;

// 競賽專用的命令列參數結構
struct ContestArgs {
    vector<string> weightFiles;
    vector<string> lefFiles;
    vector<string> libFiles;
    vector<string> defFiles;
    vector<string> verilogFiles;
    vector<string> sdcFiles;
    vector<string> tfFiles;
    string outputName;
};

void printUsage(const char* programName) {
    cout << "Usage: " << programName << " [options]" << endl;
    cout << "Contest format:" << endl;
    cout << "  -weight <file>         Weight file (required)" << endl;
    cout << "  -lib <file1> <file2>   Library files (.lib)" << endl;
    cout << "  -lef <file1> <file2>   LEF files" << endl;
    cout << "  -def <file1> <file2>   DEF files" << endl;
    cout << "  -v <file1> <file2>     Verilog files" << endl;
    cout << "  -sdc <file1> <file2>   SDC files" << endl;
    cout << "  -tf <file1> <file2>    Technology files" << endl;
    cout << "  -out <name>            Output name (required)" << endl;
}

void printBanner() {
    cout << "=========================================" << endl;
    cout << "  ICCAD Contest Problem B Parser" << endl;
    cout << "  Multi-bit Flip-Flop Optimization" << endl;
    cout << "=========================================" << endl;
}

// 解析競賽格式的命令列參數
ContestArgs parseContestArgs(int argc, char* argv[]) {
    ContestArgs args;

    for (int i = 1; i < argc; i++) {
        string arg = argv[i];

        if (arg == "-weight") {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                args.weightFiles.push_back(argv[++i]);
            }
        }
        else if (arg == "-lib") {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                args.libFiles.push_back(argv[++i]);
            }
        }
        else if (arg == "-lef") {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                args.lefFiles.push_back(argv[++i]);
            }
        }
        else if (arg == "-def") {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                args.defFiles.push_back(argv[++i]);
            }
        }
        else if (arg == "-v") {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                args.verilogFiles.push_back(argv[++i]);
            }
        }
        else if (arg == "-sdc") {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                args.sdcFiles.push_back(argv[++i]);
            }
        }
        else if (arg == "-tf") {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                args.tfFiles.push_back(argv[++i]);
            }
        }
        else if (arg == "-out") {
            if (i + 1 < argc) {
                args.outputName = argv[++i];
            }
        }
        else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            exit(0);
        }
    }

    return args;
}

// 執行競賽專用的三步驟工作流程
bool executeContestWorkflow(Parser& parser, const ContestArgs& args) {
    cout << "\n=== Executing Contest Workflow ===" << endl;

    // NEW STEP 1: Parse all .lib files first to get FF cell list
    cout << "\n=== STEP 1: Parse .lib Files to Identify FF Cells ===" << endl;
    if (!args.libFiles.empty()) {
        // Parse all lib files to find FF cells
        if (!parser.parseAllLibraries(args.libFiles)) {
            cerr << "Error: Failed to parse .lib files" << endl;
            return false;
        }

        // Get the FF cell list
        set<string> ffCellList = parser.getLibParser()->getFFCellList();
        cout << "  Found " << ffCellList.size() << " FF cells in libraries" << endl;

        // Optional: Print first few FF cells
        int count = 0;
        for (const auto& cell : ffCellList) {
            if (count++ < 10) {
                cout << "    - " << cell << endl;
            }
        }
        if (ffCellList.size() > 10) {
            cout << "    ... and " << (ffCellList.size() - 10) << " more" << endl;
        }
    }

    // STEP 2: Parse Weight file
    cout << "\n=== STEP 2: Parse Weight File ===" << endl;
    if (args.weightFiles.empty()) {
        cerr << "Error: No weight file specified" << endl;
        return false;
    }

    if (!parser.parseWeights(args.weightFiles[0])) {
        cerr << "Error: Failed to parse weight file" << endl;
        return false;
    }

    // STEP 3: Parse LEF files (now we know which cells are FFs)
    if (!args.lefFiles.empty()) {
        cout << "\n=== STEP 3: Parse .lef Files ===" << endl;
        for (const string& lefFile : args.lefFiles) {
            if (!parser.parseLEF(lefFile)) {
                cerr << "Warning: Failed to parse LEF file: " << lefFile << endl;
            }
        }
    }

    // STEP 4: Parse design files (DEF, Verilog)
    cout << "\n=== STEP 4: Parse Design Files ===" << endl;

    // Parse DEF - now we can identify FF instances
    if (!args.defFiles.empty()) {
        cout << "  Parsing DEF file..." << endl;
        if (!parser.parseDEF(args.defFiles[0])) {
            cerr << "Error: Failed to parse DEF file" << endl;
            return false;
        }

        // After parsing DEF, identify FF instances
        parser.identifyFFInstances();
    }

    // Parse Verilog
    if (!args.verilogFiles.empty()) {
        cout << "  Parsing Verilog file..." << endl;
        if (!parser.parseVerilog(args.verilogFiles[0])) {
            cerr << "Warning: Failed to parse Verilog file" << endl;
        }
    }

    // Parse other files
    if (!args.sdcFiles.empty()) {
        parser.parseSDC(args.sdcFiles[0]);
    }

    if (!args.tfFiles.empty()) {
        parser.parseTech(args.tfFiles[0]);
    }

    // STEP 5: Group FF instances by cell type for pre-banking analysis
    cout << "\n=== STEP 5: Group FF Instances by Cell Type ===" << endl;
    parser.groupFFInstancesByType();

    return true;
}

int main(int argc, char* argv[]) {
    auto startTime = high_resolution_clock::now();

    printBanner();

    // Parse command line arguments
    ContestArgs args = parseContestArgs(argc, argv);

    // Validate required arguments
    if (args.weightFiles.empty() || args.outputName.empty()) {
        cerr << "Error: Missing required arguments" << endl;
        printUsage(argv[0]);
        return 1;
    }

    // Display parsed files
    cout << "\nParsed command line:" << endl;
    cout << "  Weight files: " << args.weightFiles.size() << endl;
    cout << "  Library files: " << args.libFiles.size() << endl;
    cout << "  LEF files: " << args.lefFiles.size() << endl;
    cout << "  DEF files: " << args.defFiles.size() << endl;
    cout << "  Verilog files: " << args.verilogFiles.size() << endl;
    cout << "  SDC files: " << args.sdcFiles.size() << endl;
    cout << "  Tech files: " << args.tfFiles.size() << endl;
    cout << "  Output name: " << args.outputName << endl;

    try {
        // Create Parser object
        Parser parser("", args.outputName);

        // Execute contest workflow with new .lib-first approach
        if (!executeContestWorkflow(parser, args)) {
            cerr << "Error: Contest workflow failed" << endl;
            return 1;
        }

        // Analysis and optimization phase
        if (parser.isDefLoaded()) {
            cout << "\n=== Analysis Phase ===" << endl;

            // The FF instances are already identified and grouped by type
            const auto& ffGroups = parser.getFFGroupsByType();

            cout << "\nFF Banking Opportunities:" << endl;

            // Analyze each FF type group
            for (const auto& [cellType, instances] : ffGroups) {
                const LibCell* libCell = parser.getLibParser()->getCell(cellType);

                if (libCell && instances.size() >= 2) {
                    // Check banking opportunities
                    if (cellType.find("2_") == string::npos &&
                        cellType.find("4_") == string::npos) {
                        // This is a single-bit FF
                        cout << "\n  Cell type: " << cellType << endl;
                        cout << "    Instances: " << instances.size() << endl;

                        // Look for 2-bit and 4-bit targets
                        string degenerate = libCell->singleBitDegenerate;
                        if (!degenerate.empty()) {
                            cout << "    Can degenerate from: " << degenerate << endl;
                        }

                        // Find potential MBFF targets
                        set<string> mbffTargets;
                        for (const auto& [mbffType, mbffCell] : parser.getLibParser()->getAllCells()) {
                            if (mbffCell.singleBitDegenerate == cellType) {
                                mbffTargets.insert(mbffType);
                            }
                        }

                        if (!mbffTargets.empty()) {
                            cout << "    Potential MBFF targets:" << endl;
                            for (const auto& target : mbffTargets) {
                                cout << "      - " << target << endl;
                            }
                        }
                    }
                }
            }

            // Perform clustering
            cout << "\n=== Hierarchical Clustering Phase ===" << endl;
            parser.performHierarchicalClustering();

            //*dpc test*
            const HierarchicalClustering* clustering = parser.getHierarchicalClustering();
            std::map<std::string, FlipFlopInfo> ffLookup;
            const auto& clockDomains = clustering->getClockDomains();
            for (const auto& [clk, ffs] : clockDomains) {
                for (const auto& ff : ffs) {
                    ffLookup[ff.instName] = ff;
                }
            }
            DensityPeakClustering dpc;
            dpc.setMacroMap(&parser.getMacroMap());
            const auto& clusteredDesign = clustering->getClusteredDesign();
            auto result = dpc.clusterByScanChain(clusteredDesign, ffLookup);

            // Perform banking optimization
            cout << "\n=== Banking Optimization Phase ===" << endl;
            parser.performBankingOptimization();
        }

        // Generate output files
        cout << "\n=== Output Generation ===" << endl;
        if (!parser.writeOutputFiles()) {
            cerr << "Error: Failed to write output files" << endl;
            return 1;
        }

        // Display execution statistics
        auto endTime = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(endTime - startTime);

        cout << "\n=== Execution Summary ===" << endl;
        parser.printSummary();
        cout << "Execution time: " << duration.count() << " ms" << endl;

        cout << "\n=== Processing completed successfully! ===" << endl;

        return 0;

    }
    catch (const exception& e) {
        cerr << "Fatal error: " << e.what() << endl;
        return 1;
    }
}
void demonstrateClusteringUsage(const Parser& parser) {
    if (!parser.isClusteringPerformed()) {
        cout << "Clustering not performed, skipping demonstration" << endl;
        return;
    }

    const HierarchicalClustering* clustering = parser.getHierarchicalClustering();
    const auto& clusteredDesign = clustering->getClusteredDesign();

    cout << "\n=== Demonstrating Clustering Usage ===" << endl;

    // Example 1: Iterate through all clock domains
    for (const auto& [clockNet, scanChains] : clusteredDesign) {
        cout << "\nClock domain: " << clockNet << endl;

        // Example 2: Find chains suitable for 4-bit banking
        vector<const ScanChain*> bankableFours;
        for (const auto& chain : scanChains) {
            if (chain.length() >= 4) {
                bankableFours.push_back(&chain);
            }
        }

        if (!bankableFours.empty()) {
            cout << "  Found " << bankableFours.size()
                << " chains with 4+ FFs (suitable for 4-bit MBFF)" << endl;
        }

        // Example 3: Find isolated FFs
        int isolatedCount = 0;
        for (const auto& chain : scanChains) {
            if (chain.length() == 1) {
                isolatedCount++;
            }
        }

        if (isolatedCount > 0) {
            cout << "  Found " << isolatedCount << " isolated FFs" << endl;
        }
    }
}