#include "ParserVerilog.h"
#include <fstream>
#include <sstream>
#include <cctype>
#include <iostream>
static std::string readBracketSuffixes(const std::string& s, size_t& i) {
    std::string suf;
    size_t n = s.size();
    for (;;) {
        // 允許中間有空白
        while (i < n && std::isspace((unsigned char)s[i])) ++i;
        if (i >= n || s[i] != '[') break;

        int depth = 0;
        do {
            char c = s[i++];
            suf.push_back(c);
            if (c == '[') ++depth;
            else if (c == ']') --depth;
        } while (i < n && depth > 0);
    }
    return suf;
}
VerilogParser::VerilogParser() = default;
static inline std::string normalizeId(const std::string& s) {
    if (!s.empty() && s.front() == '\\' && !s.empty() && s.back() == ' ')
        return s.substr(1, s.size() - 2);
    return s;
}

// NEW: 讀 packed range，例如 [99:0]（允許內部空白）
static std::string readPackedRange(const std::string& s, size_t& i, size_t end) {
    VerilogParser::skipSpaces(s, i);
    if (i >= end || s[i] != '[') return {};
    size_t j = i, depth = 0;
    std::string r;
    while (j < end) {
        char c = s[j++];
        r.push_back(c);
        if (c == '[') ++depth;
        else if (c == ']') { if (--depth == 0) break; }
    }
    i = j;
    return r; // 直接保留原樣，如 "[99:0]"
}
static std::string readFileAll(const std::string& path) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs) return {};
    std::ostringstream oss; oss << ifs.rdbuf();
    return oss.str();
}
static void collectDeclList(const std::string& s, size_t& i, size_t end,
    std::vector<std::string>& outIds);
