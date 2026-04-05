#pragma once

#include <set>
#include <vector>

#include "common.h"

std::vector<int> DetermineRoutingOrder(
    const RoutingDB& db,
    const NetsToRoute& nets_to_route);

PathSearchResult AStarToTree(
    const RoutingDB& db,
    Point start,
    const std::set<Point>& tree_points);

RoutedTree RouteNetAsTree(
    const RoutingDB& db,
    const Net& net);

void WriteRoutedTreeToUsage(
    RoutingDB& db,
    const RoutedTree& tree);

void WriteUsageToEdgeState(
    RoutingDB& db);

RoutingResult RunRouting(
    RoutingDB& db,
    const NetsToRoute& nets_to_route);

RoutingResult RunInitialRouting(
    RoutingDB& db);

int ComputeTotalWirelength(const std::vector<RoutedTree>& routed_nets);
int ComputeTotalOverflow(const RoutingDB& db);

void SetRouterDebug(bool enabled);
