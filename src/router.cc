#include "router.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "tools.h"

namespace {

constexpr double kLambda = 1.0;
constexpr double kMu = 0.5;

bool g_router_debug_enabled = false;

struct NodeRecord {
    int parent = -1;
    double g = std::numeric_limits<double>::infinity();
    double f = std::numeric_limits<double>::infinity();
};

struct OpenEntry {
    int idx = -1;
    double g = 0.0;
    double f = 0.0;
};

struct OpenEntryCompare {
    bool operator()(const OpenEntry& lhs, const OpenEntry& rhs) const {
        if (lhs.f != rhs.f) {
            return lhs.f > rhs.f;
        }
        if (lhs.g != rhs.g) {
            return lhs.g > rhs.g;
        }
        return lhs.idx > rhs.idx;
    }
};

std::string FormatPoint(const Point& point) {
    std::ostringstream oss;
    oss << "(" << point.r << "," << point.c << ")";
    return oss.str();
}

void EnsureProblemBound(const RoutingDB& db, const char* caller) {
    if (db.problem == nullptr) {
        throw std::logic_error(std::string(caller) + ": RoutingDB is not bound to a Problem");
    }
}

void EnsureWorkingUsageShape(RoutingDB& db) {
    if (db.working_horizontal_usage.size() != db.horizontal_edges.size()) {
        db.working_horizontal_usage.resize(db.horizontal_edges.size(), 0);
    }
    if (db.working_vertical_usage.size() != db.vertical_edges.size()) {
        db.working_vertical_usage.resize(db.vertical_edges.size(), 0);
    }
}

void LoadWorkingUsageFromEdgeState(RoutingDB& db) {
    EnsureWorkingUsageShape(db);
    for (std::size_t i = 0; i < db.horizontal_edges.size(); ++i) {
        db.working_horizontal_usage[i] = db.horizontal_edges[i].usage;
    }
    for (std::size_t i = 0; i < db.vertical_edges.size(); ++i) {
        db.working_vertical_usage[i] = db.vertical_edges[i].usage;
    }
}

int PointIndex(const RoutingDB& db, Point p) {
    if (!InBounds(p.r, p.c, Rows(db), Cols(db))) {
        throw std::out_of_range("PointIndex: point out of bounds at " + FormatPoint(p));
    }
    return CellIndex(p.r, p.c, Cols(db));
}

Point PointFromIndex(const RoutingDB& db, int idx) {
    const int cols = Cols(db);
    const int rows = Rows(db);
    const int cell_count = rows * cols;
    if (idx < 0 || idx >= cell_count) {
        throw std::out_of_range("PointFromIndex: invalid point index " + std::to_string(idx));
    }
    return Point{idx / cols, idx % cols};
}

int ManhattanDistance(Point a, Point b) {
    return std::abs(a.r - b.r) + std::abs(a.c - b.c);
}

double HeuristicToTree(Point p, const std::set<Point>& tree_points) {
    if (tree_points.empty()) {
        throw std::logic_error("HeuristicToTree: tree_points must not be empty");
    }

    int best = std::numeric_limits<int>::max();
    for (const Point& target : tree_points) {
        best = std::min(best, ManhattanDistance(p, target));
    }
    return static_cast<double>(best);
}

int BoundingBoxPerimeter(const Net& net) {
    int min_r = std::numeric_limits<int>::max();
    int max_r = std::numeric_limits<int>::min();
    int min_c = std::numeric_limits<int>::max();
    int max_c = std::numeric_limits<int>::min();

    for (const Point& pin : net.pins) {
        min_r = std::min(min_r, pin.r);
        max_r = std::max(max_r, pin.r);
        min_c = std::min(min_c, pin.c);
        max_c = std::max(max_c, pin.c);
    }
    return 2 * ((max_r - min_r) + (max_c - min_c));
}

Point ChooseSeedPin(const Net& net) {
    if (net.pins.size() < 2) {
        throw std::runtime_error("ChooseSeedPin: net " + net.name + " has fewer than 2 pins");
    }

    int best_index = -1;
    int best_total_distance = std::numeric_limits<int>::max();
    for (std::size_t i = 0; i < net.pins.size(); ++i) {
        int total_distance = 0;
        for (std::size_t j = 0; j < net.pins.size(); ++j) {
            if (i == j) {
                continue;
            }
            total_distance += ManhattanDistance(net.pins[i], net.pins[j]);
        }
        if (total_distance < best_total_distance) {
            best_total_distance = total_distance;
            best_index = static_cast<int>(i);
        }
    }

    return net.pins[best_index];
}

Point ChooseNextPinToConnect(
    const std::vector<Point>& unconnected,
    const std::set<Point>& tree_points) {
    if (unconnected.empty()) {
        throw std::logic_error("ChooseNextPinToConnect: unconnected set must not be empty");
    }
    if (tree_points.empty()) {
        throw std::logic_error("ChooseNextPinToConnect: tree_points must not be empty");
    }

    std::size_t best_index = 0;
    int best_distance = std::numeric_limits<int>::max();
    for (std::size_t i = 0; i < unconnected.size(); ++i) {
        int distance_to_tree = std::numeric_limits<int>::max();
        for (const Point& tree_point : tree_points) {
            distance_to_tree =
                std::min(distance_to_tree, ManhattanDistance(unconnected[i], tree_point));
        }
        if (distance_to_tree < best_distance) {
            best_distance = distance_to_tree;
            best_index = i;
        }
    }

    return unconnected[best_index];
}

int GetWorkingUsage(const RoutingDB& db, int r, int c, Dir dir) {
    switch (dir) {
        case Dir::kUp:
            return db.working_vertical_usage[VerticalEdgeIndex(r - 1, c, Cols(db))];
        case Dir::kDown:
            return db.working_vertical_usage[VerticalEdgeIndex(r, c, Cols(db))];
        case Dir::kLeft:
            return db.working_horizontal_usage[HorizontalEdgeIndex(r, c - 1, Cols(db))];
        case Dir::kRight:
            return db.working_horizontal_usage[HorizontalEdgeIndex(r, c, Cols(db))];
    }
    throw std::logic_error("GetWorkingUsage: invalid direction");
}

int& MutableWorkingUsage(RoutingDB& db, int r, int c, Dir dir) {
    switch (dir) {
        case Dir::kUp:
            return db.working_vertical_usage[VerticalEdgeIndex(r - 1, c, Cols(db))];
        case Dir::kDown:
            return db.working_vertical_usage[VerticalEdgeIndex(r, c, Cols(db))];
        case Dir::kLeft:
            return db.working_horizontal_usage[HorizontalEdgeIndex(r, c - 1, Cols(db))];
        case Dir::kRight:
            return db.working_horizontal_usage[HorizontalEdgeIndex(r, c, Cols(db))];
    }
    throw std::logic_error("MutableWorkingUsage: invalid direction");
}

double PresentCost(int usage, int capacity) {
    if (usage < capacity) {
        return 1.0;
    }
    return 1.0 + kLambda * static_cast<double>(usage - capacity + 1);
}

double CostOfEdge(const RoutingDB& db, int r, int c, Dir dir) {
    const EdgeState& edge = GetEdgeState(db, r, c, dir);
    const int usage = GetWorkingUsage(db, r, c, dir);
    const int capacity = EdgeCapacity(db, dir);
    return edge.base_cost * PresentCost(usage, capacity) * edge.history_cost;
}

std::vector<Point> ReconstructPath(
    const RoutingDB& db,
    const std::unordered_map<int, NodeRecord>& close_map,
    int goal_idx) {
    std::vector<Point> reversed_path;
    int current = goal_idx;
    while (current != -1) {
        reversed_path.push_back(PointFromIndex(db, current));
        const auto it = close_map.find(current);
        if (it == close_map.end()) {
            throw std::logic_error("ReconstructPath: missing close-map node");
        }
        current = it->second.parent;
    }
    std::reverse(reversed_path.begin(), reversed_path.end());
    return reversed_path;
}

Dir DirectionBetweenPoints(Point from, Point to) {
    if (to.r == from.r - 1 && to.c == from.c) {
        return Dir::kUp;
    }
    if (to.r == from.r + 1 && to.c == from.c) {
        return Dir::kDown;
    }
    if (to.r == from.r && to.c == from.c - 1) {
        return Dir::kLeft;
    }
    if (to.r == from.r && to.c == from.c + 1) {
        return Dir::kRight;
    }
    throw std::runtime_error("DirectionBetweenPoints: non-Manhattan adjacent points " +
                             FormatPoint(from) + " and " + FormatPoint(to));
}

void UpdateHistoryCostFromWorkingUsage(RoutingDB& db) {
    for (std::size_t i = 0; i < db.horizontal_edges.size(); ++i) {
        const int overflow =
            std::max(0, db.working_horizontal_usage[i] - HorizontalCapacity(db));
        db.horizontal_edges[i].history_cost += kMu * static_cast<double>(overflow);
    }
    for (std::size_t i = 0; i < db.vertical_edges.size(); ++i) {
        const int overflow = std::max(0, db.working_vertical_usage[i] - VerticalCapacity(db));
        db.vertical_edges[i].history_cost += kMu * static_cast<double>(overflow);
    }
}

void EmitRoutingOrderDebug(const std::vector<int>& order) {
    if (!g_router_debug_enabled) {
        return;
    }

    std::cout << "[router] routing order:";
    for (int index : order) {
        std::cout << " " << index;
    }
    std::cout << "\n";
}

void EmitNetStartDebug(const Net& net, Point seed) {
    if (!g_router_debug_enabled) {
        return;
    }

    std::cout << "[router] start net name=" << net.name
              << " id=" << net.id
              << " pin_count=" << net.pins.size()
              << " seed=" << FormatPoint(seed) << "\n";
}

void EmitCommittedTreeEdgesDebug(const RoutedTree& tree) {
    if (!g_router_debug_enabled) {
        return;
    }

    int edge_count = 0;
    for (const std::vector<Point>& branch : tree.branches) {
        if (branch.size() < 2) {
            continue;
        }
        for (std::size_t i = 1; i < branch.size(); ++i) {
            const Point& from = branch[i - 1];
            const Point& to = branch[i];
            DirectionBetweenPoints(from, to);
            std::cout << "[router] net edge name=" << tree.net_name
                      << " id=" << tree.net_id
                      << " from=" << FormatPoint(from)
                      << " to=" << FormatPoint(to) << "\n";
            ++edge_count;
        }
    }

    std::cout << "[router] committed net name=" << tree.net_name
              << " id=" << tree.net_id
              << " edge_count=" << edge_count << "\n";
}

void EmitRoutingDoneDebug(const RoutingResult& result) {
    if (!g_router_debug_enabled) {
        return;
    }

    std::cout << "[router] initial routing done nets=" << result.routed_nets.size()
              << " total_wirelength=" << result.total_wirelength
              << " total_overflow=" << result.total_overflow << "\n";
}

std::unordered_map<int, int> BuildNetIdToIndexMap(const RoutingDB& db) {
    std::unordered_map<int, int> net_id_to_index;
    for (std::size_t i = 0; i < db.problem->nets.size(); ++i) {
        net_id_to_index.emplace(db.problem->nets[i].id, static_cast<int>(i));
    }
    return net_id_to_index;
}

}  // namespace

