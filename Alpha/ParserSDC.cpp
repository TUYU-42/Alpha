#include "ParserSDC.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>

using namespace std;

SdcParser::SdcParser() : isLoaded_(false) {
    // Initialize regex patterns
    createClockRegex_ = regex(R"(create_clock\s+(.+))");
    setLoadRegex_ = regex(R"(set_load\s+(.+))");
    setDelayRegex_ = regex(R"(set_(input|output)_delay\s+(.+))");
    setConstraintRegex_ = regex(R"(set_(max_transition|max_capacitance|clock_latency|clock_uncertainty)\s+(\S+)\s+(.+))");
    optionRegex_ = regex(R"(-(\S+)\s+(\S+))");
}

bool SdcParser::parseFile(const string& filename) {
    ifstream sdcFile(filename);
    if (!sdcFile.is_open()) {
        addError("Cannot open SDC file: " + filename);
        return false;
    }

    try {
        clear();
        string line;

        while (getline(sdcFile, line)) {
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            if (line.empty() || line[0] == '#') continue;

            if (!parseCommand(line)) {
                addWarning("Failed to parse SDC command: " + line);
            }
        }

        sdcFile.close();

        // Post-processing
        extractClocks();
        extractConstraints();

        isLoaded_ = true;
        return true;
    }
    catch (const exception& e) {
        addError("Error parsing SDC file: " + string(e.what()));
        sdcFile.close();
        return false;
    }
}

bool SdcParser::parseFromString(const string& content) {
    try {
        clear();
        istringstream iss(content);
        string line;

        while (getline(iss, line)) {
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            if (line.empty() || line[0] == '#') continue;

            if (!parseCommand(line)) {
                addWarning("Failed to parse SDC command: " + line);
            }
        }

        extractClocks();
        extractConstraints();
        isLoaded_ = true;
        return true;
    }
    catch (const exception& e) {
        addError("Error parsing SDC string: " + string(e.what()));
        return false;
    }
}

bool SdcParser::parseCommand(const string& line) {
    SdcCommand cmd;
    cmd.raw_line = line;

    smatch match;

    if (regex_search(line, match, createClockRegex_)) {
        cmd.command_type = "create_clock";
        return parseCreateClock(match[1], cmd);
    }
    else if (line.find("set_input_delay") != string::npos) {
        cmd.command_type = "set_input_delay";
        return parseSetInputDelay(line.substr(line.find("set_input_delay") + 15), cmd);
    }
    else if (line.find("set_output_delay") != string::npos) {
        cmd.command_type = "set_output_delay";
        return parseSetOutputDelay(line.substr(line.find("set_output_delay") + 16), cmd);
    }
    else if (line.find("set_load") != string::npos) {
        cmd.command_type = "set_load";
        return parseSetLoad(line.substr(line.find("set_load") + 8), cmd);
    }
    else if (line.find("set_clock_latency") != string::npos) {
        cmd.command_type = "set_clock_latency";
        return parseSetClockLatency(line.substr(line.find("set_clock_latency") + 17), cmd);
    }
    else if (line.find("set_clock_uncertainty") != string::npos) {
        cmd.command_type = "set_clock_uncertainty";
        return parseSetClockUncertainty(line.substr(line.find("set_clock_uncertainty") + 21), cmd);
    }
    else if (line.find("set_max_transition") != string::npos) {
        cmd.command_type = "set_max_transition";
        return parseSetMaxTransition(line.substr(line.find("set_max_transition") + 18), cmd);
    }
    else if (line.find("set_max_capacitance") != string::npos) {
        cmd.command_type = "set_max_capacitance";
        return parseSetMaxCapacitance(line.substr(line.find("set_max_capacitance") + 19), cmd);
    }
    else if (line.find("set_units") != string::npos) {
        cmd.command_type = "set_units";
        return parseSetUnits(line.substr(line.find("set_units") + 9), cmd);
    }
    else if (line.find("set ") == 0) {
        cmd.command_type = "set";
        return parseSet(line.substr(4), cmd);
    }
    else {
        cmd.command_type = "unknown";
        commands_.push_back(cmd);
        return true;
    }
}

bool SdcParser::parseCreateClock(const string& args, SdcCommand& cmd) {
    cmd.parameters["raw_args"] = args;

    // Parse create_clock options
    auto options = SdcUtils::parseOptions(args);
    for (const auto& option : options) {
        cmd.parameters[option.first] = option.second;
    }

    commands_.push_back(cmd);
    return true;
}

bool SdcParser::parseSetInputDelay(const string& args, SdcCommand& cmd) {
    cmd.parameters["raw_args"] = args;

    // Parse input delay options
    auto options = SdcUtils::parseOptions(args);
    for (const auto& option : options) {
        cmd.parameters[option.first] = option.second;
    }

    commands_.push_back(cmd);
    return true;
}

