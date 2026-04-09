#include "rrr.h"

#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "router.h"
#include "tools.h"

namespace {

constexpr int kMaxRRRIterations = 50;

bool g_rrr_debug_enabled = true;

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

std::unordered_map<int, int> BuildNetIdToIndexMap(const RoutingDB& db) {
    std::unordered_map<int, int> net_id_to_index;
    for (std::size_t i = 0; i < db.problem->nets.size(); ++i) {
        net_id_to_index.emplace(db.problem->nets[i].id, static_cast<int>(i));
    }
    return net_id_to_index;
}

bool IsTreeEmpty(const RoutedTree& tree) {
    if (tree.net_id == -1) {
        return true;
    }
    return tree.branches.empty() && tree.tree_points.empty();
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

void ClearWorkingUsage(RoutingDB& db) {
    db.working_horizontal_usage.assign(db.horizontal_edges.size(), 0);
    db.working_vertical_usage.assign(db.vertical_edges.size(), 0);
}

void ClearCommittedEdgeUsageOnly(RoutingDB& db) {
    for (EdgeState& edge : db.horizontal_edges) {
        edge.usage = 0;
    }
    for (EdgeState& edge : db.vertical_edges) {
        edge.usage = 0;
    }
}

void RebuildDBFromGlobalSolution(
    RoutingDB& db,
    const std::vector<RoutedTree>& routed_nets) {
    ClearWorkingUsage(db);
    ClearCommittedEdgeUsageOnly(db);

    for (const RoutedTree& tree : routed_nets) {
        if (IsTreeEmpty(tree)) {
            continue;
        }
        WriteRoutedTreeToUsage(db, tree);
    }
    WriteUsageToEdgeState(db);
}

bool HasOverflowEdge(const RoutingDB& db) {
    for (const EdgeState& edge : db.horizontal_edges) {
        if (edge.usage > HorizontalCapacity(db)) {
            return true;
        }
    }
    for (const EdgeState& edge : db.vertical_edges) {
        if (edge.usage > VerticalCapacity(db)) {
            return true;
        }
    }
    return false;
}

bool TreeTouchesOverflowEdge(const RoutingDB& db, const RoutedTree& tree) {
    for (const std::vector<Point>& branch : tree.branches) {
        if (branch.size() < 2) {
            continue;
        }

        for (std::size_t i = 1; i < branch.size(); ++i) {
            const Point& from = branch[i - 1];
            const Point& to = branch[i];
            const Dir dir = DirectionBetweenPoints(from, to);
            if (EdgeOverflow(db, from.r, from.c, dir) > 0) {
                return true;
            }
        }
    }

    return false;
}

NetsToRoute SelectVictimNetIds(
    const RoutingDB& db,
    const std::vector<RoutedTree>& current_routed_nets) {
    NetsToRoute victims;
    if (!HasOverflowEdge(db)) {
        return victims;
    }

    for (const RoutedTree& tree : current_routed_nets) {
        if (IsTreeEmpty(tree)) {
            continue;
        }
        if (TreeTouchesOverflowEdge(db, tree)) {
            victims.insert(tree.net_id);
        }
    }
    return victims;
}

void ValidateKnownUniqueNetIds(
    const std::unordered_map<int, int>& net_id_to_index,
    const std::vector<RoutedTree>& routed_nets,
    const char* caller) {
    std::unordered_set<int> seen_net_ids;
    for (const RoutedTree& tree : routed_nets) {
        if (IsTreeEmpty(tree)) {
            continue;
        }

        if (net_id_to_index.find(tree.net_id) == net_id_to_index.end()) {
            throw std::runtime_error(std::string(caller) + ": unknown net_id " +
                                     std::to_string(tree.net_id));
        }
        if (!seen_net_ids.insert(tree.net_id).second) {
            throw std::runtime_error(std::string(caller) + ": duplicate net_id " +
                                     std::to_string(tree.net_id));
        }
    }
}

std::vector<RoutedTree> RemoveVictimsFromGlobalSolution(
    const std::vector<RoutedTree>& current_routed_nets,
    const NetsToRoute& victim_net_ids) {
    std::vector<RoutedTree> remaining_routed_nets;
    remaining_routed_nets.reserve(current_routed_nets.size());

    for (const RoutedTree& tree : current_routed_nets) {
        if (victim_net_ids.find(tree.net_id) != victim_net_ids.end()) {
            continue;
        }
        remaining_routed_nets.push_back(tree);
    }

    return remaining_routed_nets;
}

void MergePartialResultIntoGlobalSolution(
    const std::unordered_map<int, int>& net_id_to_index,
    const NetsToRoute& victim_net_ids,
    const RoutingResult& partial_result,
    std::vector<RoutedTree>* current_routed_nets) {
    if (current_routed_nets == nullptr) {
        throw std::logic_error("MergePartialResultIntoGlobalSolution: current_routed_nets is null");
    }

    std::unordered_set<int> existing_net_ids;
    existing_net_ids.reserve(current_routed_nets->size() + partial_result.routed_nets.size());
    for (const RoutedTree& tree : *current_routed_nets) {
        if (IsTreeEmpty(tree)) {
            continue;
        }
        if (!existing_net_ids.insert(tree.net_id).second) {
            throw std::runtime_error("MergePartialResultIntoGlobalSolution: duplicate net_id " +
                                     std::to_string(tree.net_id) +
                                     " in current global solution");
        }
    }

    for (const RoutedTree& tree : partial_result.routed_nets) {
        if (IsTreeEmpty(tree)) {
            continue;
        }
        if (net_id_to_index.find(tree.net_id) == net_id_to_index.end()) {
            throw std::runtime_error("MergePartialResultIntoGlobalSolution: unknown net_id " +
                                     std::to_string(tree.net_id));
        }
        if (victim_net_ids.find(tree.net_id) == victim_net_ids.end()) {
            throw std::runtime_error("MergePartialResultIntoGlobalSolution: unexpected rerouted net_id " +
                                     std::to_string(tree.net_id));
        }
        if (!existing_net_ids.insert(tree.net_id).second) {
            throw std::runtime_error("MergePartialResultIntoGlobalSolution: duplicate net_id " +
                                     std::to_string(tree.net_id) + " after merge");
        }
        current_routed_nets->push_back(tree);
    }
}

bool IsBetterSolution(
    int total_overflow,
    int total_wirelength,
    int best_total_overflow,
    int best_total_wirelength) {
    if (total_overflow != best_total_overflow) {
        return total_overflow < best_total_overflow;
    }
    return total_wirelength < best_total_wirelength;
}

void EmitIterationStartDebug(int iteration, int total_overflow) {
    if (!g_rrr_debug_enabled) {
        return;
    }

    std::cout << "[rrr] iter=" << iteration
              << " overflow=" << total_overflow << "\n";
}

void EmitIterationResultDebug(
    int iteration,
    std::size_t victim_count,
    int total_overflow,
    int total_wirelength,
    int best_total_overflow,
    int best_total_wirelength) {
    if (!g_rrr_debug_enabled) {
        return;
    }

    std::cout << "[rrr] iter=" << iteration
              << " victims=" << victim_count
              << " reroute_overflow=" << total_overflow
              << " reroute_wirelength=" << total_wirelength
              << " best_overflow=" << best_total_overflow
              << " best_wirelength=" << best_total_wirelength << "\n";
}

void EmitStopDebug(
    int iteration,
    const std::string& reason,
    int best_total_overflow,
    int best_total_wirelength) {
    if (!g_rrr_debug_enabled) {
        return;
    }

    std::cout << "[rrr] stop iter=" << iteration
              << " reason=" << reason
              << " best_overflow=" << best_total_overflow
              << " best_wirelength=" << best_total_wirelength << "\n";
}

}  // namespace

RoutingResult RunRRR(
    RoutingDB& db,
    const RoutingResult& initial_result) {
    EnsureProblemBound(db, "RunRRR");

    const std::unordered_map<int, int> net_id_to_index = BuildNetIdToIndexMap(db);
    ValidateKnownUniqueNetIds(net_id_to_index, initial_result.routed_nets, "RunRRR");

    if (initial_result.routed_nets.empty()) {
        RebuildDBFromGlobalSolution(db, initial_result.routed_nets);
        return RoutingResult{
            initial_result.routed_nets,
            ComputeTotalWirelength(initial_result.routed_nets),
            ComputeTotalOverflow(db),
        };
    }

    std::vector<RoutedTree> current_routed_nets = initial_result.routed_nets;
    RebuildDBFromGlobalSolution(db, current_routed_nets);

    int best_total_overflow = ComputeTotalOverflow(db);
    int best_total_wirelength = ComputeTotalWirelength(current_routed_nets);
    std::vector<RoutedTree> best_routed_nets = current_routed_nets;

    for (int iteration = 1; iteration <= kMaxRRRIterations; ++iteration) {
        RebuildDBFromGlobalSolution(db, current_routed_nets);

        const int current_total_overflow = ComputeTotalOverflow(db);
        EmitIterationStartDebug(iteration, current_total_overflow);

        if (current_total_overflow == 0) {
            EmitStopDebug(iteration, "overflow_zero", best_total_overflow, best_total_wirelength);
            break;
        }

        const NetsToRoute victim_net_ids = SelectVictimNetIds(db, current_routed_nets);
        if (victim_net_ids.empty()) {
            EmitStopDebug(iteration, "no_victims", best_total_overflow, best_total_wirelength);
            break;
        }

        current_routed_nets =
            RemoveVictimsFromGlobalSolution(current_routed_nets, victim_net_ids);
        RebuildDBFromGlobalSolution(db, current_routed_nets);

        const RoutingResult partial_result = RunRouting(db, victim_net_ids);
        ValidateKnownUniqueNetIds(
            net_id_to_index, partial_result.routed_nets, "RunRRR partial_result");
        MergePartialResultIntoGlobalSolution(
            net_id_to_index, victim_net_ids, partial_result, &current_routed_nets);
        ValidateKnownUniqueNetIds(
            net_id_to_index, current_routed_nets, "RunRRR merged_solution");

        RebuildDBFromGlobalSolution(db, current_routed_nets);

        const int total_overflow = ComputeTotalOverflow(db);
        const int total_wirelength = ComputeTotalWirelength(current_routed_nets);
        if (IsBetterSolution(
                total_overflow,
                total_wirelength,
                best_total_overflow,
                best_total_wirelength)) {
            best_total_overflow = total_overflow;
            best_total_wirelength = total_wirelength;
            best_routed_nets = current_routed_nets;
        }

        EmitIterationResultDebug(
            iteration,
            victim_net_ids.size(),
            total_overflow,
            total_wirelength,
            best_total_overflow,
            best_total_wirelength);
    }

    RebuildDBFromGlobalSolution(db, best_routed_nets);
    return RoutingResult{
        best_routed_nets,
        ComputeTotalWirelength(best_routed_nets),
        ComputeTotalOverflow(db),
    };
}
