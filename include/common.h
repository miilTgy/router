#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

struct Point {
    int r;
    int c;

    bool operator==(const Point& other) const {
        return r == other.r && c == other.c;
    }

    bool operator<(const Point& other) const {
        if (r != other.r) return r < other.r;
        return c < other.c;
    }
};

struct Net {
    std::string name;
    int id = -1;
    std::vector<Point> pins;
};

struct Problem {
    int rows = 0;
    int cols = 0;

    int vertical_capacity = 0;
    int horizontal_capacity = 0;

    std::vector<Point> blocks;
    std::vector<Net> nets;

    std::vector<uint8_t> blocked;
};

enum class Dir : uint8_t {
    kUp = 0,
    kDown = 1,
    kLeft = 2,
    kRight = 3,
};

struct EdgeState {
    int usage = 0;
    double base_cost = 1.0;
    double present_cost = 1.0;
    double history_cost = 1.0;
};

struct PathSearchResult {
    bool success = false;
    Point attach_point{-1, -1};
    std::vector<Point> path;
    double path_cost = 0.0;
};

struct RoutedTree {
    int net_id = -1;
    std::string net_name;
    std::set<Point> tree_points;
    std::vector<std::vector<Point>> branches;
};

struct RoutingResult {
    std::vector<RoutedTree> routed_nets;
    int total_wirelength = 0;
    int total_overflow = 0;
};

using NetsToRoute = std::set<int>;

struct RoutingDB {
    // Problem must outlive RoutingDB because runtime state references immutable input.
    const Problem* problem = nullptr;

    std::vector<EdgeState> horizontal_edges;
    std::vector<EdgeState> vertical_edges;
    std::vector<int> working_horizontal_usage;
    std::vector<int> working_vertical_usage;
};
