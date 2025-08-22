// ParserVerilog.h - эセ
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <map>
#include <set>
#include <functional>
#include"LibParser.h"
#include "DataStructures.h"

// === 穝糤籔 WriteOutput 甧挡篶 ===
struct PinConnection {
    std::string pin;  // .PIN
    std::string net;  // ( NET ) や穿 escaped: \foo[3]
};

struct VerilogInstance {
    std::string cellType;
    std::string instName;
    bool isModuleInstance = false;
    std::string referencedModule;

    // セ parser ノ㏑
    std::vector<PinConnection> pinConnections;

    //  WriteOutput 甧ウノ inst.connections
    std::vector<std::pair<std::string, std::string>> connections;

    std::string hierarchicalPath; // 匡
};

struct VerilogModule {
    std::string name;
    std::vector<std::string> ports;              // header raw
    std::vector<std::string> inputs;             // 穝糤
    std::vector<std::string> outputs;            // 穝糤
    std::vector<std::string> wires;              // 穝糤
    std::vector<std::string> assignStatements;   // 穝糤妓玂痙 "assign a=b;"
    std::vector<std::string> supplies0; // names declared by 'supply0'
    std::vector<std::string> supplies1; // names declared by 'supply1'
    std::vector<VerilogInstance> instances;
    std::vector<std::string> inouts;
    std::unordered_map<std::string, std::string> portDeclWidth;
};

class VerilogParser {
public:
    VerilogParser();


    // 新增：設置（可選）LibParser 指標（目前不?依?）

    // High-level APIs
    bool parseFile(const std::string& filepath);
    bool parseFromString(const std::string& text);
    void setLibParser(const LibParser* p) { lib_ = p; }
    // 侣ご玂痙
    const std::unordered_map<std::string, std::unique_ptr<VerilogModule>>& modules() const { return moduleMap_; }
    const std::string& topModuleName() const { return topModule_; }

    // === 穝糤WriteOutput 惠璶 API ===
    // 絬┦ modules 浪跌钡 for(auto&m:getModules())
    const std::vector<VerilogModule>& getModules() const { return modulesLinear_; }
    // 新增：外部可取 inst-pin-net 映射
    const std::vector<InstPinNet>& getInstPinNets() const { return instPinNets_; }
    const std::vector<VerilogInstance>& getInstances() const { return flatInstances_; }

    // 顶糷戈癟
    struct HierarchyNode {
        std::string instanceName;
        std::string moduleName;
        std::string fullPath;
        std::weak_ptr<HierarchyNode> parent;
        std::vector<std::shared_ptr<HierarchyNode>> children;
    };
    std::shared_ptr<HierarchyNode> buildHierarchy(const std::string& topModule);

    // ㄑ WriteOutput ㄏノ琈甮
    const std::unordered_map<std::string, std::string>& getHierarchicalNameMapping() const { return localToFull_; }

    // 碝т instanceby 虏 or by full path
    const VerilogInstance* findInstance(const std::string& name) const;
    const VerilogInstance* findInstanceByHierarchicalPath(const std::string& fullPath) const;
    static void skipSpaces(const std::string& s, size_t& i);
    // 砞﹚ top 嘿
    void setModuleNameHint(const std::string& top) { topModule_ = top; }
    static std::string readIdentifier(const std::string& s, size_t& i);
    static std::string readEscapedIdentifier(const std::string& s, size_t& i);
    static bool isIdentifierStart(char c);
    void analyzeHierarchy();        // ?出?單的統?
    void findClockNets();           // 掃 CK/CLK/CP 等 pin 的 net
    void printScanChainSummary();   // 列出 FF 與其 SI/SO 連?概況

private:
    // ===== 琂Τ helper玂痙 =====
    static std::string removeComments(const std::string& s);
    std::unordered_map<std::string, std::string> localToFull_;
    static bool isIdentifierBody(char c);
    const LibParser* lib_ = nullptr;
    std::vector<InstPinNet> instPinNets_;
    struct ModuleSpan { size_t begin; size_t end; };
    bool splitModules(const std::string& text, std::vector<std::pair<std::string, ModuleSpan>>& out);
    bool parseModuleHeader(const std::string& text, size_t modBegin, size_t& afterHeader, std::string& modName, std::vector<std::string>& ports);

    bool parseInstancesInModule(const std::string& text, size_t bodyBegin, size_t bodyEnd, VerilogModule& out);
    bool extractNextInstanceBlock(const std::string& s, size_t bodyEnd, size_t& pos, std::string& cellType, std::string& instName, std::string& argBlock);
    void parseInstanceConnections(const std::string& args, std::vector<PinConnection>& outPins);

    // === 穝糤籔 assign ┾ ===
    void parseDeclarationsAndAssigns(const std::string& text, size_t bodyBegin, size_t bodyEnd, VerilogModule& out);

    // Hierarchy
    std::shared_ptr<HierarchyNode> buildHierarchyRecursive(const std::string& moduleName, const std::string& parentPath, std::shared_ptr<HierarchyNode> parent);

    bool isKnownModule(const std::string& name) const { return moduleMap_.find(name) != moduleMap_.end(); }

    // === 穝糤俱瞶 linear 籔琩 ===
    void rebuildLinearViewsAndLookups();
    void rebuildInstPinNets();
    static bool isClockPinName(const std::string& pin);
    bool isLikelyFlipFlop(const std::string& cellType) const;
    static std::string toUpper(std::string s);
private:
    std::unordered_map<std::string, std::unique_ptr<VerilogModule>> moduleMap_;
    std::string topModule_;

    // 穝糤linear/キ籔ま
    std::vector<VerilogModule> modulesLinear_;
    std::vector<VerilogInstance> flatInstances_;
    std::unordered_map<std::string, const VerilogInstance*> nameToInst_;      // instName -> ptr
    std::unordered_map<std::string, const VerilogInstance*> fullToInst_;      // fullPath -> ptr
    // local -> full path
};
