#include "ParserWeights.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

using namespace std;

WeightParser::WeightParser() : isLoaded_(false) {
    // Initialize default weights
    weights_.Alpha = 0.0;
    weights_.Beta = 0.0;
    weights_.Gamma = 0.0;
    weights_.TNS = 0.0;
    weights_.TPO = 0.0;
    weights_.Area = 0.0;
}

bool WeightParser::parseFile(const string& filename) {
    ifstream infile(filename);
    if (!infile.is_open()) {
        cerr << "Error: Cannot open weight file: " << filename << endl;
        return false;
    }

    try {
        string line;
        while (getline(infile, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') continue;

            istringstream iss(line);
            string key;
            double value;

            if (!(iss >> key >> value)) {
                // Skip malformed lines
                continue;
            }

            // Parse weight parameters
            if (key == "Alpha") {
                weights_.Alpha = value;
            }
            else if (key == "Beta") {
                weights_.Beta = value;
            }
            else if (key == "Gamma") {
                weights_.Gamma = value;
            }
            else if (key == "TNS") {
                weights_.TNS = value;
            }
            else if (key == "TPO") {
                weights_.TPO = value;
            }
            else if (key == "Area") {
                weights_.Area = value;
            }
            else {
                cerr << "Warning: Unknown weight parameter: " << key << endl;
            }
        }

        infile.close();
        isLoaded_ = true;
        return true;
    }
    catch (const exception& e) {
        cerr << "Error parsing weight file: " << e.what() << endl;
        infile.close();
        return false;
    }
}

bool WeightParser::parseFromString(const string& content) {
    try {
        istringstream iss(content);
        string line;

        while (getline(iss, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') continue;

            istringstream lineStream(line);
            string key;
            double value;

            if (!(lineStream >> key >> value)) {
                continue;
            }

            // Parse weight parameters
            if (key == "Alpha") {
                weights_.Alpha = value;
            }
            else if (key == "Beta") {
                weights_.Beta = value;
            }
            else if (key == "Gamma") {
                weights_.Gamma = value;
            }
            else if (key == "TNS") {
                weights_.TNS = value;
            }
            else if (key == "TPO") {
                weights_.TPO = value;
            }
            else if (key == "Area") {
                weights_.Area = value;
            }
        }

        isLoaded_ = true;
        return true;
    }
    catch (const exception& e) {
        cerr << "Error parsing weight string: " << e.what() << endl;
        return false;
    }
}

void WeightParser::clear() {
    weights_.Alpha = 0.0;
    weights_.Beta = 0.0;
    weights_.Gamma = 0.0;
    weights_.TNS = 0.0;
    weights_.TPO = 0.0;
    weights_.Area = 0.0;
    isLoaded_ = false;
}

void WeightParser::printWeights() const {
    cout << "\n=== Weight Parameters ===" << endl;
    cout << fixed << setprecision(6);
    cout << "Alpha: " << weights_.Alpha << endl;
    cout << "Beta:  " << weights_.Beta << endl;
    cout << "Gamma: " << weights_.Gamma << endl;
    cout << "TNS:   " << weights_.TNS << endl;
    cout << "TPO:   " << weights_.TPO << endl;
    cout << "Area:  " << weights_.Area << endl;
}

bool WeightParser::validateWeights() const {
    // Check for reasonable weight values
    const double MIN_WEIGHT = -1000.0;
    const double MAX_WEIGHT = 1000.0;

    bool isValid = true;

    if (weights_.Alpha < MIN_WEIGHT || weights_.Alpha > MAX_WEIGHT) {
        cerr << "Warning: Alpha weight out of reasonable range: " << weights_.Alpha << endl;
        isValid = false;
    }

    if (weights_.Beta < MIN_WEIGHT || weights_.Beta > MAX_WEIGHT) {
        cerr << "Warning: Beta weight out of reasonable range: " << weights_.Beta << endl;
        isValid = false;
    }

    if (weights_.Gamma < MIN_WEIGHT || weights_.Gamma > MAX_WEIGHT) {
        cerr << "Warning: Gamma weight out of reasonable range: " << weights_.Gamma << endl;
        isValid = false;
    }

    if (weights_.TNS < MIN_WEIGHT || weights_.TNS > MAX_WEIGHT) {
        cerr << "Warning: TNS weight out of reasonable range: " << weights_.TNS << endl;
        isValid = false;
    }

    if (weights_.TPO < MIN_WEIGHT || weights_.TPO > MAX_WEIGHT) {
        cerr << "Warning: TPO weight out of reasonable range: " << weights_.TPO << endl;
        isValid = false;
    }

    if (weights_.Area < MIN_WEIGHT || weights_.Area > MAX_WEIGHT) {
        cerr << "Warning: Area weight out of reasonable range: " << weights_.Area << endl;
        isValid = false;
    }

    return isValid;
}

bool WeightParser::saveToFile(const string& filename) const {
    ofstream outfile(filename);
    if (!outfile.is_open()) {
        cerr << "Error: Cannot create weight file: " << filename << endl;
        return false;
    }

    try {
        outfile << fixed << setprecision(6);
        outfile << "# Weight Parameters" << endl;
        outfile << "Alpha " << weights_.Alpha << endl;
        outfile << "Beta " << weights_.Beta << endl;
        outfile << "Gamma " << weights_.Gamma << endl;
        outfile << "TNS " << weights_.TNS << endl;
        outfile << "TPO " << weights_.TPO << endl;
        outfile << "Area " << weights_.Area << endl;

        outfile.close();
        return true;
    }
    catch (const exception& e) {
        cerr << "Error writing weight file: " << e.what() << endl;
        outfile.close();
        return false;
    }
}

string WeightParser::toString() const {
    ostringstream oss;
    oss << fixed << setprecision(6);
    oss << "Alpha " << weights_.Alpha << "\n";
    oss << "Beta " << weights_.Beta << "\n";
    oss << "Gamma " << weights_.Gamma << "\n";
    oss << "TNS " << weights_.TNS << "\n";
    oss << "TPO " << weights_.TPO << "\n";
    oss << "Area " << weights_.Area << "\n";
    return oss.str();
}