#include "Parser.h"
#include "DataStructures.h"
#include "place.h"
#include "DPC.h"
#include "Legalizer.h"
#include"LibParser.h"
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

            
            // Perform clustering
            //*dpc test*
            const auto& ffList = parser.getDefParser()->getFlipFlops();
            DensityPeakClustering dpc;
            dpc.setMacroMap(&parser.getMacroMap());

            // 構造 ffLookup（map: instance name → FlipFlopInfo）
            std::map<std::string, FlipFlopInfo> ffLookup;
            for (const auto& ff : ffList) {
                ffLookup[ff.instName] = ff;
            }


            // bankingList 不經 cluster 直接合併
            auto bankingList = dpc.clusterByAllFFs(ffList, parser.getLibParser(), parser.getWeightParser());

            // 這裡要把 ffLookup 傳進去
            dpc.exportBankingDebugReport(bankingList, "banking_debug_report.txt", ffLookup, parser.getLibParser());
            parser.setBankingList(bankingList);


            //write def test
            std::ofstream ofs("original_nets_debug.txt");
            if (!ofs) {
                std::cerr << "Failed to open nets_debug.txt for writing!" << std::endl;
            }
            else {
                ofs << "=== DEF NetInfo Debug Output ===\n";
                ofs << "Total nets: " << parser.getDefData().nets.size() << "\n\n";
                for (const auto& net : parser.getDefData().nets) {
                    ofs << "Net: " << net.name << " (use: " << net.use << ")\n";
                    for (const auto& conn : net.connections) {
                        ofs << "  - Instance: " << conn.instance << ", Pin: " << conn.pin << "\n";
                    }
                    ofs << "---------------------------------\n";
                }
            }

            // 先備份原本的 defdata
            auto defdatacopy = parser.getDefData();
            parser.getDefData().components.clear();

            // 1. 先推 MBFF
            for (const auto& mbff : bankingList) {
                ComponentInfo newComp;
                newComp.name = mbff.newInstanceName;
                newComp.cellType = mbff.mbffCellType;
                newComp.x = mbff.x;
                newComp.y = mbff.y;
                if (!mbff.mergedFFs.empty()) {
                    auto it = ffLookup.find(mbff.mergedFFs[0]);
                    newComp.orient = (it != ffLookup.end()) ? it->second.orient : "N";
                }
                else {
                    newComp.orient = "N";
                }
                parser.getDefData().components.push_back(newComp);
            }

            // 2. 再推回原本 def 裡面的其他非 FF（邏輯閘、buffer 等）
            // 假設 ffLookup 裡面存的都是 FF
            std::set<std::string> ffNames;
            for (const auto& kv : ffLookup) ffNames.insert(kv.first);

            for (const auto& comp : defdatacopy.components) {
                if (ffNames.count(comp.name) == 0) {
                    // 不是 FF，直接加回去
                    parser.getDefData().components.push_back(comp);
                }
            }


            // 1. STEP 1: 建立 instance pin 對 net 的 lookup
            std::map<std::pair<std::string, std::string>, std::string> instPinToNet;
            for (const auto& net : defdatacopy.nets) {
                for (const auto& np : net.connections) {
                    instPinToNet[{np.instance, np.pin}] = net.name;
                }
            }

            // 2. STEP 2: 收集所有被 merge 的 FF instance name
            std::set<std::string> mergedFFs;
            for (const auto& mb : bankingList)
                for (const auto& ff : mb.mergedFFs)
                    mergedFFs.insert(ff);

            // 3. STEP 3: 遍歷所有 nets，**先保留沒有被 merge 的 instance/pin**
            std::vector<NetInfo> newNets;
            for (const auto& net : defdatacopy.nets) {
                NetInfo n = net;
                std::vector<NetPin> filtered;
                for (const auto& np : net.connections) {
                    if (mergedFFs.count(np.instance) == 0) {
                        filtered.push_back(np);
                    }
                }
                if (!filtered.empty()) {
                    n.connections = filtered;
                    newNets.push_back(n);
                }
            }

            // 4. STEP 4: 為每個 MBFF 新增正確的連線
            for (const auto& mbff : bankingList) {
                for (const auto& [mbffPin, origFFPin] : mbff.mbffPinToOrigPin) {
                    auto slashPos = origFFPin.find('/');
                    std::string origInst = origFFPin.substr(0, slashPos);
                    std::string origPin = origFFPin.substr(slashPos + 1);
                    auto netIt = instPinToNet.find({ origInst, origPin });
                    if (netIt == instPinToNet.end()) continue;

                    std::string origNetName = netIt->second;
                    auto found = std::find_if(newNets.begin(), newNets.end(),
                        [&](const NetInfo& n) { return n.name == origNetName; });
                    if (found == newNets.end()) {
                        NetInfo newNet;
                        newNet.name = origNetName;
                        newNet.use = ""; // 可以根據原本 net 填
                        newNet.connections.push_back({ mbff.newInstanceName, mbffPin });
                        newNets.push_back(newNet);
                    }
                    else {
                        found->connections.push_back({ mbff.newInstanceName, mbffPin });
                    }
                }
            }

            // 5. STEP 5: 單顆 FF
            for (const auto& mbff : bankingList) {
                if (mbff.bitWidth == 1) {
                    const std::string& origInst = mbff.newInstanceName;
                    for (const auto& net : defdatacopy.nets) {
                        for (const auto& np : net.connections) {
                            if (np.instance == origInst) {
                                auto found = std::find_if(newNets.begin(), newNets.end(),
                                    [&](const NetInfo& n) { return n.name == net.name; });
                                if (found != newNets.end()) {
                                    auto same = std::find_if(found->connections.begin(), found->connections.end(),
                                        [&](const NetPin& conn) { return conn.instance == np.instance && conn.pin == np.pin; });
                                    if (same == found->connections.end())
                                        found->connections.push_back(np);
                                }
                            }
                        }
                    }
                }
            }

            parser.getDefData().nets = newNets;


            // DEBUG 1: 輸出每個 MBFF/pin 對應原 FF/pin/net
            {
                std::ofstream dbg("banking_net_debug.txt");
                for (const auto& mbff : bankingList) {
                    dbg << "MBFF instance: " << mbff.newInstanceName << " (cell=" << mbff.mbffCellType << ", bitWidth=" << mbff.bitWidth << ")\n";
                    for (const auto& [mbffPin, origFFPin] : mbff.mbffPinToOrigPin) {
                        dbg << "  MBFF pin: " << mbffPin;
                        dbg << "  -> original FF pin: " << origFFPin;
                        auto slashPos = origFFPin.find('/');
                        std::string origInst = origFFPin.substr(0, slashPos);
                        std::string origPin = origFFPin.substr(slashPos + 1);
                        auto netIt = instPinToNet.find({ origInst, origPin });
                        if (netIt != instPinToNet.end())
                            dbg << "  [net=" << netIt->second << "]";
                        else
                            dbg << "  [net=N/A]";
                        dbg << "\n";
                    }
                    dbg << "-----------------------------------\n";
                }
            }

            // DEBUG 2: 輸出完整 net 資訊
            {
                std::ofstream ofs("nets_debug.txt");
                ofs << "=== DEF NetInfo Debug Output ===\n";
                for (const auto& net : parser.getDefData().nets) {
                    ofs << "Net: " << net.name << " (use: " << net.use << ")\n";
                    for (const auto& conn : net.connections) {
                        ofs << "  - Instance: " << conn.instance << ", Pin: " << conn.pin << "\n";
                    }
                    ofs << "---------------------------------\n";
                }
                ofs << "Total nets: " << parser.getDefData().nets.size() << "\n";
            }


        }

        // Generate output files
        cout << "\n=== Output Generation ===" << endl;
        if (!parser.writeOutputFiles()) {
            cerr << "Error: Failed to write output files" << endl;
            return 1;
        }
        if (parser.isDefLoaded() && parser.isLefLoaded()) {
            // Perform flip-flop legalization
            cout << "\n=== Starting Flip-Flop Legalization ===" << endl;
            if (parser.performLegalization()) {
                cout << "✓ All flip-flops legalized successfully" << endl;

                // Write the updated DEF file
                string outputDef = args.outputName + ".def";
                parser.getDefParser()->writeDefFile(outputDef);

                // The legalization report is automatically generated
                // Check: outputName_legalization_report.txt
            }
            else {
                cerr << "Error: Legalization failed" << endl;
                // You might want to continue or return based on your requirements
            }
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