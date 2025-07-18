#ifndef LIB_PARSER_H
#define LIB_PARSER_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>

// .lib 中的 pin 資訊
struct LibPin {
    std::string name;
    std::string direction;  // input, output, inout
    std::string function;   // Boolean function
    double capacitance = 0.0;
    std::map<std::string, std::string> attributes;
};

// .lib 中的完整 cell 資訊
struct LibCell {
    std::string name;
    std::string libraryName;  // 新增：所屬的 library 名稱
    double area = 0.0;
    double cellLeakagePower = 0.0;
    std::string singleBitDegenerate;  // 單位元退化資訊
    std::map<std::string, LibPin> pins;
    std::map<std::string, std::string> attributes;

    // FF 相關資訊
    std::string ffType;
    int bitWidth = 1;
    bool isScannable = false;
    bool hasFF = false;  // 新增：標記是否包含 ff() block
};

// Library 資訊
struct LibraryInfo {
    std::string name;
    std::string filename;
    std::set<std::string> cells;
};

class LibParser {
private:
    std::map<std::string, LibCell> cellLibrary_;
    std::set<std::string> parsedCells_;
    std::map<std::string, LibraryInfo> libraries_;  // 新增：library 資訊
    bool isLoaded_ = false;

    // Helper methods
    bool parseCell(std::ifstream& file, const std::string& cellName, LibCell& cell, const std::string& libraryName);
    bool parseCellForFF(std::ifstream& file, const std::string& cellName, LibCell& cell, const std::string& libraryName);
    bool parsePin(std::ifstream& file, const std::string& pinName, LibPin& pin);
    bool parseFF(std::ifstream& file, LibCell& cell);
    std::string extractQuotedString(const std::string& line);
    double extractNumericValue(const std::string& line);
    void skipToEndOfBlock(std::ifstream& file, int depth = 1);
    std::string extractLibraryName(const std::string& line);
    // Parse a cell specifically looking for FF characteristics





public:
    LibParser() = default;
    ~LibParser() = default;

    // 新的主要解析介面：先解析所有 FF cells
    bool parseAllLibraries(const std::vector<std::string>& libFiles);

    // 取得所有 FF cell names (包含有 single_bit_degenerate 或 ff() 的)
    std::set<std::string> getFFCellList() const;

    // 競賽用：使用初始列表解析
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

    // 查詢方法
    std::vector<std::string> getFlipFlopCells() const;
    std::vector<std::string> getMultiBitCells() const;
    std::string getSingleBitDegenerate(const std::string& cellName) const;
    const std::map<std::string, LibraryInfo>& getLibraries() const { return libraries_; }

    // 工具方法
    void clear();
    void printSummary() const;
    void printCellDetails(const std::string& cellName) const;
    void printFFCellList() const;
    bool isLoaded() const { return isLoaded_; }
    // New method for parsing all libraries to find FF cells





};

#endif // LIB_PARSER_H