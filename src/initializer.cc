#include "initializer.h"

#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {

bool g_initializer_debug_enabled = false;

std::string FormatPoint(const Point& point) {
    std::ostringstream oss;
    oss << "(" << point.r << ", " << point.c << ")";
    return oss.str();
}

void ValidateProblemShape(const Problem& problem) {
    if (problem.rows <= 0) {
        throw std::runtime_error("InitializeRoutingDB: rows must be positive, got " +
                                 std::to_string(problem.rows));
    }
    if (problem.cols <= 0) {
        throw std::runtime_error("InitializeRoutingDB: cols must be positive, got " +
                                 std::to_string(problem.cols));
    }
    if (problem.horizontal_capacity < 0) {
        throw std::runtime_error(
            "InitializeRoutingDB: horizontal capacity must be non-negative, got " +
            std::to_string(problem.horizontal_capacity));
    }
    if (problem.vertical_capacity < 0) {
        throw std::runtime_error(
            "InitializeRoutingDB: vertical capacity must be non-negative, got " +
            std::to_string(problem.vertical_capacity));
    }

    const std::size_t expected_blocked_size =
        static_cast<std::size_t>(problem.rows) * static_cast<std::size_t>(problem.cols);
    if (problem.blocked.size() != expected_blocked_size) {
        throw std::runtime_error(
            "InitializeRoutingDB: problem.blocked size mismatch, expected " +
            std::to_string(expected_blocked_size) + ", got " +
            std::to_string(problem.blocked.size()));
    }
}

void ValidateBlocks(const Problem& problem) {
    std::vector<uint8_t> seen(problem.blocked.size(), 0);

    for (const Point& block : problem.blocks) {
        if (!InBounds(block.r, block.c, problem.rows, problem.cols)) {
            throw std::runtime_error("InitializeRoutingDB: block out of bounds at " +
                                     FormatPoint(block));
        }

        const int index = CellIndex(block.r, block.c, problem.cols);
        if (seen[index] != 0) {
            throw std::runtime_error("InitializeRoutingDB: duplicate block at " +
                                     FormatPoint(block));
        }
        seen[index] = 0xff;
    }

    for (int r = 0; r < problem.rows; ++r) {
        for (int c = 0; c < problem.cols; ++c) {
            const int index = CellIndex(r, c, problem.cols);
            const bool blocked_from_bitmap = problem.blocked[index] != 0;
            const bool blocked_from_blocks = seen[index] != 0;
            if (blocked_from_bitmap != blocked_from_blocks) {
                throw std::runtime_error(
                    "InitializeRoutingDB: inconsistent blocked sources at " +
                    FormatPoint(Point{r, c}));
            }
        }
    }
}

void ValidateNets(const Problem& problem) {
    std::set<int> seen_net_ids;

    for (const Net& net : problem.nets) {
        if (!seen_net_ids.insert(net.id).second) {
            throw std::runtime_error("InitializeRoutingDB: duplicate net id " +
                                     std::to_string(net.id) + " for net " + net.name);
        }

        if (net.pins.size() < 2) {
            throw std::runtime_error("InitializeRoutingDB: net " + net.name +
                                     " has fewer than 2 pins");
        }

        std::set<Point> seen_pins;
        for (const Point& pin : net.pins) {
            if (!InBounds(pin.r, pin.c, problem.rows, problem.cols)) {
                throw std::runtime_error("InitializeRoutingDB: pin out of bounds in net " +
                                         net.name + " at " + FormatPoint(pin));
            }
            if (!seen_pins.insert(pin).second) {
                throw std::runtime_error("InitializeRoutingDB: duplicate pin in net " +
                                         net.name + " at " + FormatPoint(pin));
            }
            if (IsBlockedCell(problem, pin.r, pin.c)) {
                throw std::runtime_error("InitializeRoutingDB: pin on blocked cell in net " +
                                         net.name + " at " + FormatPoint(pin));
            }
        }
    }
}

void EmitDebugSummary(const RoutingDB& db) {
    if (!g_initializer_debug_enabled) {
        return;
    }

    std::size_t total_pins = 0;
    for (const Net& net : db.problem->nets) {
        total_pins += net.pins.size();
    }

    std::cout << "[initializer] grid size = " << Rows(db) << " x " << Cols(db) << "\n";
    std::cout << "[initializer] blocked cells = " << db.problem->blocks.size() << "\n";
    std::cout << "[initializer] horizontal edges = " << db.horizontal_edges.size() << "\n";
    std::cout << "[initializer] vertical edges = " << db.vertical_edges.size() << "\n";
    std::cout << "[initializer] nets = " << db.problem->nets.size() << "\n";
    std::cout << "[initializer] total pins = " << total_pins << "\n";
}

}  // namespace

void SetInitializerDebug(bool enabled) {
    g_initializer_debug_enabled = enabled;
}

RoutingDB InitializeRoutingDB(const Problem& problem) {
    ValidateProblemShape(problem);
    ValidateBlocks(problem);
    ValidateNets(problem);

    RoutingDB db;
    db.problem = &problem;

    db.horizontal_edges.resize(static_cast<std::size_t>(problem.rows) *
                               static_cast<std::size_t>(std::max(0, problem.cols - 1)));
    db.vertical_edges.resize(static_cast<std::size_t>(std::max(0, problem.rows - 1)) *
                             static_cast<std::size_t>(problem.cols));

    EmitDebugSummary(db);
    return db;
}