void SetRouterDebug(bool enabled) {
    g_router_debug_enabled = enabled;
}

std::vector<int> DetermineRoutingOrder(
    const RoutingDB& db,
    const NetsToRoute& nets_to_route) {
    EnsureProblemBound(db, "DetermineRoutingOrder");

    std::vector<int> selected_indices;
    selected_indices.reserve(nets_to_route.size());
    for (std::size_t i = 0; i < db.problem->nets.size(); ++i) {
        if (nets_to_route.find(db.problem->nets[i].id) != nets_to_route.end()) {
            selected_indices.push_back(static_cast<int>(i));
        }
    }

    std::stable_sort(selected_indices.begin(), selected_indices.end(),
                     [&db](int lhs_index, int rhs_index) {
                         const Net& lhs = db.problem->nets[lhs_index];
                         const Net& rhs = db.problem->nets[rhs_index];
                         const int lhs_perimeter = BoundingBoxPerimeter(lhs);
                         const int rhs_perimeter = BoundingBoxPerimeter(rhs);
                         if (lhs_perimeter != rhs_perimeter) {
                             return lhs_perimeter > rhs_perimeter;
                         }
                         if (lhs.pins.size() != rhs.pins.size()) {
                             return lhs.pins.size() > rhs.pins.size();
                         }
                         return lhs_index < rhs_index;
                     });

    return selected_indices;
}

