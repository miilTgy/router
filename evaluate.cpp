#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

string trim(const string& text) {
    size_t start = 0;
    while (start < text.size() && isspace(static_cast<unsigned char>(text[start]))) {
        start++;
    }

    size_t end = text.size();
    while (end > start && isspace(static_cast<unsigned char>(text[end - 1]))) {
        end--;
    }

    return text.substr(start, end - start);
}

struct Point {
    int x, y;

    Point(int _x = 0, int _y = 0) : x(_x), y(_y) {}

    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }

    bool operator<(const Point& other) const {
        if (x != other.x) return x < other.x;
        return y < other.y;
    }
};

bool parsePointText(const string& text, Point& point) {
    size_t commaPos = text.find(',');
    if (commaPos == string::npos) {
        return false;
    }

    string xText = trim(text.substr(0, commaPos));
    string yText = trim(text.substr(commaPos + 1));
    if (xText.empty() || yText.empty()) {
        return false;
    }

    try {
        point.x = stoi(xText);
        point.y = stoi(yText);
    } catch (...) {
        return false;
    }

    return true;
}

namespace std {
    template<>
    struct hash<Point> {
        size_t operator()(const Point& p) const {
            return hash<int>()(p.x) ^ hash<int>()(p.y);
        }
    };
}

struct Net {
    int id;
    vector<Point> pins;
};

struct Edge {
    Point p1, p2;

    Edge(const Point& _p1, const Point& _p2) {
        if (_p1 < _p2) {
            p1 = _p1;
            p2 = _p2;
        } else {
            p1 = _p2;
            p2 = _p1;
        }
    }

    bool isVertical() const {
        return p1.x == p2.x;
    }

    bool isHorizontal() const {
        return p1.y == p2.y;
    }

    bool operator<(const Edge& other) const {
        if (p1 < other.p1) return true;
        if (other.p1 < p1) return false;
        return p2 < other.p2;
    }
};

int encodePoint(int x, int y, int gridY) {
    return x * gridY + y;
}

bool areAllPinsConnected(const vector<Point>& pins, const vector<Edge>& connections) {
    if (pins.empty()) return true;
    if (pins.size() == 1) return true;

    map<Point, vector<Point>> graph;
    for (const Edge& edge : connections) {
        graph[edge.p1].push_back(edge.p2);
        graph[edge.p2].push_back(edge.p1);
    }

    for (const Point& pin : pins) {
        if (graph.find(pin) == graph.end()) {
            return false;
        }
    }

    unordered_set<Point> visited;
    queue<Point> q;
    q.push(pins[0]);
    visited.insert(pins[0]);

    while (!q.empty()) {
        Point current = q.front();
        q.pop();

        for (const Point& neighbor : graph[current]) {
            if (visited.find(neighbor) == visited.end()) {
                visited.insert(neighbor);
                q.push(neighbor);
            }
        }
    }

    for (const Point& pin : pins) {
        if (visited.find(pin) == visited.end()) {
            return false;
        }
    }

    return true;
}

