#ifndef PARSER_WEIGHTS_H
#define PARSER_WEIGHTS_H

#include "DataStructures.h"
#include <string>

class WeightParser {
private:
    Weights weights_;
    bool isLoaded_;
    std::vector<std::string> cellList_;  // 新增：儲存初始元件列表
public:
    // Constructor & Destructor
    WeightParser();
    ~WeightParser() = default;
    const Weights& getWeights() const { return weights_; }
    // NEW: 琌更
    bool isLoaded() const { return isLoaded_; }
    // NEW: ﹍ cell 睲虫璝Τノ
    const std::vector<std::string>& getCellList() const { return cellList_; }

    // Main interface methods
    bool parseFile(const std::string& filename);
    bool parseFromString(const std::string& content);

    Weights& getWeights() { return weights_; }

    // Individual weight access
    double getAlpha() const { return weights_.Alpha; }
    double getBeta() const { return weights_.Beta; }
    double getGamma() const { return weights_.Gamma; }
    double getTNS() const { return weights_.TNS; }
    double getTPO() const { return weights_.TPO; }
    double getArea() const { return weights_.Area; }

    // Weight setting methods
    void setAlpha(double value) { weights_.Alpha = value; }
    void setBeta(double value) { weights_.Beta = value; }
    void setGamma(double value) { weights_.Gamma = value; }
    void setTNS(double value) { weights_.TNS = value; }
    void setTPO(double value) { weights_.TPO = value; }
    void setArea(double value) { weights_.Area = value; }

    // Utility methods
    void clear();
    void printWeights() const;
    bool validateWeights() const;

    // Export methods
    bool saveToFile(const std::string& filename) const;
    std::string toString() const;
};

#endif // PARSER_WEIGHTS_H