PathSearchResult AStarToTree(
    const RoutingDB& db,
    Point start,
    const std::set<Point>& tree_points) {
    EnsureProblemBound(db, "AStarToTree");
    if (tree_points.empty()) {
        throw std::logic_error("AStarToTree: tree_points must not be empty");
    }

    if (tree_points.find(start) != tree_points.end()) {
        return PathSearchResult{true, start, {start}, 0.0};
    }

    std::unordered_map<int, NodeRecord> open_map;
    std::unordered_map<int, NodeRecord> close_map;
    std::priority_queue<OpenEntry, std::vector<OpenEntry>, OpenEntryCompare> open_heap;

    const int start_idx = PointIndex(db, start);
    const double h = HeuristicToTree(start, tree_points);
    open_map[start_idx] = NodeRecord{-1, 0.0, h};
    open_heap.push(OpenEntry{start_idx, 0.0, h});

    while (!open_heap.empty()) {
        const OpenEntry entry = open_heap.top();
        open_heap.pop();

        const auto open_it = open_map.find(entry.idx);
        if (open_it == open_map.end()) {
            continue;
        }
        const NodeRecord current = open_it->second;
        if (entry.g != current.g || entry.f != current.f) {
            continue;
        }

        open_map.erase(open_it);
        close_map[entry.idx] = current;

        const Point current_point = PointFromIndex(db, entry.idx);
        if (tree_points.find(current_point) != tree_points.end()) {
            return PathSearchResult{
                true,
                current_point,
                ReconstructPath(db, close_map, entry.idx),
                current.g,
            };
        }

        for (Dir dir : {Dir::kUp, Dir::kDown, Dir::kLeft, Dir::kRight}) {
            if (!HasEdge(db, current_point.r, current_point.c, dir)) {
                continue;
            }
            if (!IsEdgeAvailable(db, current_point.r, current_point.c, dir)) {
                continue;
            }

            const Point neighbor = NeighborPoint(current_point.r, current_point.c, dir);
            const int neighbor_idx = PointIndex(db, neighbor);
            const double tentative_g =
                current.g + CostOfEdge(db, current_point.r, current_point.c, dir);
            const double tentative_f = tentative_g + HeuristicToTree(neighbor, tree_points);

            const auto open_neighbor_it = open_map.find(neighbor_idx);
            if (open_neighbor_it != open_map.end()) {
                if (tentative_g < open_neighbor_it->second.g) {
                    open_neighbor_it->second.parent = entry.idx;
                    open_neighbor_it->second.g = tentative_g;
                    open_neighbor_it->second.f = tentative_f;
                    open_heap.push(OpenEntry{neighbor_idx, tentative_g, tentative_f});
                }
                continue;
            }

            if (close_map.find(neighbor_idx) != close_map.end()) {
                continue;
            }

            open_map[neighbor_idx] = NodeRecord{entry.idx, tentative_g, tentative_f};
            open_heap.push(OpenEntry{neighbor_idx, tentative_g, tentative_f});
        }
    }

    return PathSearchResult{};
}

