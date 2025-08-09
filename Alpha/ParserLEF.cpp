#include "ParserLEF.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <algorithm>

using namespace std;

// LefPort implementation
ParseRes LefPort::parseNext(const vector<string>& tk, Statement*& child) {
    child = nullptr;
    if (tk.empty()) return ParseRes::kContinue;
    if (tk[0] == "END") return ParseRes::kDone;
    if (tk[0] == "LAYER" && tk.size() >= 2)
        layers.emplace_back(make_unique<LayerDef>(tk[1]));
    else if (tk[0] == "RECT" && layers.size())
        layers.back()->rects.push_back({ stod(tk[1]),stod(tk[2]),stod(tk[3]),stod(tk[4]) });
    else if (tk[0] == "POLYGON" && layers.size()) {
        Poly poly;
        for (size_t i = 1; i + 1 < tk.size(); i += 2)
            poly.pts.emplace_back(stod(tk[i]), stod(tk[i + 1]));
        layers.back()->polys.push_back(move(poly));
    }
    return ParseRes::kContinue;
}

// LefPin implementation
ParseRes LefPin::parseNext(const vector<string>& tk, Statement*& child) {
    child = nullptr;
    if (tk.empty()) return ParseRes::kContinue;
    if (tk[0] == "DIRECTION" && tk.size() >= 2)
        info["DIRECTION"] = tk[1];
    else if (tk[0] == "USE" && tk.size() >= 2)
        info["USE"] = tk[1];
    else if (tk[0] == "ANTENNADIFFAREA" && tk.size() >= 2)
        info["ANTENNADIFFAREA"] = tk[1];
    else if (tk[0] == "PORT") {
        port = make_unique<LefPort>();
        child = port.get();
        return ParseRes::kChild;
    }
    else if (tk[0] == "END" && tk.size() == 2 && tk[1] == name)
        return ParseRes::kDone;
    return ParseRes::kContinue;
}

// LefMacro implementation
ParseRes LefMacro::parseNext(const vector<string>& tk, Statement*& child) {
    child = nullptr;
    if (tk.empty()) return ParseRes::kContinue;
    const string& kw = tk[0];
    if (kw == "CLASS" && tk.size() >= 2)
        info["CLASS"] = tk[1];
    else if (kw == "ORIGIN" && tk.size() >= 3)
        origin = { stod(tk[1]), stod(tk[2]) };
    else if (kw == "SIZE" && tk.size() >= 4)
        size = { stod(tk[1]),stod(tk[3]) };
    else if (kw == "SYMMETRY" && tk.size() >= 2)
        info["SYMMETRY"] = tk[1];
    else if (kw == "SITE" && tk.size() >= 2)
        info["SITE"] = tk[1];
    else if (kw == "PIN" && tk.size() >= 2) {
        pins.emplace_back(make_unique<LefPin>(tk[1]));
        child = pins.back().get();
        return ParseRes::kChild;
    }
    else if (kw == "OBS") {
        obs = make_unique<LefObs>();
        child = obs.get();
        return ParseRes::kChild;
    }
    else if (kw == "END" && tk.size() == 2 && tk[1] == name)
        return ParseRes::kDone;
    return ParseRes::kContinue;
}

// LefLayer implementation
ParseRes LefLayer::parseNext(const vector<string>& tk, Statement*& child) {
    child = nullptr;
    if (tk.empty()) return ParseRes::kContinue;
    if (tk[0] == "TYPE" && tk.size() >= 2)
        info["TYPE"] = tk[1];
    else if (tk[0] == "DIRECTION" && tk.size() >= 2)
        info["DIRECTION"] = tk[1];
    else if (tk[0] == "PITCH" && tk.size() >= 2)
        info["PITCH"] = tk[1];
    else if (tk[0] == "WIDTH" && tk.size() >= 2)
        info["WIDTH"] = tk[1];
    else if (tk[0] == "SPACING" && tk.size() >= 2)
        info["SPACING"] = tk[1];
    else if (tk[0] == "END" && tk.size() == 2 && tk[1] == name)
        return ParseRes::kDone;
    return ParseRes::kContinue;
}

