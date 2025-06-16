#include "ParserTech.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>

using namespace std;

TechParser::TechParser() : isLoaded_(false) {
    // Initialize regex patterns
    sectionStartRegex_ = regex(R"((\w+)\s*(\"[^\"]+\"|\S+)?\s*\{)");
    keyValueRegex_ = regex(R"((\w+)\s*=\s*(\(.+\)|\"?[^\"\n]+\"?))");
    blockEndRegex_ = regex(R"(^\s*\}\s*$)");
}

bool TechParser::parseFile(const string& filename) {
    ifstream infile(filename);
    if (!infile.is_open()) {
        addError("Cannot open technology file: " + filename);
        return false;
    }

    try {
        clear();

        // Read entire file content
        string content((istreambuf_iterator<char>(infile)), istreambuf_iterator<char>());
        infile.close();

        // Parse content
        size_t pos = 0;
        while (pos < content.length()) {
            if (!parseSection(content, pos)) {
                break;
            }
        }

        isLoaded_ = true;
        analyzeTechFile();
        return true;
    }
    catch (const exception& e) {
        addError("Error parsing technology file: " + string(e.what()));
        infile.close();
        return false;
    }
}

bool TechParser::parseFromString(const string& content) {
    try {
        clear();

        size_t pos = 0;
        while (pos < content.length()) {
            if (!parseSection(content, pos)) {
                break;
            }
        }

        isLoaded_ = true;
        analyzeTechFile();
        return true;
    }
    catch (const exception& e) {
        addError("Error parsing technology string: " + string(e.what()));
        return false;
    }
}

bool TechParser::parseSection(const string& content, size_t& pos) {
    // Skip whitespace and comments
    while (pos < content.length() && (isspace(content[pos]) || content[pos] == '#')) {
        if (content[pos] == '#') {
            // Skip comment line
            while (pos < content.length() && content[pos] != '\n') {
                pos++;
            }
        }
        pos++;
    }

    if (pos >= content.length()) return false;

    // Find section start
    size_t lineStart = pos;
    size_t lineEnd = content.find('\n', pos);
    if (lineEnd == string::npos) lineEnd = content.length();

    string line = content.substr(lineStart, lineEnd - lineStart);

    smatch match;
    if (regex_search(line, match, sectionStartRegex_)) {
        string sectionType = match[1];
        string sectionName = match.size() > 2 ? match[2].str() : "";

        pos = lineEnd + 1;

        if (sectionType == "Technology") {
            return parseTechnology(content, pos);
        }
        else if (sectionType == "Color") {
            int colorId = 0;
            if (!sectionName.empty()) {
                try {
                    colorId = stoi(sectionName);
                }
                catch (...) {
                    colorId = 0;
                }
            }
            return parseColor(content, pos, colorId);
        }
        else if (sectionType == "Layer") {
            string layerName = TechUtils::removeQuotes(sectionName);
            return parseLayer(content, pos, layerName);
        }
        else if (sectionType == "ContactCode") {
            string contactName = TechUtils::removeQuotes(sectionName);
            return parseContactCode(content, pos, contactName);
        }
        else if (sectionType == "DesignRule") {
            return parseDesignRule(content, pos);
        }
        else if (sectionType == "PRRule") {
            return parsePRRule(content, pos);
        }
        else if (sectionType == "DensityRule") {
            return parseDensityRule(content, pos);
        }
        else if (sectionType == "LayerDataType") {
            string typeName = TechUtils::removeQuotes(sectionName);
            return parseLayerDataType(content, pos, typeName);
        }
        else {
            // Skip unknown section
            int braceCount = 1;
            while (pos < content.length() && braceCount > 0) {
                if (content[pos] == '{') braceCount++;
                else if (content[pos] == '}') braceCount--;
                pos++;
            }
            return true;
        }
    }
    else {
        // Skip this line
        pos = lineEnd + 1;
        return true;
    }
}

bool TechParser::parseTechnology(const string& content, size_t& pos) {
    size_t blockStart = pos;
    size_t blockEnd = pos;
    int braceCount = 1;

    while (blockEnd < content.length() && braceCount > 0) {
        if (content[blockEnd] == '{') braceCount++;
        else if (content[blockEnd] == '}') braceCount--;
        blockEnd++;
    }

    if (braceCount != 0) {
        addError("Unmatched braces in Technology section");
        return false;
    }

    parseKeyValuePairs(content, blockStart, blockEnd - 1, techData_.tech.parameters);
    pos = blockEnd;
    return true;
}

