#ifndef LIB_PARSER_H
#define LIB_PARSER_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>

// .lib 檔案中的 pin 資?
struct LibPin {
    std::string name;
    std::string direction;  // input, output, inout
    std::string function;   // Boolean function
    double capacitance = 0.0;
    std::map<std::string, std::string> attributes;
};

// .lib 檔案中的完整 cell 資?
struct LibCell {
    std::string name;
    double area = 0.0;
    double cellLeakagePower = 0.0;
    std::string singleBitDegenerate;  // ??關鍵屬性！
    std::map<std::string, LibPin> pins;
    std::map<std::string, std::string> attributes;

    // FF 相關屬性
    std::string ffType;
    int bitWidth = 1;
    bool isScannable = false;
};

class LibParser {
private:
    std::map<std::string, LibCell> cellLibrary_;
    std::set<std::string> parsedCells_;
    bool isLoaded_ = false;

    // Helper methods
    bool parseCell(std::ifstream& file, const std::string& cellName, LibCell& cell);
    bool parsePin(std::ifstream& file, const std::string& pinName, LibPin& pin);
    std::string extractQuotedString(const std::string& line);
    double extractNumericValue(const std::string& line);
    void skipToEndOfBlock(std::ifstream& file, int depth = 1);

public:
    LibParser() = default;
    ~LibParser() = default;

    // ??專用：使用元件列表解析
    bool parseWithCellList(const std::vector<std::string>& libFiles,
        const std::vector<std::string>& initialCellList,
        std::set<std::string>& finalCellList);

    // 一般解析（測?用）
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

    // 工具方法
    void clear();
    void printSummary() const;
    void printCellDetails(const std::string& cellName) const;
    bool isLoaded() const { return isLoaded_; }
};

#endif // LIB_PARSER_H#pragma once