bool SdcParser::parseSetOutputDelay(const string& args, SdcCommand& cmd) {
    cmd.parameters["raw_args"] = args;

    // Parse output delay options
    auto options = SdcUtils::parseOptions(args);
    for (const auto& option : options) {
        cmd.parameters[option.first] = option.second;
    }

    commands_.push_back(cmd);
    return true;
}

bool SdcParser::parseSetLoad(const string& args, SdcCommand& cmd) {
    cmd.parameters["raw_args"] = args;

    // Parse load options
    auto options = SdcUtils::parseOptions(args);
    for (const auto& option : options) {
        cmd.parameters[option.first] = option.second;
    }

    commands_.push_back(cmd);
    return true;
}

bool SdcParser::parseSetClockLatency(const string& args, SdcCommand& cmd) {
    istringstream iss(args);
    string value;
    if (iss >> value) {
        cmd.parameters["value"] = value;
    }

    commands_.push_back(cmd);
    return true;
}

bool SdcParser::parseSetClockUncertainty(const string& args, SdcCommand& cmd) {
    istringstream iss(args);
    string value;
    if (iss >> value) {
        cmd.parameters["value"] = value;
    }

    commands_.push_back(cmd);
    return true;
}

bool SdcParser::parseSetMaxTransition(const string& args, SdcCommand& cmd) {
    istringstream iss(args);
    string value;
    if (iss >> value) {
        cmd.parameters["value"] = value;
    }

    commands_.push_back(cmd);
    return true;
}

bool SdcParser::parseSetMaxCapacitance(const string& args, SdcCommand& cmd) {
    istringstream iss(args);
    string value;
    if (iss >> value) {
        cmd.parameters["value"] = value;
    }

    commands_.push_back(cmd);
    return true;
}

bool SdcParser::parseSetUnits(const string& args, SdcCommand& cmd) {
    auto options = SdcUtils::parseOptions(args);
    for (const auto& option : options) {
        cmd.parameters[option.first] = option.second;
    }

    commands_.push_back(cmd);
    return true;
}

bool SdcParser::parseSet(const string& args, SdcCommand& cmd) {
    istringstream iss(args);
    string name, value;
    if (iss >> name >> value) {
        cmd.parameters["name"] = name;
        cmd.parameters["value"] = value;
    }

    commands_.push_back(cmd);
    return true;
}

void SdcParser::extractClocks() {
    clocks_.clear();

    for (const auto& cmd : commands_) {
        if (cmd.command_type == "create_clock") {
            SdcClock clock;

            // Extract clock parameters from command
            auto periodIt = cmd.parameters.find("period");
            if (periodIt != cmd.parameters.end()) {
                clock.period = SdcUtils::parseTimeValue(periodIt->second);
            }

            auto nameIt = cmd.parameters.find("name");
            if (nameIt != cmd.parameters.end()) {
                clock.name = nameIt->second;
            }
            else {
                clock.name = "clk_" + to_string(clocks_.size());
            }

            clocks_.push_back(clock);
        }
    }
}

void SdcParser::extractConstraints() {
    constraints_.clear();

    for (const auto& cmd : commands_) {
        SdcConstraint constraint;
        constraint.type = cmd.command_type;

        auto valueIt = cmd.parameters.find("value");
        if (valueIt != cmd.parameters.end()) {
            constraint.value = SdcUtils::parseTimeValue(valueIt->second);
        }

        // Extract target from various parameter fields
        for (const auto& param : cmd.parameters) {
            if (param.first != "value" && param.first != "raw_args") {
                constraint.options[param.first] = param.second;
            }
        }

        constraints_.push_back(constraint);
    }
}

const SdcClock* SdcParser::findClock(const string& name) const {
    auto it = find_if(clocks_.begin(), clocks_.end(),
        [&name](const SdcClock& clock) { return clock.name == name; });
    return (it != clocks_.end()) ? &(*it) : nullptr;
}

vector<SdcConstraint> SdcParser::getConstraintsOfType(const string& type) const {
    vector<SdcConstraint> result;
    copy_if(constraints_.begin(), constraints_.end(), back_inserter(result),
        [&type](const SdcConstraint& constraint) { return constraint.type == type; });
    return result;
}

double SdcParser::getClockPeriod(const string& clockName) const {
    const SdcClock* clock = findClock(clockName);
    return clock ? clock->period : 0.0;
}

double SdcParser::getInputDelay(const string& port) const {
    for (const auto& constraint : constraints_) {
        if (constraint.type == "set_input_delay") {
            auto targetIt = constraint.options.find("target");
            if (targetIt != constraint.options.end() && targetIt->second == port) {
                return constraint.value;
            }
        }
    }
    return 0.0;
}