bool VerilogParser::parseFile(const std::string& filepath) {
    std::string raw = readFileAll(filepath);
    if (raw.empty()) return false;
    return parseFromString(raw);
}
void VerilogParser::parseDeclarationsAndAssigns(const std::string& text, size_t bodyBegin, size_t bodyEnd, VerilogModule& out) {
    size_t i = bodyBegin;
    while (i < bodyEnd) {
        skipSpaces(text, i);
        if (i >= bodyEnd) break;

        auto startsWith = [&](const char* kw) {
            size_t k = 0, j = i;
            while (kw[k]) { if (j >= bodyEnd || text[j] != kw[k]) return false; ++k; ++j; }
            if (j < bodyEnd && isIdentifierBody(text[j])) return false;
            return true;
            };

        if (startsWith("input")) {
            i += 5; // skip 'input'
            // NEW: 跳過可能出現的型別/修飾詞
            skipSpaces(text, i);
            while (i < bodyEnd) {
                // 支援 wire/logic/reg/signed/unsigned
                size_t tmp = i;
                std::string kw;
                if (isIdentifierStart(text[tmp])) kw = readIdentifier(text, tmp);
                if (kw == "wire" || kw == "logic" || kw == "reg" ||
                    kw == "signed" || kw == "unsigned") {
                    i = tmp;
                    skipSpaces(text, i);
                }
                else break;
            }
            // NEW: 讀可選的 [msb:lsb]
            std::string range = readPackedRange(text, i, bodyEnd);
            skipSpaces(text, i);

            // 記下本次 collect 前的大小
            size_t base = out.inputs.size();
            collectDeclList(text, i, bodyEnd, out.inputs);

            // NEW: 把這行所有識別字都套上剛剛的 range
            if (!range.empty()) {
                for (size_t k = base; k < out.inputs.size(); ++k) {
                    out.portDeclWidth[normalizeId(out.inputs[k])] = range;
                }
            }
            continue;
        }

        if (startsWith("output")) {
            i += 6; // skip 'input' 
            // NEW: 跳過可能出現的型別/修飾詞
            skipSpaces(text, i);
            while (i < bodyEnd) {
                // 支援 wire/logic/reg/signed/unsigned
                size_t tmp = i;
                std::string kw;
                if (isIdentifierStart(text[tmp])) kw = readIdentifier(text, tmp);
                if (kw == "wire" || kw == "logic" || kw == "reg" ||
                    kw == "signed" || kw == "unsigned") {
                    i = tmp;
                    skipSpaces(text, i);
                }
                else break;
            }
            // NEW: 讀可選的 [msb:lsb]
            std::string range = readPackedRange(text, i, bodyEnd);
            skipSpaces(text, i);

            // 記下本次 collect 前的大小
            size_t base = out.outputs.size();
            collectDeclList(text, i, bodyEnd, out.outputs);

            // NEW: 把這行所有識別字都套上剛剛的 range
            if (!range.empty()) {
                for (size_t k = base; k < out.outputs.size(); ++k) {
                    out.portDeclWidth[normalizeId(out.outputs[k])] = range;
                }
            }
            continue;
        }
        if (startsWith("inout")) {
            i += 5; // skip 'input' 
            // NEW: 跳過可能出現的型別/修飾詞
            skipSpaces(text, i);
            while (i < bodyEnd) {
                // 支援 wire/logic/reg/signed/unsigned
                size_t tmp = i;
                std::string kw;
                if (isIdentifierStart(text[tmp])) kw = readIdentifier(text, tmp);
                if (kw == "wire" || kw == "logic" || kw == "reg" ||
                    kw == "signed" || kw == "unsigned") {
                    i = tmp;
                    skipSpaces(text, i);
                }
                else break;
            }
            // NEW: 讀可選的 [msb:lsb]
            std::string range = readPackedRange(text, i, bodyEnd);
            skipSpaces(text, i);

            // 記下本次 collect 前的大小
            size_t base = out.inouts.size();
            collectDeclList(text, i, bodyEnd, out.inouts);

            // NEW: 把這行所有識別字都套上剛剛的 range
            if (!range.empty()) {
                for (size_t k = base; k < out.inouts.size(); ++k) {
                    out.portDeclWidth[normalizeId(out.inouts[k])] = range;
                }
            }
            continue;
        }

        if (startsWith("wire") || startsWith("logic") || startsWith("reg")) {
            size_t kwlen = startsWith("wire") ? 4 : (startsWith("logic") ? 5 : 3);
            i += kwlen;

            // 讀可選的 packed range，如 [599:0] 或 [0:0]
            skipSpaces(text, i);
            std::string range = readPackedRange(text, i, bodyEnd);
            skipSpaces(text, i);

            // 記下這行宣告的識別字列表
            size_t base = out.wires.size();
            collectDeclList(text, i, bodyEnd, out.wires);

            // 把寬度記起來（跟你 portDeclWidth 一樣的做法）
            if (!range.empty()) {
                for (size_t k = base; k < out.wires.size(); ++k) {
                    out.wireDeclWidth[normalizeId(out.wires[k])] = range;
                }
            }
            continue;
        }
        if (startsWith("assign")) {
            // 擷取直到 ';'
            size_t j = i;
            while (j < bodyEnd && text[j] != ';') ++j;
            if (j < bodyEnd) ++j;
            out.assignStatements.emplace_back(std::string(text.begin() + i, text.begin() + j));
            i = j;
            continue;
        }
        if (startsWith("supply0")) {
            i += 7; // skip 'supply0'
            std::vector<std::string> tmp;
            collectDeclList(text, i, bodyEnd, tmp);
            out.supplies0.insert(out.supplies0.end(), tmp.begin(), tmp.end());
            continue;
        }
        if (startsWith("supply1")) {
            i += 7; // skip 'supply1'
            std::vector<std::string> tmp;
            collectDeclList(text, i, bodyEnd, tmp);
            out.supplies1.insert(out.supplies1.end(), tmp.begin(), tmp.end());
            continue;
        }

        // 其它東西跳到下一個 ';' 或換行（讓 instance parser 處理）
        if (text[i] == ';') { ++i; continue; }
        ++i;
    }
}
static void collectDeclList(const std::string& s, size_t& i, size_t end, std::vector<std::string>& outIds) {
    // 讀到 ';' 為止，支援多個 token 以 ',' 分隔；每個 token 是 identifier 或 escaped id
    while (i < end) {
        VerilogParser::skipSpaces(s, i);
        if (i >= end) break;
        if (s[i] == ';') { ++i; break; }

        std::string tok;
        if (s[i] == '\\') tok = VerilogParser::readEscapedIdentifier(s, i);
        else if (VerilogParser::isIdentifierStart(s[i])) tok = VerilogParser::readIdentifier(s, i);

        if (!tok.empty()) outIds.push_back(tok);

        // 跳過直到逗點或分號
        while (i < end && s[i] != ',' && s[i] != ';') ++i;
        if (i < end && s[i] == ',') { ++i; continue; }
        if (i < end && s[i] == ';') { ++i; break; }
    }
}
// ===== Helpers =====
std::string VerilogParser::removeComments(const std::string& s) {
    std::string out; out.reserve(s.size());
    enum State { Normal, Slash, Line, Block, BlockStar } st = Normal;
    for (char c : s) {
        switch (st) {
        case Normal:
            if (c == '/') st = Slash; else out.push_back(c);
            break;
        case Slash:
            if (c == '/') { st = Line; }
            else if (c == '*') { st = Block; }
            else { out.push_back('/'); out.push_back(c); st = Normal; }
            break;
        case Line:
            if (c == '\n') { out.push_back('\n'); st = Normal; }
            break;
        case Block:
            if (c == '*') st = BlockStar; break;
        case BlockStar:
            if (c == '/') st = Normal; else st = Block;
            break;
        }
    }
    if (st == Slash) out.push_back('/');
    return out;
}

