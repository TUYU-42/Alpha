#include "Parser.h"
#include "DataStructures.h"
#include "Legalizer.h"
#include "WriteOutput.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>
#include <chrono>

// NEW: Firefly includes
#include "FireflyPreprocess.h"
#include "Firefly.h"

// (Optional) if your compatible tables live in another TU, declare extern here
extern std::unordered_map<std::string, std::vector<std::string>> bankingCompatibleTable;
extern std::unordered_map<std::string, std::vector<std::string>> debankingCompatibleTable;

using namespace std;
using namespace std::chrono;

// =========================
// Contest CLI args (same as your original)
// =========================
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

static void printUsage(const char* programName) {
    cout << "Usage: " << programName << " [options]\n";
    cout << "Contest format:\n";
    cout << "  -weight <file>         Weight file (required)\n";
    cout << "  -lib <file1> <file2>   Library files (.lib)\n";
    cout << "  -lef <file1> <file2>   LEF files\n";
    cout << "  -def <file1> <file2>   DEF files\n";
    cout << "  -v <file1> <file2>     Verilog files\n";
    cout << "  -sdc <file1> <file2>   SDC files\n";
    cout << "  -tf <file1> <file2>    Technology files\n";
    cout << "  -db <file1> <file2>    Database files\n";
    cout << "  -out <name>            Output name (required)\n";
}

static void printBanner() {
    cout << "=========================================\n";
    cout << "  ICCAD Contest Problem B Parser\n";
    cout << "  Multi-bit Flip-Flop Optimization (Firefly)\n";
    cout << "=========================================\n";
}

static ContestArgs parseContestArgs(int argc, char* argv[]) {
    ContestArgs args;
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "-weight") {
            while (i + 1 < argc && argv[i + 1][0] != '-') args.weightFiles.push_back(argv[++i]);
        }
        else if (arg == "-lib") {
            while (i + 1 < argc && argv[i + 1][0] != '-') args.libFiles.push_back(argv[++i]);
        }
        else if (arg == "-lef") {
            while (i + 1 < argc && argv[i + 1][0] != '-') args.lefFiles.push_back(argv[++i]);
        }
        else if (arg == "-db") {
            while (i + 1 < argc && argv[i + 1][0] != '-') args.dbFiles.push_back(argv[++i]);
        }
        else if (arg == "-def") {
            while (i + 1 < argc && argv[i + 1][0] != '-') args.defFiles.push_back(argv[++i]);
        }
        else if (arg == "-v") {
            while (i + 1 < argc && argv[i + 1][0] != '-') args.verilogFiles.push_back(argv[++i]);
        }
        else if (arg == "-sdc") {
            while (i + 1 < argc && argv[i + 1][0] != '-') args.sdcFiles.push_back(argv[++i]);
        }
        else if (arg == "-tf") {
            while (i + 1 < argc && argv[i + 1][0] != '-') args.tfFiles.push_back(argv[++i]);
        }
        else if (arg == "-out") {
            if (i + 1 < argc) args.outputName = argv[++i];
        }
        else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            exit(0);
        }
    }
    return args;
}

