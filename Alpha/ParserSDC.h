#ifndef PARSER_SDC_H
#define PARSER_SDC_H

#include "DataStructures.h"
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <memory>

// SDC-specific data structures
struct SdcClock {
    std::string name;
    double period;
    std::vector<std::string> sources;
    double waveform[2] = { 0.0, 0.0 }; // rise, fall
};

struct SdcConstraint {
    std::string type;
    double value;
    std::string target;
    std::map<std::string, std::string> options;
};

struct SdcTimingPath {
    std::string from;
    std::string to;
    double delay;
    std::string pathType; // setup, hold, etc.
};

class SdcParser {
private:
    std::vector<SdcCommand> commands_;
    std::vector<SdcClock> clocks_;
    std::vector<SdcConstraint> constraints_;
    std::vector<SdcTimingPath> timingPaths_;
    bool isLoaded_;
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;

    // Helper methods
    bool parseCommand(const std::string& line);
    bool parseCreateClock(const std::string& args, SdcCommand& cmd);
    bool parseSetInputDelay(const std::string& args, SdcCommand& cmd);
    bool parseSetOutputDelay(const std::string& args, SdcCommand& cmd);
    bool parseSetLoad(const std::string& args, SdcCommand& cmd);
    bool parseSetClockLatency(const std::string& args, SdcCommand& cmd);
    bool parseSetClockUncertainty(const std::string& args, SdcCommand& cmd);
    bool parseSetMaxTransition(const std::string& args, SdcCommand& cmd);
    bool parseSetMaxCapacitance(const std::string& args, SdcCommand& cmd);
    bool parseSetUnits(const std::string& args, SdcCommand& cmd);
    bool parseSet(const std::string& args, SdcCommand& cmd);

    void extractClocks();
    void extractConstraints();
    void addError(const std::string& error);
    void addWarning(const std::string& warning);

    // Regex patterns
    std::regex createClockRegex_;
    std::regex setLoadRegex_;
    std::regex setDelayRegex_;
    std::regex setConstraintRegex_;
    std::regex optionRegex_;

public:
    // Constructor & Destructor
    SdcParser();
    ~SdcParser() = default;

    // Copy/Move constructors
    SdcParser(const SdcParser&) = delete;
    SdcParser& operator=(const SdcParser&) = delete;
    SdcParser(SdcParser&&) = default;
    SdcParser& operator=(SdcParser&&) = default;

    // Main interface methods
    bool parseFile(const std::string& filename);
    bool parseFromString(const std::string& content);

    // Data access methods
    const std::vector<SdcCommand>& getCommands() const { return commands_; }
    const std::vector<SdcClock>& getClocks() const { return clocks_; }
    const std::vector<SdcConstraint>& getConstraints() const { return constraints_; }
    const std::vector<SdcTimingPath>& getTimingPaths() const { return timingPaths_; }
    bool isLoaded() const { return isLoaded_; }

    // Query methods
    const SdcClock* findClock(const std::string& name) const;
    std::vector<SdcConstraint> getConstraintsOfType(const std::string& type) const;
    double getClockPeriod(const std::string& clockName) const;
    double getInputDelay(const std::string& port) const;
    double getOutputDelay(const std::string& port) const;

    // Statistics methods
    size_t getCommandCount() const { return commands_.size(); }
    size_t getClockCount() const { return clocks_.size(); }
    size_t getConstraintCount() const { return constraints_.size(); }
    size_t getTimingPathCount() const { return timingPaths_.size(); }

    // Analysis methods
    void analyzeTimingConstraints();
    void extractTimingPaths();
    std::map<std::string, double> getClockFrequencies() const;
    std::vector<std::string> getCriticalPaths() const;

    // Utility methods
    void clear();
    void printSummary() const;
    void printCommands() const;
    void printClocks() const;
    void printConstraints() const;
    bool validateConstraints() const;

    // Error handling
    const std::vector<std::string>& getErrors() const { return errors_; }
    const std::vector<std::string>& getWarnings() const { return warnings_; }
    bool hasErrors() const { return !errors_.empty(); }
    bool hasWarnings() const { return !warnings_.empty(); }

    // Export methods
    bool writeSdcFile(const std::string& filename) const;
    bool writeTimingReport(const std::string& filename) const;
    std::string toString() const;
};

// Utility functions for SDC parsing
namespace SdcUtils {
    std::vector<std::string> parseArgumentList(const std::string& args);
    std::map<std::string, std::string> parseOptions(const std::string& args);
    double parseTimeValue(const std::string& timeStr);
    std::string extractPortName(const std::string& portExpr);
    std::string extractClockName(const std::string& clockExpr);
    bool isValidConstraintValue(double value, const std::string& type);
}

#endif // PARSER_SDC_H