// LefVia implementation
ParseRes LefVia::parseNext(const vector<string>& tk, Statement*& child) {
    child = nullptr;
    if (tk.empty()) return ParseRes::kContinue;
    if (tk[0] == "END") return ParseRes::kDone;
    if (tk[0] == "LAYER" && tk.size() >= 2)
        layers.emplace_back(make_unique<LayerDef>(tk[1]));
    else if (tk[0] == "RECT" && layers.size())
        layers.back()->rects.push_back({ stod(tk[1]),stod(tk[2]),stod(tk[3]),stod(tk[4]) });
    return ParseRes::kContinue;
}

// LefSite implementation
ParseRes LefSite::parseNext(const vector<string>& tk, Statement*& child) {
    child = nullptr;
    if (tk.empty()) return ParseRes::kContinue;
    if (tk[0] == "CLASS" && tk.size() >= 2)
        info["CLASS"] = tk[1];
    else if (tk[0] == "SYMMETRY" && tk.size() >= 2) {
        std::string sym_line;
        for (size_t i = 1; i < tk.size(); ++i) {
            if (i > 1) sym_line += " ";
            sym_line += tk[i];
        }
        info["SYMMETRY"] = sym_line;
    }
    else if (tk[0] == "SIZE" && tk.size() >= 4)
        size = { stod(tk[1]), stod(tk[3]) };
    else if (tk[0] == "END" && tk.size() == 2 && tk[1] == name)
        return ParseRes::kDone;
    return ParseRes::kContinue;
}

// LefParser helper methods
vector<string> LefParser::tokenize(const string& line) {
    vector<string> tok;
    string word;
    istringstream iss(line);
    while (iss >> word) {
        if (!word.empty() && word.back() == ';') word.pop_back();
        tok.push_back(word);
    }
    return tok;
}

// Main parsing method
bool LefParser::parseFile(const string& filename) {
    ifstream fin(filename);
    if (!fin) {
        cerr << "Error: Cannot open LEF file: " << filename << endl;
        return false;
    }

    try {
        string line;
        vector<Statement*> stack;            // raw‑ptr stack (non‑owning)

        while (getline(fin, line)) {
            auto tk = tokenize(line);
            if (tk.empty()) continue;

            // Global settings (outside any block)
            if (stack.empty()) {
                if (tk[0] == "VERSION" && tk.size() >= 2) {
                    version = stod(tk[1]);
                    continue;
                }
                else if (tk[0] == "BUSBITCHARS" && tk.size() >= 2) {
                    busBitChars = tk[1];
                    if (busBitChars.front() == '"' && busBitChars.back() == '"') {
                        busBitChars = busBitChars.substr(1, busBitChars.size() - 2);
                    }
                    continue;
                }
                else if (tk[0] == "UNITS" && tk.size() >= 5 && tk[1] == "DATABASE" && tk[2] == "MICRONS") {
                    units = stod(tk[3]);
                    continue;
                }
                else if (tk[0] == "MANUFACTURINGGRID" && tk.size() >= 2) {
                    manufacturingGrid = stod(tk[1]);
                    continue;
                }

                // top‑level block detection
                StmtUPtr obj;
                if (tk[0] == "MACRO" && tk.size() >= 2) {
                    auto macro = make_unique<LefMacro>(tk[1]);
                    // Check if ANTENNACELL in the same line
                    for (const auto& token : tk) {
                        if (token == "ANTENNACELL") {
                            macro->isAntennaCell = true;
                            break;
                        }
                    }
                    obj = move(macro);
                }
                else if (tk[0] == "LAYER" && tk.size() >= 2)
                    obj = make_unique<LefLayer>(tk[1]);
                else if (tk[0] == "VIA" && tk.size() >= 2)
                    obj = make_unique<LefVia>(tk[1]);
                else if (tk[0] == "SITE" && tk.size() >= 2)
                    obj = make_unique<LefSite>(tk[1]);

                if (!obj) continue;                       // skip other lines
                Statement* raw = obj.get();
                allObjs.emplace_back(move(obj));    // take ownership
                stack.push_back(raw);
                continue;                                // read next line
            }

            // inside a block
            Statement* cur = stack.back();
            Statement* child = nullptr;
            ParseRes res = cur->parseNext(tk, child);

            if (res == ParseRes::kChild && child) {
                stack.push_back(child);                  // push observational ptr
            }
            else if (res == ParseRes::kDone) {
                // pop finished block
                Statement* finished = stack.back();
                stack.pop_back();
                if (auto m = dynamic_cast<LefMacro*>(finished))
                    macroDict[m->name] = m;
                else if (auto l = dynamic_cast<LefLayer*>(finished))
                    layerDict[l->name] = l;
                else if (auto v = dynamic_cast<LefVia*>(finished))
                    viaDict[v->name] = v;
                else if (auto s = dynamic_cast<LefSite*>(finished))
                    siteDict[s->name] = s;
            }
        }

        fin.close();
        isLoaded_ = true;  // 設置載入狀態
        return true;
    }
    catch (const exception& e) {
        cerr << "Error parsing LEF file: " << e.what() << endl;
        fin.close();
        return false;
    }
}