bool TechParser::parseColor(const string& content, size_t& pos, int colorId) {
    Color color;
    color.id = colorId;

    size_t blockStart = pos;
    size_t blockEnd = pos;
    int braceCount = 1;

    while (blockEnd < content.length() && braceCount > 0) {
        if (content[blockEnd] == '{') braceCount++;
        else if (content[blockEnd] == '}') braceCount--;
        blockEnd++;
    }

    if (braceCount != 0) {
        addError("Unmatched braces in Color section");
        return false;
    }

    parseKeyValuePairs(content, blockStart, blockEnd - 1, color.parameters);
    techData_.colors.push_back(color);
    pos = blockEnd;
    return true;
}

bool TechParser::parseLayer(const string& content, size_t& pos, const string& layerName) {
    Layer layer;
    layer.name = layerName;

    size_t blockStart = pos;
    size_t blockEnd = pos;
    int braceCount = 1;

    while (blockEnd < content.length() && braceCount > 0) {
        if (content[blockEnd] == '{') braceCount++;
        else if (content[blockEnd] == '}') braceCount--;
        blockEnd++;
    }

    if (braceCount != 0) {
        addError("Unmatched braces in Layer section");
        return false;
    }

    parseKeyValuePairs(content, blockStart, blockEnd - 1, layer.parameters);
    techData_.layers.push_back(layer);
    pos = blockEnd;
    return true;
}

bool TechParser::parseContactCode(const string& content, size_t& pos, const string& contactName) {
    ContactCode contact;
    contact.name = contactName;

    size_t blockStart = pos;
    size_t blockEnd = pos;
    int braceCount = 1;

    while (blockEnd < content.length() && braceCount > 0) {
        if (content[blockEnd] == '{') braceCount++;
        else if (content[blockEnd] == '}') braceCount--;
        blockEnd++;
    }

    if (braceCount != 0) {
        addError("Unmatched braces in ContactCode section");
        return false;
    }

    parseKeyValuePairs(content, blockStart, blockEnd - 1, contact.parameters);
    techData_.contacts.push_back(contact);
    pos = blockEnd;
    return true;
}

bool TechParser::parseDesignRule(const string& content, size_t& pos) {
    DesignRule rule;

    size_t blockStart = pos;
    size_t blockEnd = pos;
    int braceCount = 1;

    while (blockEnd < content.length() && braceCount > 0) {
        if (content[blockEnd] == '{') braceCount++;
        else if (content[blockEnd] == '}') braceCount--;
        blockEnd++;
    }

    if (braceCount != 0) {
        addError("Unmatched braces in DesignRule section");
        return false;
    }

    parseKeyValuePairs(content, blockStart, blockEnd - 1, rule.parameters);
    techData_.designRules.push_back(rule);
    pos = blockEnd;
    return true;
}

bool TechParser::parsePRRule(const string& content, size_t& pos) {
    PRRule rule;

    size_t blockStart = pos;
    size_t blockEnd = pos;
    int braceCount = 1;

    while (blockEnd < content.length() && braceCount > 0) {
        if (content[blockEnd] == '{') braceCount++;
        else if (content[blockEnd] == '}') braceCount--;
        blockEnd++;
    }

    if (braceCount != 0) {
        addError("Unmatched braces in PRRule section");
        return false;
    }

    parseKeyValuePairs(content, blockStart, blockEnd - 1, rule.parameters);
    techData_.prRules.push_back(rule);
    pos = blockEnd;
    return true;
}

bool TechParser::parseDensityRule(const string& content, size_t& pos) {
    DensityRule rule;

    size_t blockStart = pos;
    size_t blockEnd = pos;
    int braceCount = 1;

    while (blockEnd < content.length() && braceCount > 0) {
        if (content[blockEnd] == '{') braceCount++;
        else if (content[blockEnd] == '}') braceCount--;
        blockEnd++;
    }

    if (braceCount != 0) {
        addError("Unmatched braces in DensityRule section");
        return false;
    }

    parseKeyValuePairs(content, blockStart, blockEnd - 1, rule.parameters);
    techData_.densityRules.push_back(rule);
    pos = blockEnd;
    return true;
}

