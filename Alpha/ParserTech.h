#ifndef PARSER_TECH_H
#define PARSER_TECH_H

#include "DataStructures.h"
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <memory>

class TechParser {
private:
    TechData techData_;
    bool isLoaded_;
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;

    // Helper methods
    bool parseSection(const std::string& content, size_t& pos);
    bool parseTechnology(const std::string& content, size_t& pos);
    bool parseColor(const std::string& content, size_t& pos, int colorId);
    bool parseLayer(const std::string& content, size_t& pos, const std::string& layerName);
    bool parseContactCode(const std::string& content, size_t& pos, const std::string& contactName);
    bool parseDesignRule(const std::string& content, size_t& pos);
    bool parsePRRule(const std::string& content, size_t& pos);
    bool parseDensityRule(const std::string& content, size_t& pos);
    bool parseLayerDataType(const std::string& content, size_t& pos, const std::string& typeName);

    bool parseKeyValuePairs(const std::string& content, size_t start, size_t end,
        std::map<std::string, std::string>& parameters);

    void addError(const std::string& error);
    void addWarning(const std::string& warning);

    // Regex patterns
    std::regex sectionStartRegex_;
    std::regex keyValueRegex_;
    std::regex blockEndRegex_;

public:
    // Constructor & Destructor
    TechParser();
    ~TechParser() = default;

    // Copy/Move constructors
    TechParser(const TechParser&) = delete;
    TechParser& operator=(const TechParser&) = delete;
    TechParser(TechParser&&) = default;
    TechParser& operator=(TechParser&&) = default;

    // Main interface methods
    bool parseFile(const std::string& filename);
    bool parseFromString(const std::string& content);

    // Data access methods
    const TechData& getTechData() const { return techData_; }
    TechData& getTechData() { return techData_; }
    bool isLoaded() const { return isLoaded_; }

    // Component access methods
    const Technology& getTechnology() const { return techData_.tech; }
    const std::vector<Color>& getColors() const { return techData_.colors; }
    const std::vector<Layer>& getLayers() const { return techData_.layers; }
    const std::vector<ContactCode>& getContacts() const { return techData_.contacts; }
    const std::vector<DesignRule>& getDesignRules() const { return techData_.designRules; }
    const std::vector<PRRule>& getPRRules() const { return techData_.prRules; }
    const std::vector<DensityRule>& getDensityRules() const { return techData_.densityRules; }
    const std::vector<LayerDataType>& getLayerDataTypes() const { return techData_.layerDataTypes; }

    // Query methods
    const Color* findColor(int id) const;
    const Layer* findLayer(const std::string& name) const;
    const ContactCode* findContact(const std::string& name) const;
    const LayerDataType* findLayerDataType(const std::string& name) const;
    std::string getTechParameter(const std::string& key) const;

    // Statistics methods
    size_t getColorCount() const { return techData_.colors.size(); }
    size_t getLayerCount() const { return techData_.layers.size(); }
    size_t getContactCount() const { return techData_.contacts.size(); }
    size_t getDesignRuleCount() const { return techData_.designRules.size(); }
    size_t getPRRuleCount() const { return techData_.prRules.size(); }
    size_t getDensityRuleCount() const { return techData_.densityRules.size(); }
    size_t getLayerDataTypeCount() const { return techData_.layerDataTypes.size(); }

    // Analysis methods
    void analyzeTechFile();
    std::vector<std::string> getMetalLayers() const;
    std::vector<std::string> getViaLayers() const;
    std::vector<std::string> getAllRoutingLayers() const;
    std::map<std::string, std::string> getProcessParameters() const;

    // Utility methods
    void clear();
    void printSummary() const;
    void printTechData() const;
    void printLayers() const;
    void printColors() const;
    void printContacts() const;
    bool validateTechData() const;

    // Error handling
    const std::vector<std::string>& getErrors() const { return errors_; }
    const std::vector<std::string>& getWarnings() const { return warnings_; }
    bool hasErrors() const { return !errors_.empty(); }
    bool hasWarnings() const { return !warnings_.empty(); }

    // Export methods
    bool writeTechFile(const std::string& filename) const;
    bool writeLayerMap(const std::string& filename) const;
    std::string toString() const;
};

// Utility functions for Technology file parsing
namespace TechUtils {
    std::string removeQuotes(const std::string& str);
    std::string extractSectionName(const std::string& line);
    int extractColorId(const std::string& line);
    bool isValidParameterName(const std::string& name);
    bool isValidParameterValue(const std::string& value);
    std::string normalizeLayerName(const std::string& name);
}

#endif // PARSER_TECH_H