// Convert to LefData format
LefData LefParser::convertToLefData() const {
    LefData lefData;
    lefData.version = version;
    lefData.busBitChars = busBitChars;
    lefData.units = units;
    lefData.manufacturingGrid = manufacturingGrid;

    // Convert Sites
    for (const auto& sitePair : siteDict) {
        const LefSite* site = sitePair.second;

        LefSiteInfo siteInfo;
        siteInfo.name = site->name;

        auto classIt = site->info.find("CLASS");
        siteInfo.siteClass = (classIt != site->info.end()) ? classIt->second : "";

        auto symmetryIt = site->info.find("SYMMETRY");
        if (symmetryIt != site->info.end()) {
            std::istringstream iss(symmetryIt->second);
            std::string s;
            while (iss >> s) siteInfo.symmetry.push_back(s);
        }



        siteInfo.width = site->size.first;
        siteInfo.height = site->size.second;
        lefData.sites.push_back(siteInfo);
    }

    // Convert Macros
    for (const auto& macroPair : macroDict) {
        const LefMacro* macro = macroPair.second;

        LefMacroInfo macroInfo;
        macroInfo.name = macro->name;

        auto classIt = macro->info.find("CLASS");
        macroInfo.macroClass = (classIt != macro->info.end()) ? classIt->second : "";

        macroInfo.originX = macro->origin.first;
        macroInfo.originY = macro->origin.second;
        macroInfo.sizeX = macro->size.first;
        macroInfo.sizeY = macro->size.second;
        auto symmetryIt = macro->info.find("SYMMETRY");
        if (symmetryIt != macro->info.end()) {
            std::istringstream iss(symmetryIt->second);
            std::string s;
            while (iss >> s) macroInfo.symmetry.push_back(s);
        }


        auto siteIt = macro->info.find("SITE");
        macroInfo.site = (siteIt != macro->info.end()) ? siteIt->second : "";

        macroInfo.isAntennaCell = macro->isAntennaCell;

        // Convert Pins
        for (const auto& pin : macro->pins) {
            LefPinInfo pinInfo;
            pinInfo.name = pin->name;

            auto directionIt = pin->info.find("DIRECTION");
            pinInfo.direction = (directionIt != pin->info.end()) ? directionIt->second : "";

            auto useIt = pin->info.find("USE");
            pinInfo.use = (useIt != pin->info.end()) ? useIt->second : "";

            auto antennaIt = pin->info.find("ANTENNADIFFAREA");
            if (antennaIt != pin->info.end()) {
                pinInfo.antennadiffarea = stod(antennaIt->second);
            }

            // Convert Ports
            if (pin->port) {
                for (const auto& layer : pin->port->layers) {
                    for (const auto& rect : layer->rects) {
                        LefPinPortInfo portInfo;
                        portInfo.layer = layer->name;
                        portInfo.x1 = rect.x0;
                        portInfo.y1 = rect.y0;
                        portInfo.x2 = rect.x1;
                        portInfo.y2 = rect.y1;
                        portInfo.isPolygon = false;
                        pinInfo.ports.push_back(portInfo);
                    }
                    for (const auto& poly : layer->polys) {
                        LefPinPortInfo portInfo;
                        portInfo.layer = layer->name;
                        portInfo.points = poly.pts;
                        portInfo.isPolygon = true;
                        pinInfo.ports.push_back(portInfo);
                    }
                }
            }
            macroInfo.pins.push_back(pinInfo);
        }

        // Convert Obstructions
        if (macro->obs) {
            for (const auto& layer : macro->obs->layers) {
                for (const auto& rect : layer->rects) {
                    LefObstructionInfo obsInfo;
                    obsInfo.layer = layer->name;
                    obsInfo.x1 = rect.x0;
                    obsInfo.y1 = rect.y0;
                    obsInfo.x2 = rect.x1;
                    obsInfo.y2 = rect.y1;
                    obsInfo.isPolygon = false;
                    macroInfo.obstructions.push_back(obsInfo);
                }
                for (const auto& poly : layer->polys) {
                    LefObstructionInfo obsInfo;
                    obsInfo.layer = layer->name;
                    obsInfo.points = poly.pts;
                    obsInfo.isPolygon = true;
                    macroInfo.obstructions.push_back(obsInfo);
                }
            }
        }

        lefData.macros.push_back(macroInfo);
    }

    return lefData;
}