bool TechParser::parseLayerDataType(const string& content, size_t& pos, const string& typeName) {
    LayerDataType type;
    type.name = typeName;

    size_t blockStart = pos;
    size_t blockEnd = pos;
    int braceCount = 1;

    while (blockEnd < content.length() && braceCount > 0) {
        if (content[blockEnd] == '{') braceCount++;
        else if (content[blockEnd] == '}') braceCount--;
        blockEnd++;
    }

    if (braceCount != 0) {
        addError("Unmatched braces in LayerDataType section");
        return false;
    }

    parseKeyValuePairs(content, blockStart, blockEnd - 1, type.parameters);
    techData_.layerDataTypes.push_back(type);
    pos = blockEnd;
    return true;
}

bool TechParser::parseKeyValuePairs(const string& content, size_t start, size_t end,
    map<string, string>& parameters) {
    string section = content.substr(start, end - start);
    istringstream iss(section);
    string line;

    while (getline(iss, line)) {
        // Skip empty lines and comments
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == '#') continue;

        smatch match;
        if (regex_search(line, match, keyValueRegex_)) {
            string key = match[1];
            string value = match[2];

            // Remove quotes if present
            value = TechUtils::removeQuotes(value);

            if (TechUtils::isValidParameterName(key)) {
                parameters[key] = value;
            }
        }
    }

    return true;
}

const Color* TechParser::findColor(int id) const {
    auto it = find_if(techData_.colors.begin(), techData_.colors.end(),
        [id](const Color& color) { return color.id == id; });
    return (it != techData_.colors.end()) ? &(*it) : nullptr;
}

const Layer* TechParser::findLayer(const string& name) const {
    auto it = find_if(techData_.layers.begin(), techData_.layers.end(),
        [&name](const Layer& layer) { return layer.name == name; });
    return (it != techData_.layers.end()) ? &(*it) : nullptr;
}

const ContactCode* TechParser::findContact(const string& name) const {
    auto it = find_if(techData_.contacts.begin(), techData_.contacts.end(),
        [&name](const ContactCode& contact) { return contact.name == name; });
    return (it != techData_.contacts.end()) ? &(*it) : nullptr;
}

const LayerDataType* TechParser::findLayerDataType(const string& name) const {
    auto it = find_if(techData_.layerDataTypes.begin(), techData_.layerDataTypes.end(),
        [&name](const LayerDataType& type) { return type.name == name; });
    return (it != techData_.layerDataTypes.end()) ? &(*it) : nullptr;
}

string TechParser::getTechParameter(const string& key) const {
    auto it = techData_.tech.parameters.find(key);
    return (it != techData_.tech.parameters.end()) ? it->second : "";
}

void TechParser::analyzeTechFile() {
    cout << "\n=== Analyzing Technology File ===" << endl;

    // Debug: Show what layers we parsed
    cout << "Total layers parsed: " << techData_.layers.size() << endl;
    cout << "Sample layers:" << endl;
    for (size_t i = 0; i < min(techData_.layers.size(), size_t(10)); ++i) {
        const auto& layer = techData_.layers[i];
        cout << "  " << layer.name;

        // Show some parameters to understand the structure
        auto maskIt = layer.parameters.find("maskName");
        auto layerNumIt = layer.parameters.find("layerNumber");
        if (maskIt != layer.parameters.end()) {
            cout << " (mask: " << maskIt->second << ")";
        }
        if (layerNumIt != layer.parameters.end()) {
            cout << " (layer: " << layerNumIt->second << ")";
        }
        cout << endl;
    }

    // Analyze layers with improved detection
    vector<string> metalLayers = getMetalLayers();
    vector<string> viaLayers = getViaLayers();
    vector<string> allRoutingLayers = getAllRoutingLayers();

    cout << "\nMetal/Routing layers found: " << metalLayers.size() << endl;
    for (const auto& layer : metalLayers) {
        cout << "  " << layer << endl;
    }

    cout << "\nVia/Cut layers found: " << viaLayers.size() << endl;
    for (const auto& layer : viaLayers) {
        cout << "  " << layer << endl;
    }

    cout << "\nAll routing-related layers: " << allRoutingLayers.size() << endl;
    for (const auto& layer : allRoutingLayers) {
        cout << "  " << layer << endl;
    }

    // Analyze contacts
    cout << "\nContact codes found: " << techData_.contacts.size() << endl;
    for (size_t i = 0; i < min(techData_.contacts.size(), size_t(5)); ++i) {
        const auto& contact = techData_.contacts[i];
        cout << "  " << contact.name;

        auto upperIt = contact.parameters.find("upperLayer");
        auto lowerIt = contact.parameters.find("lowerLayer");
        auto cutIt = contact.parameters.find("cutLayer");

        if (upperIt != contact.parameters.end() && lowerIt != contact.parameters.end()) {
            cout << " (" << lowerIt->second << " -> " << upperIt->second;
            if (cutIt != contact.parameters.end()) {
                cout << " via " << cutIt->second;
            }
            cout << ")";
        }
        cout << endl;
    }

    // Analyze process parameters
    map<string, string> processParams = getProcessParameters();
    cout << "\nProcess parameters: " << processParams.size() << endl;
    for (const auto& param : processParams) {
        cout << "  " << param.first << " = " << param.second << endl;
    }

    // Technology information
    cout << "\nTechnology information:" << endl;
    for (const auto& param : techData_.tech.parameters) {
        if (param.first == "name" || param.first == "date" ||
            param.first == "dielectric" || param.first == "gridResolution") {
            cout << "  " << param.first << " = " << param.second << endl;
        }
    }
}