bool VerilogParser::isIdentifierStart(char c) { return std::isalpha((unsigned char)c) || c == '_' || c == '$'; }
bool VerilogParser::isIdentifierBody(char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '$'; }

std::string VerilogParser::readIdentifier(const std::string& s, size_t& i) {
    size_t n = s.size();
    size_t b = i;
    if (i < n && isIdentifierStart(s[i])) { ++i; }
    while (i < n && isIdentifierBody(s[i])) ++i;
    return std::string(s.begin() + b, s.begin() + i);
}

std::string VerilogParser::readEscapedIdentifier(const std::string& s, size_t& i) {
    // Verilog escaped id starts with '\\' and ends at the next whitespace
    size_t n = s.size();
    size_t b = i; // at '\\'
    ++i; // skip backslash
    while (i < n && !std::isspace((unsigned char)s[i]) && s[i] != '(' && s[i] != ')' && s[i] != ',') ++i;
    return std::string(s.begin() + b, s.begin() + i);
}

void VerilogParser::skipSpaces(const std::string& s, size_t& i) {
    size_t n = s.size();
    while (i < n) {
        char c = s[i];
        if (std::isspace((unsigned char)c)) { ++i; continue; }
        // collapse stray line-continuation tokens if any
        if (c == '\r') { ++i; continue; }
        break;
    }
}

bool VerilogParser::splitModules(const std::string& text,
    std::vector<std::pair<std::string, ModuleSpan>>& out) {
    const std::string kw = "module";
    const std::string kwEnd = "endmodule";
    size_t i = 0, n = text.size();
    while (i < n) {
        // Find "module"
        size_t mpos = text.find(kw, i);
        if (mpos == std::string::npos) break;
        // ensure token boundary
        if (mpos > 0 && isIdentifierBody(text[mpos - 1])) { i = mpos + kw.size(); continue; }
        size_t j = mpos + kw.size();
        skipSpaces(text, j);
        // module name (id or escaped id)
        std::string modName;
        if (j < n && text[j] == '\\') modName = readEscapedIdentifier(text, j);
        else modName = readIdentifier(text, j);
        if (modName.empty()) { i = j; continue; }

        // Find the matching "endmodule" after this point (not perfect but robust for netlists)
        size_t endpos = text.find(kwEnd, j);
        if (endpos == std::string::npos) endpos = n;

        out.push_back({ modName, ModuleSpan{mpos, endpos} });
        i = endpos + kwEnd.size();
    }
    return !out.empty();
}

