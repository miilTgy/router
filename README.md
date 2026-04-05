# 主要思路

## 数据结构

Channel connectivity graph 和 Switchbox connectivity graph 更适合 full-custom design 和 multi-chip modules ，此处更适合 Grid graph model。

## 算法路线

所有的 A* Search 的 target points 都是 routed tree 上任意一点

A* Search with negotiated-congestion edge costs + congestion driven RRR architecture.

```
Input:
    sample file
Output:
    result file
main:
    Parse input sample
    Initialize routing database from Problem
        - construct implicit grid graph view
        - mark blocked grid cells as unavailable through Problem.blocked
        - initialize edge usage and negotiated-congestion cost state
        - validate and expose net list from Problem.nets
        - reuse common grid / edge helpers from tools.h

    // Initial routing
    Initialize an empty routedTree
    Determine routing order
    for each net in routing order:
        RouteNetAsTree(net, current_edge_costs)
        Commit the routed tree to edge-usage table

    Recompute total wirelength and total overflow

    // Rip-up and reroute with negotiated congestion
    for iter = 1 to MAX_ITER:
        if total_overflow == 0:
            break

        Select victim nets that pass through overflow edges

        for each victim net:
            Rip up its current routed tree from the usage table
            Clear its stored routing tree

        for each victim net:
            RouteNetAsTree(net, current_edge_costs)
            Commit the routed tree to edge-usage table

        // present cost is not a separately committed table.
        // It takes effect immediately after each net commit because it is derived online from usage.
        Update history costs from current overflows at the end of the iteration

        Recompute total wirelength and total overflow
        if no significant improvement for several iterations:
            break

    Output all routed segments in the required format

RouteNetAsTree(net, current_edge_costs):
    Select one seed pin as the initial routed tree
    Put all remaining pins into the unconnected set

    while the unconnected set is not empty:
        Choose one unconnected pin and one connection target on the current tree/frontier
        Run single-source to multiple-target A* from target pin to tree/frontier using negotiated-congestion edge costs
        Merge the returned path into the routed tree
        Update tree frontier
        Remove the connected pin from the unconnected set
```

### Single-source to multiple-target A* Search 算法说明

```
AStarToTree(start, tree_points, db):

    if tree_points is empty:
        error

    if start is in tree_points:
        return success, [start], start, 0

    open_map  = empty hash map: idx -> NodeRecord
    close_map = empty hash map: idx -> NodeRecord
    open_heap = empty min-heap ordered by f, then g

    s = Index(start)
    h = HeuristicToTree(start, tree_points)

    open_map[s] = NodeRecord(parent = -1, g = 0, f = h)
    push (idx = s, g = 0, f = h) into open_heap

    while open_heap is not empty:

        entry = pop minimum-f entry from open_heap
        u = entry.idx

        if u not in open_map:
            continue

        current = open_map[u]

        if entry.g != current.g or entry.f != current.f:
            continue

        erase u from open_map
        close_map[u] = current

        u_point = PointFromIndex(u)

        if u_point is in tree_points:
            path = ReconstructPathFromClose(close_map, u)
            return success, path, u_point, current.g

        for dir in {Up, Down, Left, Right}:

            if no legal edge from u_point toward dir:
                continue

            if edge in this dir is unavailable:
                continue

            v_point = NeighborPoint(u_point, dir)
            v = Index(v_point)

            edge_cost = CostOfEdge(db, u_point, dir)
            tentative_g = current.g + edge_cost
            tentative_f = tentative_g + HeuristicToTree(v_point, tree_points)

            if v not in open_map and v not in close_map:
                open_map[v] = NodeRecord(parent = u, g = tentative_g, f = tentative_f)
                push (v, tentative_g, tentative_f) into open_heap
                continue

            if v in open_map:
                if tentative_g < open_map[v].g:
                    open_map[v].parent = u
                    open_map[v].g = tentative_g
                    open_map[v].f = tentative_f
                    push (v, tentative_g, tentative_f) into open_heap
                continue

            if v in close_map:
                // 不支持 reopen
                continue

    return FAIL

CostOfEdge(db, u_point, dir):
    edge = GetEdgeState(db, u_point.r, u_point.c, dir)
    usage = CurrentWorkingUsage(db, u_point, dir)
    capacity = EdgeCapacity(db, dir)
    if usage < capacity:
        present_cost = 1
    else:
        present_cost = 1 + lambda * (usage - capacity + 1)
    return edge.base_cost * present_cost * edge.history_cost

HeuristicToTree(p, tree_points):
    best = +INF
    for each t in tree_points:
        d = abs(p.r - t.r) + abs(p.c - t.c)
        best = min(best, d)
    return best

ReconstructPathFromClose(close_map, goal_idx):

    reversed_path = empty list
    cur = goal_idx

    while cur != -1:
        reversed_path.push_back(PointFromIndex(cur))
        cur = close_map[cur].parent

    reverse(reversed_path)
    return reversed_path
```

其中 `CostOfEdge(...)` 使用 negotiated-congestion edge cost。

- `base_cost` 是静态基础代价，初值通常为 `1.0`
- `history_cost` 是跨轮累积的长期惩罚，初值通常为 `1.0`
- `present_cost` 不是一张在轮末统一提交的静态表，而是由“当前工作态 usage 与 capacity”在线计算得到的即时惩罚

因此：

- 每 route 完一条 net，并把其 routed tree 提交到当前工作态 usage 后
- 后续 nets 的 A* 搜索就会立刻看到新的 `present_cost`
- `history_cost` 则只在整轮或整个 reroute iteration 结束后，依据当前 overflow 统一更新一次

initial routing 与后续 rip-up and reroute 都应复用这同一套 cost 语义。