vector<string> TechParser::getMetalLayers() const {
    vector<string> metalLayers;

    for (const auto& layer : techData_.layers) {
        // Check for metal layers by name pattern
        if (layer.name.find("M1") == 0 || layer.name.find("M2") == 0 ||
            layer.name.find("M3") == 0 || layer.name.find("M4") == 0 ||
            layer.name.find("M5") == 0 || layer.name.find("M6") == 0 ||
            layer.name.find("M7") == 0 || layer.name.find("M8") == 0 ||
            layer.name.find("M9") == 0 || layer.name.find("MRDL") == 0) {
            metalLayers.push_back(layer.name);
        }
        else {
            // Check parameters for routing indication
            auto maskIt = layer.parameters.find("maskName");
            if (maskIt != layer.parameters.end()) {
                string mask = maskIt->second;
                if (mask.find("metal") != string::npos ||
                    mask.find("m1") != string::npos || mask.find("m2") != string::npos ||
                    mask.find("m3") != string::npos || mask.find("m4") != string::npos) {
                    metalLayers.push_back(layer.name);
                }
            }
        }
    }

    return metalLayers;
}

vector<string> TechParser::getViaLayers() const {
    vector<string> viaLayers;

    for (const auto& layer : techData_.layers) {
        // Check for via layers by name pattern
        if (layer.name.find("VIA") == 0 || layer.name.find("Via") == 0 ||
            layer.name.find("via") == 0 || layer.name.find("CUT") == 0) {
            viaLayers.push_back(layer.name);
        }
        else {
            // Check parameters for cut/via indication
            auto maskIt = layer.parameters.find("maskName");
            if (maskIt != layer.parameters.end()) {
                string mask = maskIt->second;
                if (mask.find("via") != string::npos || mask.find("cut") != string::npos) {
                    viaLayers.push_back(layer.name);
                }
            }
        }
    }

    return viaLayers;
}

// 新增函數：獲取所有路由相關層
vector<string> TechParser::getAllRoutingLayers() const {
    vector<string> routingLayers;

    for (const auto& layer : techData_.layers) {
        // 檢查是否為路由相關層（金屬層、通孔層等）
        string layerName = layer.name;

        if (layerName.find("M") == 0 ||           // M1, M2, etc.
            layerName.find("VIA") != string::npos || // VIA layers
            layerName.find("METAL") != string::npos || // METAL layers
            layerName.find("RDL") != string::npos) {   // Redistribution layers
            routingLayers.push_back(layerName);
        }
    }

    return routingLayers;
}

map<string, string> TechParser::getProcessParameters() const {
    map<string, string> processParams;

    // Extract key process parameters from technology section
    for (const auto& param : techData_.tech.parameters) {
        if (param.first.find("process") != string::npos ||
            param.first.find("tech") != string::npos ||
            param.first.find("node") != string::npos ||
            param.first.find("name") != string::npos ||
            param.first.find("dielectric") != string::npos ||
            param.first.find("gridResolution") != string::npos ||
            param.first.find("lengthPrecision") != string::npos) {
            processParams[param.first] = param.second;
        }
    }

    return processParams;
}






void TechParser::clear() {
    techData_.tech.parameters.clear();
    techData_.colors.clear();
    techData_.layers.clear();
    techData_.contacts.clear();
    techData_.designRules.clear();
    techData_.prRules.clear();
    techData_.densityRules.clear();
    techData_.layerDataTypes.clear();
    errors_.clear();
    warnings_.clear();
    isLoaded_ = false;
}