bool VerilogParser::parseModuleHeader(const std::string& text, size_t modBegin, size_t& afterHeader,
    std::string& modName, std::vector<std::string>& ports) {
    size_t i = modBegin;
    const size_t n = text.size();
    // consume 'module'
    size_t mpos = text.find("module", i);
    if (mpos == std::string::npos) return false;
    i = mpos + 6;
    skipSpaces(text, i);

    if (i < n&& text[i] == '\\') modName = readEscapedIdentifier(text, i);
    else modName = readIdentifier(text, i);

    skipSpaces(text, i);
    if (i >= n || text[i] != '(') { afterHeader = i; return true; }

    // Parse port list until ") ;"
    int depth = 0; bool inHeader = true;
    size_t argBeg = ++i;
    depth = 1;
    while (i < n && depth > 0) {
        char c = text[i++];
        if (c == '(') depth++;
        else if (c == ')') depth--;
    }
    size_t argEnd = i > 0 ? i - 1 : i; // position of ')'

    // Consume optional spaces and ';'
    size_t semi = i;
    skipSpaces(text, semi);
    if (semi < n && text[semi] == ';') semi++;

    // Split ports by ',' (keep raw tokens)
    std::string headerArgs = std::string(text.begin() + argBeg, text.begin() + argEnd);
    size_t p = 0, m = headerArgs.size();
    while (p < m) {
        skipSpaces(headerArgs, p);
        if (p >= m) break;
        size_t q = p;
        // read token until comma
        while (q < m && headerArgs[q] != ',') ++q;
        std::string tok = headerArgs.substr(p, q - p);
        // trim
        size_t l = 0; while (l < tok.size() && std::isspace((unsigned char)tok[l])) ++l;
        size_t r = tok.size(); while (r > l && std::isspace((unsigned char)tok[r - 1])) --r;
        if (r > l) ports.push_back(tok.substr(l, r - l));
        p = q + 1;
    }

    afterHeader = semi;
    return true;
}

bool VerilogParser::parseInstancesInModule(const std::string& text, size_t bodyBegin, size_t bodyEnd, VerilogModule& out) {
    size_t pos = bodyBegin;
    while (pos < bodyEnd) {
        skipSpaces(text, pos);
        if (pos >= bodyEnd) break;

        // 快速略過宣告類關鍵字（交給上面那個函式）
        auto startsWith = [&](const char* kw) {
            size_t k = 0; size_t j = pos;
            while (kw[k]) { if (j >= bodyEnd || text[j] != kw[k]) return false; ++k; ++j; }
            if (j < bodyEnd && isIdentifierBody(text[j])) return false;
            return true;
            };
        if (startsWith("wire") || startsWith("input") || startsWith("output") ||
            startsWith("inout") || startsWith("reg") || startsWith("logic") ||
            startsWith("assign") || startsWith("parameter") || startsWith("localparam") ||
            startsWith("genvar") || startsWith("generate") || startsWith("endgenerate") ||
            startsWith("always") || startsWith("initial")) {
            // 跳到 ';'
            while (pos < bodyEnd && text[pos] != ';') ++pos;
            if (pos < bodyEnd && text[pos] == ';') ++pos;
            continue;
        }

        std::string cellType, instName, argBlock;
        size_t save = pos;
        if (!extractNextInstanceBlock(text, bodyEnd, pos, cellType, instName, argBlock)) {
            pos = save + 1;
            continue;
        }

        VerilogInstance inst;
        inst.cellType = cellType;
        inst.instName = instName;
        inst.isModuleInstance = isKnownModule(cellType);
        inst.referencedModule = inst.isModuleInstance ? cellType : std::string();
        parseInstanceConnections(argBlock, inst.pinConnections);

        // 同步 connections（WriteOutput 讀這個）
        inst.connections.reserve(inst.pinConnections.size());
        for (auto& pc : inst.pinConnections) inst.connections.emplace_back(pc.pin, pc.net);

        out.instances.push_back(std::move(inst));
    }
    return true;
}
bool VerilogParser::parseFromString(const std::string& textRaw) {
    std::string text = removeComments(textRaw);

    std::vector<std::pair<std::string, ModuleSpan>> mods;
    if (!splitModules(text, mods)) return false;

    // First pass: create module objects & parse headers
    for (auto& kv : mods) {
        const auto& name = kv.first;
        auto mod = std::make_unique<VerilogModule>();
        mod->name = name;

        size_t afterHeader = kv.second.begin;
        std::vector<std::string> ports;
        std::string parsedName;
        if (!parseModuleHeader(text, kv.second.begin, afterHeader, parsedName, ports)) {
            parsedName = name;
        }
        mod->name = parsedName;
        mod->ports = std::move(ports);

        moduleMap_[mod->name] = std::move(mod);
    }

    // Second pass: parse declarations + instances
    for (auto& kv : mods) {
        const std::string& name = kv.first;
        auto it = moduleMap_.find(name);
        if (it == moduleMap_.end()) continue;
        VerilogModule& M = *it->second;

        size_t afterHeader = kv.second.begin;
        std::string dummyName; std::vector<std::string> dummyPorts;
        parseModuleHeader(text, kv.second.begin, afterHeader, dummyName, dummyPorts);
        size_t bodyBegin = afterHeader;
        size_t bodyEnd = kv.second.end;

        if (bodyBegin < bodyEnd) {
            // 先抽宣告/assign
            parseDeclarationsAndAssigns(text, bodyBegin, bodyEnd, M);
            // 再抓 instances
            parseInstancesInModule(text, bodyBegin, bodyEnd, M);
        }
    }

    // Try to guess top if not set
    if (topModule_.empty() && !moduleMap_.empty()) {
        std::unordered_set<std::string> referenced;
        for (auto& mp : moduleMap_) {
            for (auto& inst : mp.second->instances) {
                if (inst.isModuleInstance) referenced.insert(inst.referencedModule);
            }
        }
        for (auto& mp : moduleMap_) {
            if (!referenced.count(mp.first)) { topModule_ = mp.first; break; }
        }
        if (topModule_.empty()) topModule_ = moduleMap_.begin()->first;
    }

    // 建立 linear/索引
    rebuildLinearViewsAndLookups();

    return true;
}
bool VerilogParser::extractNextInstanceBlock(const std::string& s, size_t bodyEnd, size_t& pos,
    std::string& cellType, std::string& instName, std::string& argBlock) {
    size_t n = s.size();
    size_t i = pos;
    skipSpaces(s, i);

    // Expect cellType
    if (i >= bodyEnd) return false;
    if (s[i] == '\\') cellType = readEscapedIdentifier(s, i);
    else if (isIdentifierStart(s[i])) cellType = readIdentifier(s, i);
    else return false;

    skipSpaces(s, i);

    // Expect instName
    if (i >= bodyEnd) return false;
    if (s[i] == '\\') instName = readEscapedIdentifier(s, i);
    else if (isIdentifierStart(s[i])) instName = readIdentifier(s, i);
    else return false;

    skipSpaces(s, i);
    if (i >= bodyEnd || s[i] != '(') return false;

    // Parse arg block until matching ');'
    int depth = 0;
    size_t beg = i + 1; // skip '('
    for (; i < bodyEnd; ++i) {
        char c = s[i];
        if (c == '(') depth++;
        else if (c == ')') {
            depth--;
            if (depth == 0) {
                // find following ';'
                size_t j = i + 1; skipSpaces(s, j);
                if (j < bodyEnd && s[j] == ';') {
                    argBlock.assign(s.begin() + beg, s.begin() + i);
                    pos = j + 1;
                    return true;
                }
            }
        }
    }
    return false;
}