// =========================
// Contest workflow (same logic as your original)
// =========================
static bool executeContestWorkflow(Parser& parser, const ContestArgs& args) {
    cout << "\n=== Executing Contest Workflow ===\n";

    // STEP 1: Parse .lib to identify FF cells
    cout << "\n=== STEP 1: Parse .lib Files to Identify FF Cells ===\n";
    if (!args.libFiles.empty()) {
        if (!parser.parseAllLibraries(args.libFiles)) {
            cerr << "Error: Failed to parse .lib files\n"; return false;
        }
        set<string> ffCellList = parser.getLibParser()->getFFCellList();
        cout << "  Found " << ffCellList.size() << " FF cells in libraries\n";
        int count = 0; for (const auto& cell : ffCellList) if (count++ < 10) cout << "    - " << cell << "\n";
        if (ffCellList.size() > 10) cout << "    ... and " << (ffCellList.size() - 10) << " more\n";
    }

    // STEP 2: Weight
    cout << "\n=== STEP 2: Parse Weight File ===\n";
    if (args.weightFiles.empty()) { cerr << "Error: No weight file specified\n"; return false; }
    if (!parser.parseWeights(args.weightFiles[0])) { cerr << "Error: Failed to parse weight file\n"; return false; }

    // STEP 3: LEF
    if (!args.lefFiles.empty()) {
        cout << "\n=== STEP 3: Parse .lef Files ===\n";
        for (const string& lefFile : args.lefFiles) if (!parser.parseLEF(lefFile)) cerr << "Warning: Failed to parse LEF file: " << lefFile << "\n";
    }

    // STEP 4: DEF + Verilog
    cout << "\n=== STEP 4: Parse Design Files ===\n";
    if (!args.defFiles.empty()) {
        cout << "  Parsing DEF file...\n";
        if (!parser.parseDEF(args.defFiles[0])) { cerr << "Error: Failed to parse DEF file\n"; return false; }
        parser.identifyFFInstances();
    }

    if (!args.verilogFiles.empty()) {
        cout << "  Parsing Verilog file...\n";
        if (!parser.parseVerilog(args.verilogFiles[0])) {
            cerr << "ERROR: Failed to parse Verilog file!\n";
            cerr << "Continue without Verilog? (y/n): ";
            char response; cin >> response; if (response != 'y' && response != 'Y') return false;
        }
    }

    // STEP 5: FF grouped by type
    cout << "\n=== STEP 5: Group FF Instances by Cell Type ===\n";
    parser.groupFFInstancesByType();

    return true;
}