void TechParser::printSummary() const {
    cout << "\n=== Technology Parser Summary ===" << endl;
    cout << "Technology: " << getTechParameter("name") << endl;
    cout << "Date: " << getTechParameter("date") << endl;
    cout << "Grid Resolution: " << getTechParameter("gridResolution") << endl;
    cout << "Dielectric: " << getTechParameter("dielectric") << endl;
    cout << endl;

    cout << "Parsed sections:" << endl;
    cout << "  Colors: " << techData_.colors.size() << endl;
    cout << "  Layers: " << techData_.layers.size() << endl;
    cout << "  Contacts: " << techData_.contacts.size() << endl;
    cout << "  Design Rules: " << techData_.designRules.size() << endl;
    cout << "  PR Rules: " << techData_.prRules.size() << endl;
    cout << "  Density Rules: " << techData_.densityRules.size() << endl;
    cout << "  Layer Data Types: " << techData_.layerDataTypes.size() << endl;

    // Show routing stack information - 調用 const 版本的函數
    const vector<string> metalLayers = this->getMetalLayers();
    const vector<string> viaLayers = this->getViaLayers();

    cout << "\nRouting stack:" << endl;
    cout << "  Metal layers: " << metalLayers.size();
    if (!metalLayers.empty()) {
        cout << " (";
        for (size_t i = 0; i < min(metalLayers.size(), size_t(5)); ++i) {
            if (i > 0) cout << ", ";
            cout << metalLayers[i];
        }
        if (metalLayers.size() > 5) cout << "...";
        cout << ")";
    }
    cout << endl;

    cout << "  Via layers: " << viaLayers.size();
    if (!viaLayers.empty()) {
        cout << " (";
        for (size_t i = 0; i < min(viaLayers.size(), size_t(3)); ++i) {
            if (i > 0) cout << ", ";
            cout << viaLayers[i];
        }
        if (viaLayers.size() > 3) cout << "...";
        cout << ")";
    }
    cout << endl;
}
void TechParser::printTechData() const {
    cout << "\n=== Technology File Data ===" << endl;

    cout << "\n=== Technology ===" << endl;
    for (const auto& param : techData_.tech.parameters) {
        cout << param.first << " = " << param.second << endl;
    }

    printColors();
    printLayers();
    printContacts();
}

void TechParser::printColors() const {
    cout << "\n=== Colors ===" << endl;
    for (const auto& color : techData_.colors) {
        cout << "Color " << color.id << endl;
        for (const auto& param : color.parameters) {
            cout << "  " << param.first << " = " << param.second << endl;
        }
    }
}

void TechParser::printLayers() const {
    cout << "\n=== Layers ===" << endl;
    for (const auto& layer : techData_.layers) {
        cout << "Layer " << layer.name << endl;
        for (const auto& param : layer.parameters) {
            cout << "  " << param.first << " = " << param.second << endl;
        }
    }
}

void TechParser::printContacts() const {
    cout << "\n=== ContactCodes ===" << endl;
    for (const auto& contact : techData_.contacts) {
        cout << "ContactCode " << contact.name << endl;
        for (const auto& param : contact.parameters) {
            cout << "  " << param.first << " = " << param.second << endl;
        }
    }
}

bool TechParser::validateTechData() const {
    bool isValid = true;

    // Validate that we have essential sections
    if (techData_.layers.empty()) {
        const_cast<TechParser*>(this)->addWarning("No layers defined in technology file");
        isValid = false;
    }

    if (techData_.colors.empty()) {
        const_cast<TechParser*>(this)->addWarning("No colors defined in technology file");
    }

    // Validate layer parameters
    for (const auto& layer : techData_.layers) {
        if (layer.name.empty()) {
            const_cast<TechParser*>(this)->addWarning("Layer with empty name found");
            isValid = false;
        }
    }

    return isValid;
}