void VerilogParser::parseInstanceConnections(const std::string& args,
    std::vector<PinConnection>& outPins) {
    size_t i = 0, n = args.size();
    while (i < n) {
        skipSpaces(args, i);
        if (i >= n) break;

        // ===== A) positional 連接：.PIN(...) 以外的情況 =====
        if (args[i] != '.') {
            std::string net;
            bool isEsc = false;

            if (i < n && args[i] == '\\') {
                isEsc = true;
                net = readEscapedIdentifier(args, i);   // 例如 "\in1[72] "
            }
            else if (i < n && (isIdentifierStart(args[i]) || args[i] == '0' || args[i] == '1')) {
                // 識別字或常數開頭
                size_t idBeg = i;
                net = readIdentifier(args, i);          // 先拿到 "in1"
                // NEW: 把緊跟的 [..] 後綴補上 -> "in1[72]" / "in1[99:0]"
                net += readBracketSuffixes(args, i);
            }

            // 跳到逗號
            while (i < n && args[i] != ',') ++i;
            if (i < n && args[i] == ',') ++i;

            if (!net.empty()) outPins.push_back({ "", net });
            continue;
        }

        // ===== B) named 連接：.PIN ( NET ) =====
        ++i; // skip '.'

        // 讀 pin 名
        std::string pin;
        if (i < n && args[i] == '\\') {
            pin = readEscapedIdentifier(args, i);       // 例如 "\D[3] "
        }
        else {
            pin = readIdentifier(args, i);              // 例如 "D"
            // NEW: pin 也可能有 [k]，補上
            pin += readBracketSuffixes(args, i);        // -> "D[3]"
        }

        skipSpaces(args, i);
        if (i >= n || args[i] != '(') {
            // 同你原本的 fallback...
            while (i < n && args[i] != ',') ++i;
            if (i < n) ++i;
            continue;
        }
        ++i; skipSpaces(args, i);

        // 讀 net
        std::string net;
        if (i < n && args[i] == '\\') {
            net = readEscapedIdentifier(args, i);       // "\in1[72] "
        }
        else if (i < n && (isIdentifierStart(args[i]) || args[i] == '0' || args[i] == '1')) {
            net = readIdentifier(args, i);              // "in1"
            // NEW: 把 [..] 後綴補上 -> "in1[72]"
            net += readBracketSuffixes(args, i);
        }

        // 收掉 ')', 跳過空白與逗號（保留你原本的邏輯）
        while (i < n && args[i] != ')') ++i;
        if (i < n && args[i] == ')') ++i;
        skipSpaces(args, i);
        if (i < n && args[i] == ',') ++i;

        outPins.push_back({ pin, net });
    }
}

