#include "parser.h"

#include <cctype>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {

bool g_parser_debug_enabled = false;

int IndexOf(int r, int c, int cols) {
    return r * cols + c;
}

std::string FormatPoint(const Point& point) {
    std::ostringstream oss;
    oss << "(" << point.r << ", " << point.c << ")";
    return oss.str();
}

[[noreturn]] void ThrowUnexpectedEnd(const std::string& expected) {
    throw std::runtime_error("Expected token '" + expected + "', got end of input");
}

std::string ReadToken(std::istream& is, const std::string& expected_description) {
    std::string token;
    if (!(is >> token)) {
        ThrowUnexpectedEnd(expected_description);
    }
    return token;
}

void ExpectToken(std::istream& is, const std::string& expected) {
    const std::string token = ReadToken(is, expected);
    if (token != expected) {
        throw std::runtime_error(
            "Expected token '" + expected + "', got '" + token + "'");
    }
}

int ReadInt(std::istream& is, const std::string& what) {
    int value = 0;
    if (!(is >> value)) {
        throw std::runtime_error("Expected integer for " + what);
    }
    return value;
}

void CheckPositiveDimension(const char* name, int value) {
    if (value <= 0) {
        throw std::runtime_error(std::string(name) + " must be positive, got " +
                                 std::to_string(value));
    }
}

void CheckNonNegative(const char* name, int value) {
    if (value < 0) {
        throw std::runtime_error(std::string(name) + " must be non-negative, got " +
                                 std::to_string(value));
    }
}

void CheckInBounds(const Point& point, int rows, int cols, const std::string& label) {
    if (point.r < 0 || point.r >= rows || point.c < 0 || point.c >= cols) {
        throw std::runtime_error(label + " out of bounds: " + FormatPoint(point));
    }
}

int ParseNetIdFromName(const std::string& name) {
    if (name.size() < 4 || name.substr(0, 3) != "net") {
        throw std::runtime_error("Invalid net name format: '" + name + "'");
    }

    const std::string suffix = name.substr(3);
    if (suffix.empty()) {
        throw std::runtime_error("Invalid net name format: '" + name + "'");
    }

    for (char ch : suffix) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            throw std::runtime_error("Invalid net name format: '" + name + "'");
        }
    }

    return std::stoi(suffix);
}

void EmitDebugSummary(const Problem& problem) {
    if (!g_parser_debug_enabled) {
        return;
    }

    std::cout << "parser.grid rows=" << problem.rows
              << " cols=" << problem.cols
              << " vertical_capacity=" << problem.vertical_capacity
              << " horizontal_capacity=" << problem.horizontal_capacity << "\n";

    std::cout << "parser.blocks count=" << problem.blocks.size() << "\n";
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        const Point& block = problem.blocks[i];
        std::cout << "parser.block[" << i << "] r=" << block.r << " c=" << block.c << "\n";
    }

    std::cout << "parser.nets count=" << problem.nets.size() << "\n";
    for (std::size_t i = 0; i < problem.nets.size(); ++i) {
        const Net& net = problem.nets[i];
        std::cout << "parser.net[" << i << "] name=" << net.name
                  << " id=" << net.id
                  << " pin_count=" << net.pins.size() << "\n";
        for (std::size_t j = 0; j < net.pins.size(); ++j) {
            const Point& pin = net.pins[j];
            std::cout << "parser.net[" << i << "].pin[" << j << "] r=" << pin.r
                      << " c=" << pin.c << "\n";
        }
    }
}

}  // namespace

void SetParserDebug(bool enabled) {
    g_parser_debug_enabled = enabled;
}

Problem ParseInputFile(const std::string& filename) {
    std::ifstream input(filename);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open input file: " + filename);
    }
    return ParseInputStream(input);
}

Problem ParseInputStream(std::istream& is) {
    Problem problem;

    ExpectToken(is, "grid");
    problem.rows = ReadInt(is, "grid rows");
    problem.cols = ReadInt(is, "grid cols");
    CheckPositiveDimension("rows", problem.rows);
    CheckPositiveDimension("cols", problem.cols);

    problem.blocked.assign(problem.rows * problem.cols, 0);

    ExpectToken(is, "vertical");
    ExpectToken(is, "capacity");
    problem.vertical_capacity = ReadInt(is, "vertical capacity");
    CheckNonNegative("vertical capacity", problem.vertical_capacity);

    ExpectToken(is, "horizontal");
    ExpectToken(is, "capacity");
    problem.horizontal_capacity = ReadInt(is, "horizontal capacity");
    CheckNonNegative("horizontal capacity", problem.horizontal_capacity);

    ExpectToken(is, "num");
    ExpectToken(is, "block");
    const int block_count = ReadInt(is, "num block");
    CheckNonNegative("num block", block_count);
    problem.blocks.reserve(block_count);

    for (int i = 0; i < block_count; ++i) {
        ExpectToken(is, "block");
        Point block{ReadInt(is, "block row"), ReadInt(is, "block col")};
        CheckInBounds(block, problem.rows, problem.cols, "Block coordinate");

        const int index = IndexOf(block.r, block.c, problem.cols);
        if (problem.blocked[index] != 0) {
            throw std::runtime_error("Duplicate block coordinate: " + FormatPoint(block));
        }

        problem.blocked[index] = 0xff;
        problem.blocks.push_back(block);
    }

    ExpectToken(is, "num");
    ExpectToken(is, "net");
    const int net_count = ReadInt(is, "num net");
    CheckNonNegative("num net", net_count);
    problem.nets.reserve(net_count);

    for (int i = 0; i < net_count; ++i) {
        Net net;
        net.name = ReadToken(is, "net name");
        net.id = ParseNetIdFromName(net.name);

        const int pin_count = ReadInt(is, "pin count");
        CheckNonNegative("pin_count", pin_count);
        net.pins.reserve(pin_count);

        std::set<Point> seen_pins;
        for (int j = 0; j < pin_count; ++j) {
            Point pin{ReadInt(is, "pin row"), ReadInt(is, "pin col")};
            CheckInBounds(pin, problem.rows, problem.cols, "Pin coordinate");

            if (!seen_pins.insert(pin).second) {
                throw std::runtime_error("Duplicate pin in net " + net.name + ": " +
                                         FormatPoint(pin));
            }

            const int index = IndexOf(pin.r, pin.c, problem.cols);
            if (problem.blocked[index] != 0) {
                throw std::runtime_error("Pin falls on blocked cell in net " + net.name +
                                         ": " + FormatPoint(pin));
            }

            net.pins.push_back(pin);
        }

        problem.nets.push_back(std::move(net));
    }

    EmitDebugSummary(problem);
    return problem;
}