int main(int argc, char* argv[]) {
    auto startTime = high_resolution_clock::now();
    printBanner();

    ContestArgs args = parseContestArgs(argc, argv);
    if (args.weightFiles.empty() || args.outputName.empty()) {
        cerr << "Error: Missing required arguments\n"; printUsage(argv[0]); return 1;
    }

    // Echo inputs
    cout << "\nParsed command line:\n";
    cout << "  Weight files: " << args.weightFiles.size() << "\n";
    cout << "  Library files: " << args.libFiles.size() << "\n";
    cout << "  LEF files: " << args.lefFiles.size() << "\n";
    cout << "  DEF files: " << args.defFiles.size() << "\n";
    cout << "  Verilog files: " << args.verilogFiles.size() << "\n";
    cout << "  SDC files: " << args.sdcFiles.size() << "\n";
    cout << "  Tech files: " << args.tfFiles.size() << "\n";
    cout << "  Output name: " << args.outputName << "\n";

    try {
        Parser parser("", args.outputName);
        if (!args.verilogFiles.empty()) parser.setInputVerilogFile(args.verilogFiles[0]);
        if (!executeContestWorkflow(parser, args)) { cerr << "Error: Contest workflow failed\n"; return 1; }

        if (parser.isDefLoaded()) {
            cout << "\n=== Analysis Phase ===\n";
            const auto& ffGroups = parser.getFFGroupsByType();
            cout << "\nFF Banking Opportunities:\n";
            for (auto ff_it = ffGroups.begin(); ff_it != ffGroups.end(); ++ff_it) {
                const std::string& cellType = ff_it->first; const std::vector<FlipFlopInfo>& instances = ff_it->second;
                const LibCell* libCell = parser.getLibParser()->getCell(cellType);
                if (libCell && instances.size() >= 2) {
                    if (cellType.find("2_") == std::string::npos && cellType.find("4_") == std::string::npos) {
                        cout << "\n  Cell type: " << cellType << "\n";
                        cout << "    Instances: " << instances.size() << "\n";
                        std::string degenerate = libCell->singleBitDegenerate; if (!degenerate.empty()) cout << "    Can degenerate from: " << degenerate << "\n";
                    }
                }
            }
        }

        // =============================
        // Hierarchical Clustering phase
        // =============================
        cout << "\n=== Hierarchical Clustering Phase ===\n";
        parser.performHierarchicalClustering();

        // =============================
        // Firefly Preprocess (hierarchy-restricted) & Run
        // =============================
        const HierarchicalClustering* clustering = parser.getHierarchicalClustering();
        const std::map<std::string, std::vector<FlipFlopInfo>>& clockDomains = clustering->getClockDomains();
        DefData defData = parser.getDefParser()->getDefData();
        LefData lefData = parser.getLefParser()->getData();

        FireflyPreprocess prep(clockDomains, defData, lefData, bankingCompatibleTable);

        auto groups = prep.buildGroupsByClockAndHierarchy();
        auto seeds = prep.buildSeedCandidates(groups, /*enable4Bit=*/true);

        Weights w = parser.getWeights();
        FireflyEngine engine(w, /*tnsWeightSpatial=*/1.0);
        cout << "here2" << endl;
        engine.setCompatTables(&bankingCompatibleTable, &debankingCompatibleTable);
        cout << "here3" << endl;
        engine.setSeeds(seeds);
        cout << "here4" << endl;
        auto best = engine.run(defData, lefData, /*maxIter=*/40, /*popSize=*/12, /*rngSeed=*/13);
        cout << "here5" << endl;

        // =============================
        // Materialize placement and update DEF state (mirror your Alpha logic)
        // =============================
        auto cleanComponents = engine.generatePlacementComponents(defData, lefData);
        auto cleanFFs = engine.generatePlacementFFsNew(defData, lefData);

        DefData& defDataNEW = parser.getDefParser()->getDefData();
        defDataNEW.components.clear();
        for (const auto& comp : cleanComponents) {
            ComponentInfo info;
            info.name = !comp.instanceName.empty() ? comp.instanceName : comp.instanceName;
            info.cellType = comp.cellType;
            info.x = comp.x;
            info.y = comp.y;
            info.orient = !comp.orientation.empty() ? comp.orientation : comp.orientation;
            info.isFF = comp.isFF;
            info.isMergedFF = comp.isMergedFF;
            info.rowName = "";
            info.status = "";
            defDataNEW.components.push_back(info);
        }

        MergeMapping map;
        for (const auto& m : engine.mergedFFResults()) {
            for (const auto& s : m.mergedFFs) map.addMapping(s, m.newInstanceName);
        }


        map.printMappings();
        cout << "\n=== Updating Parser State with Firefly Results ===\n";

        // Legalize (your existing API)
        parser.performLegalization();
        DefData& defDataFin = parser.getDefParser()->getDefData();

        // =============================
        // Write outputs (same as your original pipeline)
        // =============================
        WriteOutput writer(args.outputName,
            map,                  // 合併映射
            defDataFin,           // 最終 DEF 資料
            engine.mergedFFResults(), // 合併結果
            parser.getVerilogParser()); // Verilog Parser (may be nullptr)
        writer.setLibParser(parser.getLibParser());

        cout << "\n--- Generating mapping list ---\n";
        if (!writer.writeMapList()) { cerr << "Error: Failed to generate mapping list\n"; return 1; }
        cout << "✓ Generated " << args.outputName << ".list\n";

        cout << "\n--- Generating Verilog netlist ---\n";
        if (!writer.writeVerilog()) { cerr << "Error: Failed to generate Verilog netlist\n"; return 1; }
        cout << "✓ Generated " << args.outputName << ".v\n";

        if (!writer.writeDef()) { cerr << "Error: Failed to generate DEF file\n"; return 1; }
        cout << "✓ Generated " << args.outputName << ".def\n";

        cout << "\n--- Verifying output files ---\n";
        ifstream checkList(args.outputName + ".list");
        ifstream checkVerilog(args.outputName + ".v");
        ifstream checkDef(args.outputName + ".def");
        if (checkList.good() && checkVerilog.good() && checkDef.good()) {
            cout << "✓ All output files generated successfully!\n";
            checkList.seekg(0, ios::end); checkVerilog.seekg(0, ios::end); checkDef.seekg(0, ios::end);
            cout << "\nOutput file sizes:\n";
            cout << "  " << args.outputName << ".list: " << checkList.tellg() << " bytes\n";
            cout << "  " << args.outputName << ".v: " << checkVerilog.tellg() << " bytes\n";
            cout << "  " << args.outputName << ".def: " << checkDef.tellg() << " bytes\n";
        }
        else {
            cerr << "✗ Some output files are missing or corrupted!\n"; return 1;
        }
        checkList.close(); checkVerilog.close(); checkDef.close();

        // (Optional) If you want a CK-cap report, you can add a Firefly dumper here.
        // dpc.dumpCkCapReport(...) was removed since DPC is not used.

        auto endTime = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(endTime - startTime);
        cout << "\n=== Execution Summary ===\n";
        parser.printSummary();
        cout << "Execution time: " << duration.count() << " ms\n";
        cout << "\n=== Processing completed successfully! ===\n";
        return 0;
    }
    catch (const exception& e) {
        cerr << "Fatal error: " << e.what() << "\n"; return 1;
    }
}