void VerilogParser::rebuildLinearViewsAndLookups() {
    modulesLinear_.clear();
    flatInstances_.clear();
    nameToInst_.clear();
    fullToInst_.clear();
    localToFull_.clear();

    // 模組複製到 linear
    modulesLinear_.reserve(moduleMap_.size());
    for (auto& kv : moduleMap_) {
        modulesLinear_.push_back(*kv.second);
    }

    // 扁平化 instances
    for (const auto& M : modulesLinear_) {
        for (const auto& inst : M.instances) {
            flatInstances_.push_back(inst);
        }
    }

    // local -> full（目前先 local==full）
    for (const auto& inst : flatInstances_) {
        localToFull_[inst.instName] = inst.instName;
    }
    // 名稱查找
    for (const auto& inst : flatInstances_) {
        nameToInst_[inst.instName] = &inst;
    }

    // >>> 新增：重建 inst-pin-net
    rebuildInstPinNets();
}
void VerilogParser::analyzeHierarchy() {
    using std::cout; using std::endl;

    cout << "\n=== Verilog Hierarchy Analysis ===" << endl;
    cout << "Modules found: " << modulesLinear_.size() << endl;
    for (const auto& M : modulesLinear_) {
        cout << "  Module: " << M.name
            << "  Ports:" << M.ports.size()
            << "  Inputs:" << M.inputs.size()
            << "  Outputs:" << M.outputs.size()
            << "  Wires:" << M.wires.size()
            << "  Assigns:" << M.assignStatements.size()
            << endl;
    }

    cout << "\nInstances found: " << flatInstances_.size() << endl;

    // cell type 統計
    std::map<std::string, int> stats;
    int ffCount = 0;
    for (const auto& inst : flatInstances_) {
        stats[inst.cellType]++;
        if (isLikelyFlipFlop(inst.cellType)) ffCount++;
    }

    cout << "\nCell type statistics (top 20):" << endl;
    int shown = 0;
    for (auto it = stats.rbegin(); it != stats.rend() && shown < 20; ++it, ++shown) {
        cout << "  " << it->first << " : " << it->second << endl;
    }
    cout << "\nFlip-Flops (heuristic): " << ffCount << endl;

    // 簡單 clock domain 統計
    std::map<std::string, int> clkDomain;
    for (const auto& inst : flatInstances_) {
        if (!isLikelyFlipFlop(inst.cellType)) continue;
        for (const auto& pc : inst.pinConnections) {
            if (isClockPinName(pc.pin) && !pc.net.empty()) {
                clkDomain[pc.net]++;
                break;
            }
        }
    }
    if (!clkDomain.empty()) {
        cout << "\nClock domains found: " << clkDomain.size() << endl;
        for (const auto& kv : clkDomain) {
            cout << "  Clock '" << kv.first << "': " << kv.second << " FFs" << endl;
        }
    }
}
const VerilogInstance* VerilogParser::findInstance(const std::string& name) const {
    auto it = nameToInst_.find(name);
    if (it != nameToInst_.end()) return it->second;
    return nullptr;
}
const VerilogInstance* VerilogParser::findInstanceByHierarchicalPath(const std::string& fullPath) const {
    auto it = fullToInst_.find(fullPath);
    if (it != fullToInst_.end()) return it->second;
    // 後備：取最後一段當簡名
    size_t p = fullPath.find_last_of('/');
    if (p != std::string::npos) {
        auto it2 = nameToInst_.find(fullPath.substr(p + 1));
        if (it2 != nameToInst_.end()) return it2->second;
    }
    return nullptr;
}