bool TechParser::writeTechFile(const string& filename) const {
    ofstream techFile(filename);
    if (!techFile.is_open()) {
        return false;
    }

    try {
        techFile << "# Technology file generated by TechParser" << endl;
        techFile << "# Date: " << __DATE__ << endl << endl;

        // Write Technology section
        techFile << "Technology {" << endl;
        for (const auto& param : techData_.tech.parameters) {
            techFile << "    " << param.first << " = " << param.second << endl;
        }
        techFile << "}" << endl << endl;

        // Write Colors
        for (const auto& color : techData_.colors) {
            techFile << "Color " << color.id << " {" << endl;
            for (const auto& param : color.parameters) {
                techFile << "    " << param.first << " = " << param.second << endl;
            }
            techFile << "}" << endl << endl;
        }

        // Write Layers
        for (const auto& layer : techData_.layers) {
            techFile << "Layer \"" << layer.name << "\" {" << endl;
            for (const auto& param : layer.parameters) {
                techFile << "    " << param.first << " = " << param.second << endl;
            }
            techFile << "}" << endl << endl;
        }

        // Write ContactCodes
        for (const auto& contact : techData_.contacts) {
            techFile << "ContactCode \"" << contact.name << "\" {" << endl;
            for (const auto& param : contact.parameters) {
                techFile << "    " << param.first << " = " << param.second << endl;
            }
            techFile << "}" << endl << endl;
        }

        // Write DesignRules
        for (const auto& rule : techData_.designRules) {
            techFile << "DesignRule {" << endl;
            for (const auto& param : rule.parameters) {
                techFile << "    " << param.first << " = " << param.second << endl;
            }
            techFile << "}" << endl << endl;
        }

        techFile.close();
        return true;
    }
    catch (const exception& e) {
        techFile.close();
        return false;
    }
}

bool TechParser::writeLayerMap(const string& filename) const {
    ofstream layerMap(filename);
    if (!layerMap.is_open()) {
        return false;
    }

    try {
        layerMap << "# Layer Mapping File" << endl;
        layerMap << "# Generated by TechParser" << endl << endl;

        layerMap << left << setw(20) << "Layer Name"
            << setw(15) << "Layer Type"
            << setw(10) << "GDS Layer"
            << "Description" << endl;
        layerMap << string(70, '-') << endl;

        for (const auto& layer : techData_.layers) {
            string layerType = "unknown";
            string gdsLayer = "N/A";
            string description = "";

            auto typeIt = layer.parameters.find("layerType");
            if (typeIt != layer.parameters.end()) {
                layerType = typeIt->second;
            }

            auto gdsIt = layer.parameters.find("gdsNumber");
            if (gdsIt != layer.parameters.end()) {
                gdsLayer = gdsIt->second;
            }

            auto descIt = layer.parameters.find("description");
            if (descIt != layer.parameters.end()) {
                description = descIt->second;
            }

            layerMap << left << setw(20) << layer.name
                << setw(15) << layerType
                << setw(10) << gdsLayer
                << description << endl;
        }

        layerMap.close();
        return true;
    }
    catch (const exception& e) {
        layerMap.close();
        return false;
    }
}

string TechParser::toString() const {
    ostringstream oss;

    oss << "Technology File Summary:" << endl;
    oss << "  Colors: " << techData_.colors.size() << endl;
    oss << "  Layers: " << techData_.layers.size() << endl;
    oss << "  Contacts: " << techData_.contacts.size() << endl;
    oss << "  Design Rules: " << techData_.designRules.size() << endl;

    return oss.str();
}

void TechParser::addError(const string& error) {
    errors_.push_back(error);
    cerr << "Tech Error: " << error << endl;
}

void TechParser::addWarning(const string& warning) {
    warnings_.push_back(warning);
    cout << "Tech Warning: " << warning << endl;
}

// Utility functions
namespace TechUtils {
    string removeQuotes(const string& str) {
        string result = str;
        if (!result.empty() && result.front() == '"' && result.back() == '"') {
            result = result.substr(1, result.size() - 2);
        }
        return result;
    }

    string extractSectionName(const string& line) {
        size_t start = line.find_first_of(" \t");
        if (start == string::npos) return "";

        size_t end = line.find('{');
        if (end == string::npos) return "";

        string name = line.substr(start, end - start);
        // Trim whitespace
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t") + 1);

        return removeQuotes(name);
    }

    int extractColorId(const string& line) {
        regex colorRegex(R"(Color\s+(\d+))");
        smatch match;
        if (regex_search(line, match, colorRegex)) {
            return stoi(match[1]);
        }
        return 0;
    }

    bool isValidParameterName(const string& name) {
        if (name.empty()) return false;

        // Parameter names should be alphanumeric with underscores
        for (char c : name) {
            if (!isalnum(c) && c != '_') return false;
        }

        return true;
    }

    bool isValidParameterValue(const string& value) {
        // Basic validation - non-empty
        return !value.empty();
    }

    string normalizeLayerName(const string& name) {
        string result = name;
        // Remove any quotes and normalize
        result = removeQuotes(result);
        return result;
    }
}