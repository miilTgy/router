#pragma once

#include <cstdint>
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
