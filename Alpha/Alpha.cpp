#include "Parser.h"
#include "DataStructures.h"
#include "place.h"
#include "DPC.h"
#include "Legalizer.h"
#include "WriteOutput.h"
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
    vector<string> dbFiles;

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
    cout << "  -db <file1> <file2>     Database files" << endl;
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
        else if (arg == "-db") {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                args.dbFiles.push_back(argv[++i]);
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

   if (!args.verilogFiles.empty()) {
        cout << "  Parsing Verilog file..." << endl;
        if (!parser.parseVerilog(args.verilogFiles[0])) {
            cerr << "Warning: Failed to parse Verilog file" << endl;
        }
    }

    // Parse other files
   /* if (!args.sdcFiles.empty()) {
        parser.parseSDC(args.sdcFiles[0]);
    }

    if (!args.tfFiles.empty()) {
        parser.parseTech(args.tfFiles[0]);
    }*/

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
        if (!args.verilogFiles.empty()) {
            parser.setInputVerilogFile(args.verilogFiles[0]);
        }
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
            for (auto ff_it = ffGroups.begin(); ff_it != ffGroups.end(); ++ff_it) {
                const std::string& cellType = ff_it->first;
                const std::vector<FlipFlopInfo>& instances = ff_it->second;

                const LibCell* libCell = parser.getLibParser()->getCell(cellType);

                if (libCell && instances.size() >= 2) {
                    if (cellType.find("2_") == std::string::npos &&
                        cellType.find("4_") == std::string::npos) {
                        cout << "\n  Cell type: " << cellType << endl;
                        cout << "    Instances: " << instances.size() << endl;

                        std::string degenerate = libCell->singleBitDegenerate;
                        if (!degenerate.empty()) {
                            cout << "    Can degenerate from: " << degenerate << endl;
                        }

                        set<string> mbffTargets;
                        const std::map<std::string, LibCell>& allCells = parser.getLibParser()->getAllCells();
                        for (std::map<std::string, LibCell>::const_iterator cell_it = allCells.begin(); cell_it != allCells.end(); ++cell_it) {
                            const std::string& mbffType = cell_it->first;
                            const LibCell& mbffCell = cell_it->second;
                            if (mbffCell.singleBitDegenerate == cellType) {
                                mbffTargets.insert(mbffType);
                            }
                        }

                        if (!mbffTargets.empty()) {
                            cout << "    Potential MBFF targets:" << endl;
                            for (set<string>::const_iterator target_it = mbffTargets.begin(); target_it != mbffTargets.end(); ++target_it) {
                                cout << "      - " << *target_it << endl;
                            }
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
        const std::map<std::string, std::vector<FlipFlopInfo>>& clockDomains = clustering->getClockDomains();

        for (std::map<std::string, std::vector<FlipFlopInfo>>::const_iterator it = clockDomains.begin(); it != clockDomains.end(); ++it) {
            const std::string& clk = it->first;
            const std::vector<FlipFlopInfo>& ffs = it->second;

            for (std::vector<FlipFlopInfo>::const_iterator ffIt = ffs.begin(); ffIt != ffs.end(); ++ffIt) {
                ffLookup[ffIt->instName] = *ffIt;
            }
        }
        DensityPeakClustering dpc;


        dpc.setMacroMap(&parser.getMacroMap());
        dpc.setLibParser(parser.getLibParser());

        //  const auto& clusteredDesign = clustering->getClusteredDesign();
        //  auto result = dpc.clusterByScanChain(clusteredDesign, ffLookup);
        auto result = dpc.clusterByClockNet(ffLookup, /*autoTune=*/true);


        // 印出每個 clock 的分群數量
        for (std::map<std::string, std::vector<DPCCluster>>::const_iterator it = result.begin(); it != result.end(); ++it) {
            const std::string& clk = it->first;
            const std::vector<DPCCluster>& clusters = it->second;
            std::cout << "ClockNet: " << clk << " → " << clusters.size() << " clusters\n";
        }

        dpc.printClusteringSummary();
        dpc.analyzeSingleBitMergeCandidates();
        dpc.reportMergedFFResults();

        DefData defData = parser.getDefParser()->getDefData(); // 用 -> 而不是 .
        LefData lefData = parser.getLefParser()->getData();// 同上  const auto& defData = parser.getDefParser().getComponentMap();

        // 產生 placement 結果
        auto cleanComponents = dpc.generatePlacementComponents(defData, lefData);
        auto cleanFFs = dpc.generatePlacementFFsNew(defData, lefData);
        dpc.dumpNewFFsToTxt(cleanFFs, "cleaned_newFFs.txt");
        dpc.dumpPlacedComponentsToTxt(cleanComponents, "placed_components.txt");

        // 重建 defData.components 內容（完全替換）
        defData.components.clear();

        for (const auto& comp : cleanComponents) {
            ComponentInfo info;
            info.name = !comp.instanceName.empty() ? comp.instanceName : comp.instanceName;
            info.cellType = comp.cellType;
            info.x = comp.x;
            info.y = comp.y;
            info.orient = !comp.orientation.empty() ? comp.orientation : comp.orientation;
            info.rowName = "";
            info.status = "";
            defData.components.push_back(info);
        }

        // optional: show mapping
        MergeMapping map = dpc.getMergeMap();
        map.printMappings();
        cout << "\n=== Updating Parser State with DPC Results ===" << endl;
        parser.getDefParser()->setDefData(defData);
        // 進行 legalize
        parser.performLegalization();


     

        const DefData& finalDefData = parser.getDefParser()->getDefData();

        // Step 1: 創建 WriteOutput 物件
        WriteOutput writer(args.outputName,
            dpc.getMergeMap(),           // 合併映射
            finalDefData,                 // 最終 DEF 資料
            dpc.getMergedFFResults(),     // 合併結果
            parser.getVerilogParser());   // Verilog Parser

        // Step 2: 設定 LibParser（重要！用於判斷 FF 和取得 pin 資訊）
        writer.setLibParser(parser.getLibParser());

        // Step 3: 產生輸出檔案
        cout << "\n--- Generating mapping list ---" << endl;
        if (!writer.writeMapList()) {
            cerr << "Error: Failed to generate mapping list" << endl;
            return 1;
        }
        cout << "✓ Generated " << args.outputName << ".list" << endl;

        cout << "\n--- Generating Verilog netlist ---" << endl;
        if (!writer.writeVerilog()) {
            cerr << "Error: Failed to generate Verilog netlist" << endl;
            return 1;
        }
        cout << "✓ Generated " << args.outputName << ".v" << endl;
        if (!writer.writeDef()) {
            cerr << "Error: Failed to generate Verilog netlist" << endl;
            return 1;
        }
        cout << "✓ Generated " << args.outputName << ".def" << endl;
        // Step 4: 產生 DEF 檔案（使用 DefParser）
        //cout << "\n--- Generating DEF file ---" << endl;
       // string outputDef = args.outputName + ".def";
       // if (!parser.getDefParser()->writeDefFile(outputDef)) {
        //    cerr << "Error: Failed to generate DEF file" << endl;
         //   return 1;
      //  }
       // cout << "✓ Generated " << outputDef << endl;

        // Step 5: 驗證輸出檔案
        cout << "\n--- Verifying output files ---" << endl;

        // 檢查檔案是否存在
        ifstream checkList(args.outputName + ".list");
        ifstream checkVerilog(args.outputName + ".v");
        ifstream checkDef(args.outputName + ".def");

        if (checkList.good() && checkVerilog.good() && checkDef.good()) {
            cout << "✓ All output files generated successfully!" << endl;

            // 顯示檔案大小
            checkList.seekg(0, ios::end);
            checkVerilog.seekg(0, ios::end);
            checkDef.seekg(0, ios::end);

            cout << "\nOutput file sizes:" << endl;
            cout << "  " << args.outputName << ".list: "
                << checkList.tellg() << " bytes" << endl;
            cout << "  " << args.outputName << ".v: "
                << checkVerilog.tellg() << " bytes" << endl;
            cout << "  " << args.outputName << ".def: "
                << checkDef.tellg() << " bytes" << endl;
        }
        else {
            cerr << "✗ Some output files are missing or corrupted!" << endl;
            return 1;
        }

        checkList.close();
        checkVerilog.close();
        checkDef.close();

        // ============ 結束 ============
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
// 驗證階層映射
void verifyHierarchicalMapping(const Parser& parser) {
    cout << "\n=== Verifying Hierarchical Mapping ===" << endl;

    const VerilogParser* vParser = parser.getVerilogParser();
    if (!vParser) {
        cout << "No VerilogParser available" << endl;
        return;
    }

    // 取得所有 FF 的階層路徑
    auto ffPaths = vParser->getFFInstancesWithPaths();
    cout << "Total FF instances with paths: " << ffPaths.size() << endl;

    // 顯示前幾個映射
    int count = 0;
    for (const auto& pair : ffPaths) {
        if (count++ >= 10) break;
        cout << "  Local: " << pair.second
            << " -> Full: " << pair.first << endl;
    }

    if (ffPaths.size() > 10) {
        cout << "  ... and " << (ffPaths.size() - 10) << " more" << endl;
    }
}

// 驗證合併映射
void verifyMergeMapping(const MergeMapping& mergeMap) {
    cout << "\n=== Verifying Merge Mapping ===" << endl;

    cout << "Total merged single-bit FFs: "
        << mergeMap.singleToMultiBitName.size() << endl;
    cout << "Total multi-bit FFs created: "
        << mergeMap.multiBitToSingles.size() << endl;

    // 顯示前幾個映射
    int count = 0;
    for (const auto& pair : mergeMap.multiBitToSingles) {
        if (count++ >= 5) break;
        cout << "\nMBFF: " << pair.first << endl;
        cout << "  Contains " << pair.second.size() << " single-bit FFs:" << endl;
        for (const auto& singleFF : pair.second) {
            cout << "    - " << singleFF << endl;
        }
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
    for (std::map<std::string, std::vector<ScanChainClustered> >::const_iterator it = clusteredDesign.begin();
        it != clusteredDesign.end(); ++it) {
        const std::string& clockNet = it->first;
        const std::vector<ScanChainClustered>& scanChains = it->second;

        cout << "\nClock domain: " << clockNet << endl;

        // Example 2: Find chains suitable for 4-bit banking
        std::vector<const ScanChainClustered*> bankableFours;
        for (std::vector<ScanChainClustered>::const_iterator chain_it = scanChains.begin();
            chain_it != scanChains.end(); ++chain_it) {
            if (chain_it->length() >= 4) {
                // bankableFours.push_back(&(*chain_it));
            }
        }

        if (!bankableFours.empty()) {
            cout << "  Found " << bankableFours.size()
                << " chains with 4+ FFs (suitable for 4-bit MBFF)" << endl;
        }

        // Example 3: Find isolated FFs
        int isolatedCount = 0;
        for (std::vector<ScanChainClustered>::const_iterator chain_it = scanChains.begin();
            chain_it != scanChains.end(); ++chain_it) {
            if (chain_it->length() == 1) {
                isolatedCount++;
            }
        }

        if (isolatedCount > 0) {
            cout << "  Found " << isolatedCount << " isolated FFs" << endl;
        }
    }

}