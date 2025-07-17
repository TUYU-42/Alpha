#include "Parser.h"
#include "DataStructures.h"
#include "place.h"
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

    // STEP 1: 解析 Weight 檔案，取得初始元件列表
    cout << "\n=== STEP 1: Parse Weight File ===" << endl;
    if (args.weightFiles.empty()) {
        cerr << "Error: No weight file specified" << endl;
        return false;
    }

    if (!parser.parseWeights(args.weightFiles[0])) {
        cerr << "Error: Failed to parse weight file" << endl;
        return false;
    }

    // 取得初始元件列表
    vector<string> initialCellList;
    if (parser.getWeightParser()) {
        // TODO: 需要在 WeightParser 中加入 getCellList() 方法
        // initialCellList = parser.getWeightParser()->getCellList();

        // 暫時的解決方案：從 weight 檔案重新讀取
        ifstream weightFile(args.weightFiles[0]);
        string line;
        bool foundArea = false;

        while (getline(weightFile, line)) {
            if (line.empty()) continue;

            if (!foundArea) {
                if (line.find("Area") == 0) {
                    foundArea = true;
                }
            }
            else {
                // Area 之後的都是元件名稱
                initialCellList.push_back(line);
            }
        }
        weightFile.close();
    }

    cout << "  Initial cell list: " << initialCellList.size() << " cells" << endl;

    // STEP 2: 解析 .lib 檔案，建立最終元件列表
    if (!args.libFiles.empty()) {
        cout << "\n=== STEP 2: Parse .lib Files with Cell List ===" << endl;

        // TODO: 實作 parseLibWithCellList
        // 這裡需要新增一個 LibParser 類別
        cout << "  [TODO] Need to implement LibParser for contest workflow" << endl;

        // 暫時使用初始列表作為最終列表
        set<string> finalCellList(initialCellList.begin(), initialCellList.end());
        parser.setFinalCellList(finalCellList);
    }

    // STEP 3: 解析 .lef 檔案，只處理最終列表中的元件
    if (!args.lefFiles.empty()) {
        cout << "\n=== STEP 3: Parse .lef Files with Final Cell List ===" << endl;

        for (const string& lefFile : args.lefFiles) {
            if (!parser.parseLEF(lefFile)) {
                cerr << "Warning: Failed to parse LEF file: " << lefFile << endl;
            }
        }
    }

    // STEP 4: 解析設計檔案
    cout << "\n=== STEP 4: Parse Design Files ===" << endl;

    // 解析 DEF
    if (!args.defFiles.empty()) {
        if (!parser.parseDEF(args.defFiles[0])) {
            cerr << "Error: Failed to parse DEF file" << endl;
            return false;
        }
    }

    // 解析 Verilog
    if (!args.verilogFiles.empty()) {
        if (!parser.parseVerilog(args.verilogFiles[0])) {
            cerr << "Warning: Failed to parse Verilog file" << endl;
        }
    }

    // 解析其他檔案
    if (!args.sdcFiles.empty()) {
        parser.parseSDC(args.sdcFiles[0]);
    }

    if (!args.tfFiles.empty()) {
        parser.parseTech(args.tfFiles[0]);
    }

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

        // Execute contest workflow
        if (!executeContestWorkflow(parser, args)) {
            cerr << "Error: Contest workflow failed" << endl;
            return 1;
        }

        // Analysis and optimization phase
        if (parser.isDefLoaded()) {
            cout << "\n=== Analysis Phase ===" << endl;

            // Step 1: Analyze Flip-Flops (basic analysis)
            parser.analyzeFlipFlops();

            // ====== NEW: Hierarchical Clustering ======
            // Step 2: Perform hierarchical clustering
            cout << "\n=== Hierarchical Clustering Phase ===" << endl;
            parser.performHierarchicalClustering();

            // Check if clustering was successful
            if (parser.isClusteringPerformed()) {
                // Option to print detailed report
                bool printDetailed = false; // Set to true for verbose output
                if (printDetailed) {
                    parser.printClusteringResults();
                }

                // The clustering results are now available for use
                const HierarchicalClustering* clustering = parser.getHierarchicalClustering();
                if (clustering) {
                    const auto& stats = clustering->getStatistics();
                    cout << "\nClustering completed successfully:" << endl;
                    cout << "  - Clock domains: " << stats.totalClockDomains << endl;
                    cout << "  - Total scan chains: " << stats.totalScanChains << endl;
                    cout << "  - Results exported to: " << args.outputName << "_clustering.txt" << endl;
                }
            }
            else {
                cerr << "Warning: Hierarchical clustering failed or no flip-flops found" << endl;
            }

            //DPC test


            // ====== UPDATED: Banking Optimization ======
            // Step 3: Perform banking optimization (now uses clustering results)
            cout << "\n=== Banking Optimization Phase ===" << endl;
            parser.performBankingOptimization();

            // ====== Existing Placer Test (Optional) ======
            // Get macroMap and components for placer
            const auto& macroMap = parser.getMacroMap();
            const auto& components = parser.getDefParser()->getComponents();

            Placer placer(macroMap, components);

            // Print some instance-macro mappings
            cout << "\n=== Placer Instance-Macro Mappings ===" << endl;
            placer.printSomeMappings(10);

            // ====== Additional Analysis (Optional) ======
            // You can add more analysis based on clustering results
            if (parser.isClusteringPerformed()) {
                const auto* clustering = parser.getHierarchicalClustering();
                const auto& clockDomains = clustering->getClockDomains();

                // Example: Find largest clock domain
                string largestDomain;
                size_t maxSize = 0;
                for (const auto& [clock, ffs] : clockDomains) {
                    if (ffs.size() > maxSize) {
                        maxSize = ffs.size();
                        largestDomain = clock;
                    }
                }

                cout << "\nLargest clock domain: " << largestDomain
                    << " with " << maxSize << " flip-flops" << endl;
            }
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

        // Add clustering summary to final output
        if (parser.isClusteringPerformed()) {
            cout << "\nHierarchical Clustering: ✓ Completed" << endl;
        }
        else {
            cout << "\nHierarchical Clustering: ✗ Not performed" << endl;
        }

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