std::shared_ptr<VerilogParser::HierarchyNode>
VerilogParser::buildHierarchy(const std::string& topModule) {
    std::string root = topModule.empty() ? topModule_ : topModule;
    if (root.empty() || !isKnownModule(root)) return nullptr;
    auto rootNode = buildHierarchyRecursive(root, "", nullptr);

    // 用一層展開的 children 來補 full path 映射
    // 注意：此實作避免深遞迴以免記憶體炸裂，對 WriteOutput 只要能把 local 映到某個 full 形式即可
    localToFull_.clear();
    fullToInst_.clear();

    // root 用模組名本身
    localToFull_[rootNode->instanceName] = rootNode->fullPath;

    // 將 top module 的 child 實例掛上 fullPath 映射
    auto it = moduleMap_.find(root);
    if (it != moduleMap_.end()) {
        for (const auto& inst : it->second->instances) {
            std::string full = rootNode->fullPath + "/" + inst.instName;
            localToFull_[inst.instName] = full;
            // 反向：full -> instance 指標（用簡名查）
            if (auto p = findInstance(inst.instName)) {
                fullToInst_[full] = p;
            }
        }
    }

    return rootNode;
}
std::string VerilogParser::toUpper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
bool VerilogParser::isClockPinName(const std::string& pin) {
    std::string p = toUpper(pin);
    return (p == "CK" || p == "CLK" || p == "CLOCK" || p == "CP" || p == "C");
}
bool VerilogParser::isLikelyFlipFlop(const std::string& cellType) const {
    // 先用 .lib 的權威資訊
    if (lib_) {
        if (const LibCell* c = lib_->getCell(cellType)) {
            if (c->hasFF) return true;
            if (!c->ffType.empty()) return true;
            if (!c->singleBitDegenerate.empty()) return true;
            // 也可再加：若 pins/bundles 顯示有 D/Q 成對且 clock pin 存在，可視為 FF
            // 但目前以上三條足夠且安全
        }
    }

    // 找不到 .lib 或 .lib 無法判斷：退回名稱啟發式（你原先的規則）
    std::string u = toUpper(cellType);
    return (u.find("DFF") != std::string::npos ||
        u.find("SDFF") != std::string::npos ||
        u.find("FF_") != std::string::npos ||
        u.find("FLIP") != std::string::npos ||
        u.find("FSD") != std::string::npos ||   // SNPS FSDN*
        u.find("SSR") != std::string::npos);    // e.g., SSRRDPQ*
}
void VerilogParser::rebuildInstPinNets() {
    instPinNets_.clear();
    instPinNets_.reserve(flatInstances_.size() * 4);

    for (const auto& inst : flatInstances_) {
        const std::string full =
            (localToFull_.count(inst.instName) ? localToFull_.at(inst.instName) : inst.instName);

        // 兼容 pinConnections / connections 兩種來源
        if (!inst.pinConnections.empty()) {
            for (const auto& pc : inst.pinConnections) {
                if (pc.pin.empty() || pc.net.empty()) continue;
                instPinNets_.push_back(InstPinNet{ full, pc.pin, pc.net });
            }
        }
        else if (!inst.connections.empty()) {
            for (const auto& pr : inst.connections) {
                if (pr.first.empty() || pr.second.empty()) continue;
                instPinNets_.push_back(InstPinNet{ full, pr.first, pr.second });
            }
        }
    }
}
std::shared_ptr<VerilogParser::HierarchyNode>
VerilogParser::buildHierarchyRecursive(const std::string& moduleName,
    const std::string& parentPath,
    std::shared_ptr<HierarchyNode> parent) {
    auto node = std::make_shared<HierarchyNode>();
    node->instanceName = moduleName; // root uses module name as instanceName
    node->moduleName = moduleName;
    node->fullPath = parentPath.empty() ? moduleName : parentPath + "/" + moduleName;
    node->parent = parent;

    auto it = moduleMap_.find(moduleName);
    if (it == moduleMap_.end()) return node;
    VerilogModule& M = *it->second;

    // Guard set to avoid cycles (self-instantiation etc.) per path
    std::set<std::string> visitedTypesOnPath; // lightweight guard by type
    visitedTypesOnPath.insert(moduleName);

    for (const auto& inst : M.instances) {
        std::string childPath = node->fullPath + "/" + inst.instName;
        if (inst.isModuleInstance) {
            const std::string& childMod = inst.referencedModule.empty() ? inst.cellType : inst.referencedModule;
            if (visitedTypesOnPath.count(childMod)) {
                // self or cyclic instantiation — skip deeper expansion
                continue;
            }
            auto child = std::make_shared<HierarchyNode>();
            child->instanceName = inst.instName;
            child->moduleName = childMod;
            child->fullPath = childPath;
            child->parent = node;
            node->children.push_back(child);
            // Shallow expansion to avoid OOM on huge recursive graphs — expand one level only or copy guard logic if deeper is needed
        }
    }

    return node;
}
void VerilogParser::findClockNets() {
    using std::cout; using std::endl;
    std::unordered_set<std::string> nets;

    // 從 instance pins 找
    for (const auto& inst : flatInstances_) {
        for (const auto& pc : inst.pinConnections) {
            if (isClockPinName(pc.pin) && !pc.net.empty()) nets.insert(pc.net);
        }
    }
    // 從 module inputs 名稱推測（clk/clock）
    for (const auto& M : modulesLinear_) {
        for (const auto& p : M.inputs) {
            std::string up = toUpper(p);
            if (up.find("CLK") != std::string::npos || up == "CK" || up == "CP" || up == "CLOCK")
                nets.insert(p);
        }
    }

    cout << "\n=== Clock Net Detection ===" << endl;
    cout << "Clock nets detected: " << nets.size() << endl;
    for (const auto& n : nets) cout << "  " << n << endl;
}
void VerilogParser::printScanChainSummary() {
    using std::cout; using std::endl;
    cout << "\n=== Scan Chain Summary ===" << endl;

    // 找出 FF
    std::vector<const VerilogInstance*> ffs;
    for (const auto& inst : flatInstances_) {
        if (isLikelyFlipFlop(inst.cellType)) ffs.push_back(&inst);
    }
    if (ffs.empty()) { cout << "No flip-flops found for scan chain analysis." << endl; return; }

    // 簡單列出每個 FF 的 SI/SO 連結，並嘗試建立 SO->SI map
    std::unordered_map<std::string, std::string> siNet2ff;
    std::unordered_map<std::string, std::string> soNet2ff;

    auto isSI = [](std::string p) { auto u = VerilogParser::toUpper(p); return (u == "SI" || u == "SCAN_IN"); };
    auto isSO = [](std::string p) { auto u = VerilogParser::toUpper(p); return (u == "SO" || u == "SCAN_OUT"); };

    for (auto* ff : ffs) {
        for (const auto& pc : ff->pinConnections) {
            if (isSI(pc.pin) && !pc.net.empty()) siNet2ff[pc.net] = ff->instName;
            else if (isSO(pc.pin) && !pc.net.empty()) soNet2ff[pc.net] = ff->instName;
        }
    }

    cout << "Flip-flops (heuristic): " << ffs.size() << endl;
    cout << "  with SI nets: " << siNet2ff.size() << ", with SO nets: " << soNet2ff.size() << endl;

    // 嘗試用 SO→SI 串鏈（很簡版，用於偵錯）
    std::unordered_set<std::string> used;
    int chainIdx = 0;
    for (const auto& so : soNet2ff) {
        const std::string& net = so.first;
        auto it = siNet2ff.find(net);
        if (it == siNet2ff.end()) continue;
        if (used.count(net)) continue;
        used.insert(net);

        cout << "\n--- Scan Chain " << (++chainIdx) << " seed on net " << net << " ---" << endl;
        cout << "  " << so.second << " (SO) -> " << it->second << " (SI)" << endl;
    }

    if (chainIdx == 0) cout << "No explicit SO->SI arcs detected (SI/SO may be unused in this testcase)." << endl;
}