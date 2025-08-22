#ifndef LIB_PARSER_H
#define LIB_PARSER_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>



// 增?的 LibPin 結?，加入 signal_type
struct LibPin {
    std::string name;
    std::string direction;  // input, output, inout
    std::string function;   // Boolean function
    std::string signalType; // test_scan_in, test_scan_out, data, clock, etc.
    double capacitance = 0.0;
    double maxCapacitance = 0.0;
    double minCapacitance = 0.0;
    double maxTransition = 0.0;
    std::map<std::string, std::string> attributes;
    bool isClock = false;   // <--- 新增
};

// 新增 Bundle 結?
struct LibBundle {
    std::string name;
    std::vector<std::string> members;
    std::string direction;
    std::string signalType;
    std::map<std::string, std::string> attributes;
};

// 增?的 LibCell 結?
struct LibCell {
    std::string name;
    std::string libraryName;
    double area = 0.0;
    double cellLeakagePower = 0.0;
    std::string singleBitDegenerate;

    std::map<std::string, LibPin> pins;
    std::map<std::string, LibBundle> bundles;  // 新增 bundle 支援
    std::map<std::string, std::string> attributes;

    // FF 相關資?
    std::string ffType;
    int bitWidth = 1;
    bool isScannable = false;
    bool hasFF = false;

    // 輔助方法
    bool hasBundle(const std::string& bundleName) const {
        return bundles.find(bundleName) != bundles.end();
    }

    std::vector<std::string> getBundleMembers(const std::string& bundleName) const {
        auto it = bundles.find(bundleName);
        if (it != bundles.end()) {
            return it->second.members;
        }
        return std::vector<std::string>();
    }
};

// Library 資?
struct LibraryInfo {
    std::string name;
    std::string filename;
    std::set<std::string> cells;
};

class LibParser {
private:
    std::map<std::string, LibCell> cellLibrary_;
    std::set<std::string> parsedCells_;
    std::map<std::string, LibraryInfo> libraries_;
    bool isLoaded_ = false;

    // Helper methods
    bool parseCell(std::ifstream& file, const std::string& cellName, LibCell& cell, const std::string& libraryName);
    bool parseCellForFF(std::ifstream& file, const std::string& cellName, LibCell& cell, const std::string& libraryName);
    bool parsePin(std::ifstream& file, const std::string& pinName, LibPin& pin);
    bool parseBundle(std::ifstream& file, const std::string& bundleName, LibBundle& bundle);
    bool parseFF(std::ifstream& file, LibCell& cell);
    std::string extractQuotedString(const std::string& line);
    double extractNumericValue(const std::string& line);
    void skipToEndOfBlock(std::ifstream& file, int depth = 1);
    std::string extractLibraryName(const std::string& line);

    // Multi-bit FF helper methods
    std::string findMultibitVariant(const std::string& cellName, int bitWidth) const;
    bool isMultibitFF(const std::string& cellName, int bitWidth) const;
    bool isCompatibleFF(const std::string& cell1, const std::string& cell2) const;
    std::string extractBaseFFType(const std::string& cellName) const;
    std::string generateMultibitName(const std::string& baseType, int bitWidth) const;

    // 新增：從 bundles 更新 bitWidth
    void updateCellBitWidthFromBundles(LibCell& cell) const;

public:
    LibParser() = default;
    ~LibParser() = default;
    double getClockPinCap(const std::string& cellName) const;
    std::string getClockPinName(const std::string& cellName) const; // 方便除錯/日後用
    // 新的主要函數：解析所有 FF cells
    bool parseAllLibraries(const std::vector<std::string>& libFiles);

    // 取得所有 FF cell names (包含有 single_bit_degenerate 或 ff() 的)
    std::set<std::string> getFFCellList() const;

    // ??用：根據初始列表解析
    bool parseWithCellList(const std::vector<std::string>& libFiles,
        const std::vector<std::string>& initialCellList,
        std::set<std::string>& finalCellList);

    // 一般解析
    bool parseFile(const std::string& filename);
    bool parseFiles(const std::vector<std::string>& filenames);

    // 資料存取
    const LibCell* getCell(const std::string& cellName) const;
    const std::map<std::string, LibCell>& getAllCells() const { return cellLibrary_; }
    bool hasCell(const std::string& cellName) const;

    // 查?方法
    std::vector<std::string> getFlipFlopCells() const;
    std::vector<std::string> getMultiBitCells() const;
    std::string getSingleBitDegenerate(const std::string& cellName) const;

    // 多位元 FF 查?方法
    std::string getmultibitff2(const std::string& cellName) const;
    std::string getmultibitff4(const std::string& cellName) const;

    const std::map<std::string, LibraryInfo>& getLibraries() const { return libraries_; }

    // 新增：取得 cell 的 scan pins
    std::vector<std::string> getCellScanPins(const std::string& cellName) const;

    // 新增：判斷 pin 是否為 scan pin
    bool isScanPin(const std::string& cellName, const std::string& pinName) const;

    void clear();
    void printSummary() const;
    void printCellDetails(const std::string& cellName) const;
    void printFFCellList() const;
    bool isLoaded() const { return isLoaded_; }

    int extractBitWidth(const std::string& cellName) const;
    void updateCellBitWidth(LibCell& cell) const;
    int getCellBitWidth(const std::string& cellName) const;
};

#endif // LIB_PARSER_H