bool parseInputFile(const string& filename, int& gridX, int& gridY,
                    int& verticalCapacity, int& horizontalCapacity,
                    unordered_set<int>& blockedGrids,
                    vector<Net>& nets) {
    ifstream infile(filename);
    if (!infile) {
        cerr << "Cannot open input file: " << filename << endl;
        return false;
    }

    string line;
    getline(infile, line);
    istringstream iss(line);
    string tmp;
    iss >> tmp >> gridX >> gridY;

    getline(infile, line);
    iss.clear();
    iss.str(line);
    iss >> tmp >> tmp >> verticalCapacity;

    getline(infile, line);
    iss.clear();
    iss.str(line);
    iss >> tmp >> tmp >> horizontalCapacity;

    getline(infile, line);
    line = trim(line);

    if (line.rfind("num block", 0) == 0) {
        iss.clear();
        iss.str(line);
        int numBlocks = 0;
        iss >> tmp >> tmp >> numBlocks;
        for (int i = 0; i < numBlocks; i++) {
            getline(infile, line);
            iss.clear();
            iss.str(line);
            string blockToken;
            int x, y;
            iss >> blockToken >> x >> y;
            blockedGrids.insert(encodePoint(x, y, gridY));
        }
        getline(infile, line);
        line = trim(line);
    }

    iss.clear();
    iss.str(line);
    int numNets;
    iss >> tmp >> tmp >> numNets;

    for (int i = 0; i < numNets; i++) {
        getline(infile, line);
        iss.clear();
        iss.str(line);
        string netName;
        int numPins;
        iss >> netName >> numPins;

        Net net;
        net.id = stoi(netName.substr(3));

        for (int j = 0; j < numPins; j++) {
            getline(infile, line);
            iss.clear();
            iss.str(line);
            int x, y;
            iss >> x >> y;
            if (blockedGrids.find(encodePoint(x, y, gridY)) != blockedGrids.end()) {
                cerr << "Input pin lies on blocked grid: (" << x << ", " << y << ")" << endl;
                return false;
            }
            net.pins.push_back(Point(x, y));
        }

        nets.push_back(net);
    }

    infile.close();
    return true;
}

bool parseOutputFile(const string& filename, map<int, vector<Edge>>& netConnections) {
    ifstream infile(filename);
    if (!infile) {
        cerr << "Cannot open output file: " << filename << endl;
        return false;
    }

    string line;
    int currentNetId = -1;

    while (getline(infile, line)) {
        if (line.empty()) continue;

        if (line[0] == 'n') {
            string netName = line;
            currentNetId = stoi(netName.substr(3));
        } else if (line[0] == '!') {
            currentNetId = -1;
        } else if (currentNetId != -1) {
            size_t pos1 = line.find('(');
            size_t pos2 = line.find(')');
            size_t pos3 = line.find('(', pos2);
            size_t pos4 = line.find(')', pos3);

            if (pos1 != string::npos && pos2 != string::npos &&
                pos3 != string::npos && pos4 != string::npos) {
                string point1 = line.substr(pos1 + 1, pos2 - pos1 - 1);
                string point2 = line.substr(pos3 + 1, pos4 - pos3 - 1);

                Point p1, p2;
                if (!parsePointText(point1, p1) || !parsePointText(point2, p2)) {
                    cerr << "Invalid edge format in output file: " << line << endl;
                    return false;
                }

                netConnections[currentNetId].push_back(Edge(p1, p2));
            } else {
                cerr << "Invalid line in output file: " << line << endl;
                return false;
            }
        }
    }

    infile.close();
    return true;
}

bool checkAllNetsIncluded(const vector<Net>& nets, const map<int, vector<Edge>>& netConnections) {
    for (const Net& net : nets) {
        if (netConnections.find(net.id) == netConnections.end()) {
            cout << "Network " << net.id << " is missing in the output file" << endl;
            return false;
        }
    }
    return true;
}

bool checkAllPinsConnected(const vector<Net>& nets, const map<int, vector<Edge>>& netConnections) {
    for (const Net& net : nets) {
        if (netConnections.find(net.id) == netConnections.end()) {
            return false;
        }

        const vector<Edge>& connections = netConnections.at(net.id);
        if (!areAllPinsConnected(net.pins, connections)) {
            cout << "Network " << net.id << " has pins that are not fully connected" << endl;
            return false;
        }
    }
    return true;
}

int calculateWireLength(const map<int, vector<Edge>>& netConnections) {
    int totalLength = 0;

    for (const auto& pair : netConnections) {
        const vector<Edge>& connections = pair.second;
        for (const Edge& edge : connections) {
            totalLength += abs(edge.p1.x - edge.p2.x) + abs(edge.p1.y - edge.p2.y);
        }
    }

    return totalLength;
}