double SdcParser::getOutputDelay(const string& port) const {
    for (const auto& constraint : constraints_) {
        if (constraint.type == "set_output_delay") {
            auto targetIt = constraint.options.find("target");
            if (targetIt != constraint.options.end() && targetIt->second == port) {
                return constraint.value;
            }
        }
    }
    return 0.0;
}

map<string, double> SdcParser::getClockFrequencies() const {
    map<string, double> frequencies;
    for (const auto& clock : clocks_) {
        if (clock.period > 0.0) {
            frequencies[clock.name] = 1.0 / clock.period;
        }
    }
    return frequencies;
}

void SdcParser::clear() {
    commands_.clear();
    clocks_.clear();
    constraints_.clear();
    timingPaths_.clear();
    errors_.clear();
    warnings_.clear();
    isLoaded_ = false;
}

void SdcParser::printSummary() const {
    cout << "\n=== SDC Parser Summary ===" << endl;
    cout << "Commands: " << commands_.size() << endl;
    cout << "Clocks: " << clocks_.size() << endl;
    cout << "Constraints: " << constraints_.size() << endl;
    cout << "Timing Paths: " << timingPaths_.size() << endl;
}

void SdcParser::printCommands() const {
    cout << "\n=== SDC Commands ===" << endl;
    for (const auto& cmd : commands_) {
        cout << "Command Type: " << cmd.command_type << endl;
        cout << "  Raw Line: " << cmd.raw_line << endl;
        for (const auto& param : cmd.parameters) {
            cout << "    " << param.first << " : " << param.second << endl;
        }
    }
}

void SdcParser::printClocks() const {
    cout << "\n=== SDC Clocks ===" << endl;
    for (const auto& clock : clocks_) {
        cout << "Clock: " << clock.name << endl;
        cout << "  Period: " << clock.period << " ns" << endl;
        cout << "  Frequency: " << (clock.period > 0 ? 1.0 / clock.period : 0.0) << " GHz" << endl;
    }
}

void SdcParser::printConstraints() const {
    cout << "\n=== SDC Constraints ===" << endl;
    for (const auto& constraint : constraints_) {
        cout << "Constraint: " << constraint.type << endl;
        cout << "  Value: " << constraint.value << endl;
        cout << "  Target: " << constraint.target << endl;
        for (const auto& option : constraint.options) {
            cout << "    " << option.first << ": " << option.second << endl;
        }
    }
}

bool SdcParser::writeSdcFile(const string& filename) const {
    ofstream sdcFile(filename);
    if (!sdcFile.is_open()) {
        return false;
    }

    try {
        sdcFile << "# SDC file generated by SdcParser" << endl;
        sdcFile << "# Date: " << __DATE__ << endl << endl;

        for (const auto& cmd : commands_) {
            sdcFile << cmd.raw_line << endl;
        }

        sdcFile.close();
        return true;
    }
    catch (const exception& e) {
        sdcFile.close();
        return false;
    }
}

void SdcParser::addError(const string& error) {
    errors_.push_back(error);
    cerr << "SDC Error: " << error << endl;
}

void SdcParser::addWarning(const string& warning) {
    warnings_.push_back(warning);
    cout << "SDC Warning: " << warning << endl;
}

// Utility functions
namespace SdcUtils {
    vector<string> parseArgumentList(const string& args) {
        vector<string> result;
        istringstream iss(args);
        string arg;

        while (iss >> arg) {
            result.push_back(arg);
        }

        return result;
    }

    map<string, string> parseOptions(const string& args) {
        map<string, string> options;
        regex optionRegex(R"(-(\S+)\s+(\S+))");

        sregex_iterator it(args.begin(), args.end(), optionRegex);
        sregex_iterator end;

        for (; it != end; ++it) {
            options[(*it)[1]] = (*it)[2];
        }

        return options;
    }

    double parseTimeValue(const string& timeStr) {
        try {
            return stod(timeStr);
        }
        catch (const exception& e) {
            return 0.0;
        }
    }

    string extractPortName(const string& portExpr) {
        // Remove brackets and get port name
        string result = portExpr;
        size_t start = result.find('[');
        if (start != string::npos) {
            size_t end = result.find(']', start);
            if (end != string::npos) {
                result = result.substr(start + 1, end - start - 1);
            }
        }
        return result;
    }

    string extractClockName(const string& clockExpr) {
        // Extract clock name from expression
        return clockExpr;
    }

    bool isValidConstraintValue(double value, const string& type) {
        // Basic validation for constraint values
        if (type == "period" || type == "delay") {
            return value > 0.0;
        }
        return true;
    }
}