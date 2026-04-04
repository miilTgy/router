# 主要思路

## 数据结构

Channel connectivity graph 和 Switchbox connectivity graph 更适合 full-custom design 和 multi-chip modules ，此处更适合 Grid graph model。

## 算法路线

所有的 A* Search 的 target points 都是 routed tree 上任意一点

```
Input:
    sample file
Output:
    result file
main:
    Parse input sample
    Construct implicit grid graph
    Mark blocked grid cells as unavailable
    Initialize edge capacity, edge usage, overflow, and history-cost tables
    Build net list

    // Initial routing
    Determine routing order
    for each net in routing order:
        RouteNetAsTree(net, current_edge_costs)
        Commit the routed tree to edge-usage table

    Recompute total wirelength and total overflow

    // Rip-up and reroute with negotiated congestion
    for iter = 1 to MAX_ITER:
        if total_overflow == 0:
            break

        Update present-congestion costs from current edge usages
        Update history costs from current overflows
        Rebuild negotiated-congestion edge-cost table

        Select victim nets that pass through overflow edges

        for each victim net:
            Rip up its current routed tree from the usage table
            Clear its stored routing tree

        for each victim net:
            RouteNetAsTree(net, current_edge_costs)
            Commit the routed tree to edge-usage table

        Recompute total wirelength and total overflow
        if no significant improvement for several iterations:
            break

    Output all routed segments in the required format

RouteNetAsTree(net, current_edge_costs):
    Select one seed pin as the initial routed tree
    Put all remaining pins into the unconnected set

    while the unconnected set is not empty:
        Choose one unconnected pin and one connection target on the current tree/frontier
        Run A* from target pin to tree/frontier using negotiated-congestion edge costs
        Merge the returned path into the routed tree
        Update tree frontier
        Remove the connected pin from the unconnected set
```