RoutedTree RouteNetAsTree(
    const RoutingDB& db,
    const Net& net) {
    EnsureProblemBound(db, "RouteNetAsTree");
    if (net.pins.size() < 2) {
        throw std::runtime_error("RouteNetAsTree: net " + net.name + " has fewer than 2 pins");
    }

    RoutedTree tree;
    tree.net_id = net.id;
    tree.net_name = net.name;

    const Point seed = ChooseSeedPin(net);
    EmitNetStartDebug(net, seed);
    tree.tree_points.insert(seed);

    std::vector<Point> unconnected;
    unconnected.reserve(net.pins.size() - 1);
    bool skipped_seed = false;
    for (const Point& pin : net.pins) {
        if (!skipped_seed && pin == seed) {
            skipped_seed = true;
            continue;
        }
        unconnected.push_back(pin);
    }

    while (!unconnected.empty()) {
        const Point next_pin = ChooseNextPinToConnect(unconnected, tree.tree_points);
        const PathSearchResult search = AStarToTree(db, next_pin, tree.tree_points);
        if (!search.success) {
            throw std::runtime_error("RouteNetAsTree: failed to connect pin " +
                                     FormatPoint(next_pin) + " for net " + net.name);
        }

        for (const Point& point : search.path) {
            tree.tree_points.insert(point);
        }
        tree.branches.push_back(search.path);

        const auto erase_it =
            std::find(unconnected.begin(), unconnected.end(), next_pin);
        if (erase_it == unconnected.end()) {
            throw std::logic_error("RouteNetAsTree: connected pin missing from unconnected set");
        }
        unconnected.erase(erase_it);
    }

    return tree;
}