// Get data interface
LefData LefParser::getData() const {
    return convertToLefData();
}

// Debug methods
void LefParser::printSummary() const {
    cout << "=== LEF Parser Summary ===" << endl;
    cout << "Version: " << version << endl;
    cout << "Bus Bit Characters: " << busBitChars << endl;
    cout << "Units: " << units << endl;
    cout << "Manufacturing Grid: " << manufacturingGrid << endl;
    cout << "MACRO count: " << macroDict.size() << endl;
    cout << "LAYER count: " << layerDict.size() << endl;
    cout << "VIA count: " << viaDict.size() << endl;
    cout << "SITE count: " << siteDict.size() << endl;
}

void LefParser::printMacroDetails(int maxCount) const {
    cout << "\n=== LEF Macro Details ===" << endl;
    cout << left << setw(4) << "Idx"
        << setw(30) << "Name"
        << setw(10) << "Class"
        << setw(14) << "Size(um)"
        << setw(6) << "#Pin"
        << "First 3 pins" << endl
        << string(80, '-') << endl;

    int idx = 0;
    for (auto it = macroDict.begin(); it != macroDict.end() && idx < maxCount; ++it, ++idx) {
        const LefMacro* m = it->second;

        // CLASS
        string cls;
        auto clsIt = m->info.find("CLASS");
        if (clsIt != m->info.end()) cls = clsIt->second;

        // Size
        ostringstream oss;
        oss << fixed << setprecision(3) << m->size.first << 'x' << m->size.second;

        // First 3 pins
        string pinStr;
        for (size_t i = 0; i < m->pins.size() && i < 3; ++i) {
            const LefPin* p = m->pins[i].get();

            auto dirIt = p->info.find("DIRECTION");
            const string dir = (dirIt != p->info.end()) ? dirIt->second : "";

            auto useIt = p->info.find("USE");
            const string use = (useIt != p->info.end()) ? useIt->second : "";

            if (i) pinStr += ", ";
            pinStr += p->name + "(" + dir + "/" + use + ")";
        }

        cout << right << setw(3) << (idx + 1) << " "
            << left << setw(30) << m->name
            << setw(10) << cls
            << setw(14) << oss.str()
            << setw(6) << m->pins.size()
            << pinStr << endl;
    }
}

void LefParser::clear() {
    allObjs.clear();
    macroDict.clear();
    layerDict.clear();
    viaDict.clear();
    siteDict.clear();
    version = 0.0;
    busBitChars.clear();
    units = 0.0;
    manufacturingGrid = 0.0;
    isLoaded_ = false;  // 重置載入狀態
}