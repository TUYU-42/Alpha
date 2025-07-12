#ifndef PLACER_H
#define PLACER_H

#include "DataStructures.h"
#include "ParserDEF.h"
#include <unordered_map>
#include <vector>
#include <string>

struct PlacementInfo {
    std::string instanceName;    // DEF instance name
    std::string cellType;        // DEF cellType (= macro name)
    int rowIdx = -1;             // 預設 -1 表未放置
    double x = 0.0;
    double y = 0.0;
    int siteBegin = 0;
    int sitesUsed = 0;
    std::string orient;
    bool islegal = false;
};

class Placer {
public:
    // 建構式：直接餵 macro map（type 資訊）和 DEF instance list
    Placer(const std::unordered_map<std::string, LefMacroInfo>& macroMap,
        const std::vector<ComponentInfo>& components);

    // 查 instance 名稱對應的 macro 資訊（回傳 LefMacroInfo 指標）
    const LefMacroInfo* getMacroInfoByInstance(const std::string& instanceName) const;

    // （可選）查 instance 對應 ComponentInfo
    const ComponentInfo* getComponent(const std::string& instanceName) const;

    // （可選）查詢所有 component name
    std::vector<std::string> getAllInstanceNames() const;

    void printSomeMappings(int n) const;

private:
    // 型別描述：macro name → LefMacroInfo
    const std::unordered_map<std::string, LefMacroInfo>& macroMap_;
    // instanceName → ComponentInfo（for O(1) 查詢）
    std::unordered_map<std::string, ComponentInfo> instanceMap_;
    // 可加 placement result、row info 等資料
};

#endif // PLACER_H