int checkCapacityViolations(const map<int, vector<Edge>>& netConnections,
                            int gridX, int gridY,
                            int verticalCapacity, int horizontalCapacity) {
    map<Edge, int> edgeUsage;

    for (const auto& pair : netConnections) {
        const vector<Edge>& connections = pair.second;
        for (const Edge& edge : connections) {
            edgeUsage[edge]++;
        }
    }

    set<pair<int, int>> violatedGrids;

    for (const auto& pair : edgeUsage) {
        const Edge& edge = pair.first;
        int usage = pair.second;

        if (edge.isVertical()) {
            if (usage > verticalCapacity) {
                int x = edge.p1.x;
                int y = min(edge.p1.y, edge.p2.y);
                violatedGrids.insert({x, y});
            }
        } else if (edge.isHorizontal()) {
            if (usage > horizontalCapacity) {
                int x = min(edge.p1.x, edge.p2.x);
                int y = edge.p1.y;
                violatedGrids.insert({x, y});
            }
        }
    }

    return violatedGrids.size();
}

bool checkEdgeLegality(const map<int, vector<Edge>>& netConnections,
                       int gridX, int gridY,
                       const unordered_set<int>& blockedGrids) {
    for (const auto& pair : netConnections) {
        int netId = pair.first;
        const vector<Edge>& connections = pair.second;

        for (const Edge& edge : connections) {
            const Point points[2] = {edge.p1, edge.p2};
            for (const Point& point : points) {
                if (point.x < 0 || point.x >= gridX || point.y < 0 || point.y >= gridY) {
                    cout << "Network " << netId << " has out-of-bound point: ("
                         << point.x << ", " << point.y << ")" << endl;
                    return false;
                }
                if (blockedGrids.find(encodePoint(point.x, point.y, gridY)) != blockedGrids.end()) {
                    cout << "Network " << netId << " uses blocked grid: ("
                         << point.x << ", " << point.y << ")" << endl;
                    return false;
                }
            }

            int distance = abs(edge.p1.x - edge.p2.x) + abs(edge.p1.y - edge.p2.y);
            if (distance != 1) {
                cout << "Network " << netId << " has non-adjacent edge: ("
                     << edge.p1.x << ", " << edge.p1.y << ")-("
                     << edge.p2.x << ", " << edge.p2.y << ")" << endl;
                return false;
            }
        }
    }

    return true;
}

int main(int argc, char* argv[]) {
    string inputFilename = "xx/samplex.txt";
    string outputFilename = "xx/samplex_solution.txt";

    if (argc >= 3) {
        inputFilename = argv[1];
        outputFilename = argv[2];
    }

    int gridX, gridY;
    int verticalCapacity, horizontalCapacity;
    unordered_set<int> blockedGrids;
    vector<Net> nets;
    map<int, vector<Edge>> netConnections;

    if (!parseInputFile(inputFilename, gridX, gridY, verticalCapacity, horizontalCapacity, blockedGrids, nets)) {
        return 1;
    }

    if (!parseOutputFile(outputFilename, netConnections)) {
        return 1;
    }

    bool allEdgesLegal = checkEdgeLegality(netConnections, gridX, gridY, blockedGrids);
    cout << "0. Edge legality check: " << (allEdgesLegal ? "Passed" : "Failed") << endl;
    if (!allEdgesLegal) {
        return 1;
    }

    bool allNetsIncluded = checkAllNetsIncluded(nets, netConnections);
    cout << "1. Output file includes all nets: " << (allNetsIncluded ? "Yes" : "No") << endl;

    bool allPinsConnected = checkAllPinsConnected(nets, netConnections);
    cout << "2. All pins connection check: " << (allPinsConnected ? "Passed" : "Failed") << endl;

    int wireLength = calculateWireLength(netConnections);
    cout << "3. Total wire length: " << wireLength << endl;

    int numViolatedGrids = checkCapacityViolations(netConnections, gridX, gridY,
                                                   verticalCapacity, horizontalCapacity);
    cout << "4. Number of grids with capacity violations: " << numViolatedGrids << endl;

    return 0;
}