void WriteRoutedTreeToUsage(
    RoutingDB& db,
    const RoutedTree& tree) {
    EnsureProblemBound(db, "WriteRoutedTreeToUsage");
    EnsureWorkingUsageShape(db);
    EmitCommittedTreeEdgesDebug(tree);

    for (const std::vector<Point>& branch : tree.branches) {
        if (branch.size() < 2) {
            continue;
        }
        for (std::size_t i = 1; i < branch.size(); ++i) {
            const Point& from = branch[i - 1];
            const Point& to = branch[i];
            const Dir dir = DirectionBetweenPoints(from, to);
            MutableWorkingUsage(db, from.r, from.c, dir) += 1;
        }
    }
}

void WriteUsageToEdgeState(
    RoutingDB& db) {
    EnsureProblemBound(db, "WriteUsageToEdgeState");
    EnsureWorkingUsageShape(db);

    for (std::size_t i = 0; i < db.horizontal_edges.size(); ++i) {
        db.horizontal_edges[i].usage = db.working_horizontal_usage[i];
        db.horizontal_edges[i].present_cost =
            PresentCost(db.horizontal_edges[i].usage, HorizontalCapacity(db));
    }
    for (std::size_t i = 0; i < db.vertical_edges.size(); ++i) {
        db.vertical_edges[i].usage = db.working_vertical_usage[i];
        db.vertical_edges[i].present_cost =
            PresentCost(db.vertical_edges[i].usage, VerticalCapacity(db));
    }
}

RoutingResult RunRouting(
    RoutingDB& db,
    const NetsToRoute& nets_to_route) {
    EnsureProblemBound(db, "RunRouting");

    if (nets_to_route.empty()) {
        return RoutingResult{{}, 0, ComputeTotalOverflow(db)};
    }

    const std::unordered_map<int, int> net_id_to_index = BuildNetIdToIndexMap(db);
    for (int net_id : nets_to_route) {
        if (net_id_to_index.find(net_id) == net_id_to_index.end()) {
            throw std::invalid_argument("RunRouting: unknown net id " + std::to_string(net_id));
        }
    }

    LoadWorkingUsageFromEdgeState(db);

    const std::vector<int> order = DetermineRoutingOrder(db, nets_to_route);
    EmitRoutingOrderDebug(order);

    RoutingResult result;
    result.routed_nets.reserve(order.size());
    for (int net_index : order) {
        const Net& net = db.problem->nets[net_index];
        RoutedTree tree = RouteNetAsTree(db, net);
        WriteRoutedTreeToUsage(db, tree);
        result.routed_nets.push_back(std::move(tree));
    }

    UpdateHistoryCostFromWorkingUsage(db);
    WriteUsageToEdgeState(db);

    result.total_wirelength = ComputeTotalWirelength(result.routed_nets);
    result.total_overflow = ComputeTotalOverflow(db);
    EmitRoutingDoneDebug(result);
    return result;
}

RoutingResult RunInitialRouting(
    RoutingDB& db) {
    EnsureProblemBound(db, "RunInitialRouting");

    NetsToRoute all_net_ids;
    for (const Net& net : db.problem->nets) {
        all_net_ids.insert(net.id);
    }
    return RunRouting(db, all_net_ids);
}

int ComputeTotalWirelength(const std::vector<RoutedTree>& routed_nets) {
    int total_wirelength = 0;
    for (const RoutedTree& tree : routed_nets) {
        for (const std::vector<Point>& branch : tree.branches) {
            if (!branch.empty()) {
                total_wirelength += static_cast<int>(branch.size()) - 1;
            }
        }
    }
    return total_wirelength;
}

int ComputeTotalOverflow(const RoutingDB& db) {
    EnsureProblemBound(db, "ComputeTotalOverflow");

    int total_overflow = 0;
    for (std::size_t i = 0; i < db.horizontal_edges.size(); ++i) {
        total_overflow +=
            std::max(0, db.horizontal_edges[i].usage - HorizontalCapacity(db));
    }
    for (std::size_t i = 0; i < db.vertical_edges.size(); ++i) {
        total_overflow +=
            std::max(0, db.vertical_edges[i].usage - VerticalCapacity(db));
    }
    return total_overflow;
}
