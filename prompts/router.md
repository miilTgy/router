请为这个 C++ global routing 项目实现 `router` 模块。该模块负责“对显式给定的一组 nets 做 routing”；initial routing 只是其中一个特例：

1. 接收 `RoutingDB` 与本轮要 route 的 `NetsToRoute`
2. 只对该子集 nets 确定 routing order
3. 对该子集中的每个 net 执行 `RouteNetAsTree(...)`
3. 将 routed tree 提交到 edge usage

请严格复用当前仓库中的 `common.h / tools.h / parser / initializer / README` 语义，不要重复 parse 输入，不要实现 rip-up and reroute，不要实现 writer。

---

# 一、目标与边界

当前数据流是：

```text
main
  -> parser 解析输入，得到 Problem
  -> initializer 构建 RoutingDB
  -> router 执行 initial routing
  -> 后续 rrr 再做 rip-up and reroute
```

router 要做的事：

- 定义 routed tree 相关共享数据结构
- 接收上层显式传入的 `NetsToRoute`
- 只对该子集确定 net 的 routing order
- 对单个 net 执行 single-source to multi-target A*，逐步构造 routed tree
- 维护当前轮 routing 所需的 negotiated-congestion usage / cost 语义
- 输出调试信息
- 提供通用 `RunRouting(...)`，并保留 `RunInitialRouting(...)` 作为 initial routing 的薄封装

router 不要做的事：

- 不要重新 parse 输入文件
- 不要重新初始化 `RoutingDB`
- 不要由 router 自己猜测“本轮哪些 nets 需要 route”
- 不要实现结果文件 writer
- 不要实现 victim 选择逻辑
- 不要在 router 内部执行 rip-up
- 不要把大量逻辑塞进 `main`
- 不要新设计一套平行 graph / edge helper

推荐接入文件：

```text
include/router.h
src/router.cc
```

必要时可小幅扩展：

```text
include/common.h
src/main.cc
Makefile
```

如果为了消除状态归属歧义而必须扩展 `RoutingDB`，允许在 `common.h` 中增加少量 router / rrr 共享字段；但不要复制一整套平行 edge graph。

---

# 二、数据结构与 API

请在 `common.h` 中提供或补充适合 router / 后续 rrr 复用的结构：

```cpp
struct PathSearchResult {
    bool success = false;
    Point attach_point{-1, -1};
    std::vector<Point> path;   // 从 start 到 attach_point，含两端
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

using NetsToRoute = std::set<int>;   // elements are Net.id
```

约束：

- `tree_points` 用作 A* 的 multi-target 终点集合
- `branches` 保留每次接入树的一条路径，供 commit / rip-up / 输出复用
- 不需要在 `RoutedTree` 中缓存 edge usage 或单独维护 adjacency graph

此外，必须明确 router 当前轮的工作态存放位置。实现时必须满足下面二选一，但推荐第一个：

- 推荐：在 `RoutingDB` 中增加“本轮工作态 edge usage / cost 视图”字段，router 直接读写该工作态；轮末再统一提交到对外可见的 edge state
- 若不扩展 `RoutingDB`：则必须在 `src/router.cc` 中维护文件内私有工作态，并保证所有 router API 都只读写这一份工作态，不能隐式退化成直接改 `db.horizontal_edges / db.vertical_edges`

无论采用哪种方式，文档中的“router 当前轮内部 usage”都指这份工作态，而不是 `problem`、也不是重新 parse 出来的平行数据库。

请在 `include/router.h` 中声明至少以下 API：

```cpp
#pragma once

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
```

说明：

- `NetsToRoute` 是本轮要 route 的 net 集合，元素类型是 `Net.id`
- `DetermineRoutingOrder` 只对 `nets_to_route` 选中的 nets 排序，返回值仍是 `problem.nets` 的下标顺序，不是 net id
- `AStarToTree` 只读当前 routing 状态，不直接修改 usage
- `RouteNetAsTree` 只生成单个 net 的树
- `WriteRoutedTreeToUsage` 只把 tree 累加到 router 当前轮内部 usage，不直接写回外部 `EdgeState`
- `WriteUsageToEdgeState` 在整轮结束后，把本轮最终 usage 统一写回 `db.horizontal_edges / db.vertical_edges`
- `RunRouting` 是通用入口，负责对显式子集做整轮 routing、history 更新和轮末提交
- `RunInitialRouting` 只是 convenience wrapper：收集全部 `Net.id` 后调用 `RunRouting`

额外契约：

- `RoutingDB` 中对外可见的 `horizontal_edges / vertical_edges` 表示“当前已提交状态”
- router 工作态可以在一轮内部被频繁更新，但对外可见状态只能通过 `WriteUsageToEdgeState(...)` 在轮末统一覆盖
- `RunRouting(...)` 返回的 `RoutingResult.routed_nets` 只包含本轮显式传入子集的结果，不隐含“全局所有 net 都在这里”

---

# 三、核心流程

## 1. routing order

`DetermineRoutingOrder(db, nets_to_route)` 的语义：

1. 先从 `db.problem->nets` 中筛出 `net.id` 属于 `nets_to_route` 的 nets
2. 只对这个真子集排序
3. 返回这些 nets 在 `problem.nets` 中的下标顺序

排序优先级：

- bounding-box perimeter descending
- pin count descending
- parser input order

说明：

- `NetsToRoute` 的元素是 `Net.id`，不是 `problem.nets` 下标
- 不能先对全量 nets 排序再过滤；必须先筛子集，再排序
- “parser input order” 精确定义为 `db.problem->nets` 中的原始出现顺序，也就是较小的数组下标优先

## 2. `RouteNetAsTree`

`RouteNetAsTree(const RoutingDB& db, const Net& net)` 内部自行初始化空树：

1. 记录 `net_id / net_name`
2. 选择一个 seed pin 作为初始树
3. 将 seed pin 放入 `tree_points`
4. 其余 pins 放入 unconnected
5. 循环直到 unconnected 为空：
   - 选择一个待接入 pin
   - 运行 `AStarToTree(...)` 从该 pin 连到当前树
   - 成功后把 path 合并进 `tree_points` 和 `branches`
   - 将该 pin 从 unconnected 中移除

seed pin 选择策略：

- 对每个 pin，计算它到其余所有 pins 的总曼哈顿距离
- 选总距离最小者
- 若并列，选 `net.pins` 中下标更小者

建议封装：

```cpp
Point ChooseSeedPin(const Net& net);
```

选择下一条待接入 pin 的策略：

- 对每个 unconnected pin，计算它到当前 `tree_points` 的最小曼哈顿距离
- 选最小者
- 若并列，保留容器中的稳定顺序

建议封装：

```cpp
Point ChooseNextPinToConnect(
    const std::vector<Point>& unconnected,
    const std::set<Point>& tree_points);
```

额外要求：

- `RouteNetAsTree(...)` 只能读取当前轮工作态下的 edge cost / usage 视图，不能自己创建新的 usage 副本
- `RouteNetAsTree(...)` 不负责提交 usage；提交动作只能由 `WriteRoutedTreeToUsage(...)` 完成
- 若某个 pin 无法接入当前树，则立即抛出 `std::runtime_error`，不允许静默跳过该 pin

## 3. `AStarToTree`

`AStarToTree(...)` 必须按 README 的 single-source to multi-target A* 实现。

输入：

- `db`
- `start`
- `tree_points`

输出：

- 成功时返回完整路径、attach 点和路径代价
- 失败时返回 `success = false`

邻居扩展：

- 四个方向：`kUp / kDown / kLeft / kRight`
- 复用 `tools.h` 的 `HasEdge(...) / IsEdgeAvailable(...) / NeighborPoint(...) / GetEdgeState(...)`
- blocked cell 不能进入
- 不可用 edge 不能扩展

搜索状态：

```cpp
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
```

要求：

- `open_map = idx -> NodeRecord`
- `close_map = idx -> NodeRecord`
- `open_heap` 按 `f`、再按 `g` 排序
- 起点先进入 `open_map`
- `open_heap` 允许过期条目，出堆后校验
- 进入 `close_map` 后不 reopen
- heuristic 使用 `min_{t in tree_points} ManhattanDistance(p, t)`

命中 `tree_points` 时：

- 从命中点沿 `parent` 回溯到 `start`
- 构造正向路径
- 返回 `PathSearchResult`

失败时：

- open 为空仍未命中 tree，则返回失败结果
- `RouteNetAsTree` 遇到无法接入的 pin 直接抛 `std::runtime_error`

路径语义必须统一为：

- 成功返回时，`path` 永远按“从 `start` 到 `attach_point` 的正向顺序”存储
- `path` 必须包含两端点
- 若 `start` 已经属于 `tree_points`，返回 `success = true`、`attach_point = start`、`path = {start}`、`path_cost = 0`
- 不允许把“零边路径”编码成空数组；长度为 1 的 `path` 才表示零边路径

建议补充内部 helper：

```cpp
int PointIndex(const RoutingDB& db, Point p);
Point PointFromIndex(const RoutingDB& db, int idx);
double HeuristicToTree(Point p, const std::set<Point>& tree_points);
```

## 4. tree merge 与统计

当 `AStarToTree(...)` 成功后：

- 将 path 中所有点并入 `tree.tree_points`
- 将整条 path push 到 `tree.branches`
- 从 unconnected 中移除该起点

统计规则：

- `total_wirelength` = 所有 branch 的边数总和，即每条 path 贡献 `path.size() - 1`
- `total_overflow` = 所有 edge 的 `max(0, usage - capacity)` 之和
- `WriteRoutedTreeToUsage(...)` 的语义是“更新本轮内部 usage 镜像”，不是“立刻写回 `EdgeState`”

`WriteRoutedTreeToUsage(...)` 的精确定义：

- 它遍历 `tree.branches` 中每条 branch 的相邻点对
- 每对相邻点必须是 Manhattan 相邻；否则抛异常
- 对每条合法 edge，在 router 工作态中执行一次 `usage[e] += 1`
- 不修改 `tree`
- 不更新 `history_cost`
- 不把工作态立刻写回对外可见的 `EdgeState`

如果同一条 edge 在同一个 net 的不同 branch 中出现多次，按出现次数累加 usage；本模块不做 edge 去重优化。

## 5. `RunRouting` 与 `RunInitialRouting`

`RunRouting(db, nets_to_route)` 是 router 的通用主入口：

1. 校验 `db.problem` 非空
2. 校验 `nets_to_route` 中的每个 `Net.id` 都存在
3. 调用 `DetermineRoutingOrder(db, nets_to_route)`
4. 只对该子集 nets 按 order 逐条 route
5. 每条 net 完成后调用 `WriteRoutedTreeToUsage(...)`
6. 整轮结束后更新 history cost
7. 调用 `WriteUsageToEdgeState(...)`
8. 返回 `RoutingResult`

`RunInitialRouting(db)` 只是薄封装：

1. 从 `db.problem->nets` 收集全部 `Net.id`
2. 构造 `NetsToRoute all_net_ids`
3. 调用 `RunRouting(db, all_net_ids)`

后续 RRR 兼容性：

- 由上层先决定 victim net 子集
- 再把这些 `Net.id` 作为 `NetsToRoute` 传给 `RunRouting(...)`
- router 不负责自己决定 reroute 哪些 nets

必须写清楚 reroute 调用契约：

- `RunRouting(...)` 从不执行 rip-up
- 若本轮 `nets_to_route` 是 reroute victim 子集，则调用前必须由上层先把这些 victim nets 的旧 routed tree 从工作态 usage 中移除
- 调用 `RunRouting(...)` 时，router 假定传入的当前工作态已经反映了“非 victim nets 仍保留、victim nets 已被 rip-up”之后的状态
- 如果上层没有先 rip-up 就直接把已存在旧树的 victim nets 再交给 `RunRouting(...)`，则 usage 会被重复累计；这属于错误调用，不由 router 自动修正

关于结果保存：

- 为了兼容后续 rip-up / reroute，上层或共享数据库必须能够保存 `net_id -> RoutedTree`
- 本模块至少要保证 `RoutedTree` 的内容足以支持后续 rip-up；如果当前仓库尚未提供全局存储位置，可以先由调用方保存 `RunRouting(...).routed_nets`
- router 本身不负责决定这些 tree 的生命周期策略，但其输出必须可被后续 RRR 直接复用

---

# 四、Negotiated-Congestion 规则

请把 cost model 明确写成 negotiated-congestion routing，而不是简单读取静态 `present_cost`。

每条 edge 的逻辑状态：

- `base_cost[e]`：静态基础代价，默认可取 `1`
- `usage[e]`：router 在当前轮内部维护的 edge 使用量
- `capacity[e]`：edge 容量，从已有数据结构读取，固定不变
- `hist_cost[e]`：跨轮累积的历史拥塞代价，初始化为 `1`

说明：

- 文档中的 `hist_cost` 对应实现里的 `EdgeState.history_cost`（若 `EdgeState.history_cost` 不存在，则补上）
- `present_cost` 视为由当前 `usage` 在线计算的逻辑量，不要求整轮持久化成独立数组
- `EdgeState.present_cost` 若保留在结构体中，只能视为调试或快照字段，不能成为搜索代价的唯一真实来源
- 搜索代价的真实来源必须是“当前工作态 usage 在线计算出的 `present_cost`”

搜索时使用：

```cpp
edge_cost[e] = base_cost[e] * present_cost[e] * hist_cost[e];
```

其中：

```cpp
if (usage[e] < capacity[e]) {
    present_cost[e] = 1;
} else {
    present_cost[e] = 1 + lambda * (usage[e] - capacity[e] + 1);
}
```

参数含义：

- `lambda`：当前轮内的即时拥塞惩罚系数，可取中等正数
- `mu`：历史代价累积系数，可取较小正数

一轮 routing 的 usage / cost 时序：

1. 开始一轮前，从 `EdgeState` 加载当前 `usage` 与已有 `history_cost`
2. 按 `DetermineRoutingOrder(db, nets_to_route)` 产生的顺序逐条 route 子集 nets
3. 每条 net 成功后，调用 `WriteRoutedTreeToUsage(...)`，立刻对其路径上的 edge 执行内部 `usage[e] += 1`
4. 后续 net 的搜索直接使用更新后的内部 usage，因此 `present_cost` 会立刻生效
5. 整轮所有 nets 完成后，统一计算：

```cpp
overflow[e] = max(0, usage[e] - capacity[e]);
hist_cost[e] = hist_cost[e] + mu * overflow[e];
```

6. history cost 更新后，再调用 `WriteUsageToEdgeState(...)`，把本轮最终 usage 一次性写回外部 `EdgeState`

这里的“加载当前 `usage` 与已有 `history_cost`”精确定义为：

- initial routing 时，初始工作态通常来自 `initializer` 产出的全零 `usage` 与全 1 `history_cost`
- reroute 时，工作态来自上层 rip-up 完 victim 之后的当前状态
- router 不能重新推导“哪些旧树应该保留”，只能接收上层交给它的当前工作态继续 route

设计意图：

- `present_cost` 反映当前这一轮、当前时刻是否拥塞
- `hist_cost` 反映某条 edge 是否在多轮中反复成为热点
- 前者是短期即时惩罚，后者是长期累积惩罚
- A* 只负责在当前 `edge_cost` 下找最小代价路径，不负责定义 history 更新规则

不要写成以下错误语义：

- history cost 保存上一轮 current cost
- 所有 cost 都只在整轮结束后统一更新
- route 一条 net 后不修改内部 usage
- route 一条 net 后就立刻把 usage 写回 `EdgeState`
- A* 自己负责 negotiated-congestion 更新
- `RunRouting(...)` 会自动替上层完成 rip-up

---

# 五、调试、接入与边界条件

## debug

提供：

```cpp
void SetRouterDebug(bool enabled);
```

开启后输出适量摘要日志，例如：

```text
[router] routing order: 0 1 2 3
[router] start net name=net3 id=3 pin_count=4 seed=(10,8)
[router] net edge name=net3 id=3 from=(10,8) to=(10,9)
[router] net edge name=net3 id=3 from=(10,9) to=(10,10)
[router] committed net name=net3 id=3 edge_count=2
[router] initial routing done nets=20 total_wirelength=356 total_overflow=12
```

其中 debug 语义还应明确为：

- 除了 routing order / start / done 摘要外，debug 模式下还要输出每条 net 提交到 usage 的每一个 edge
- edge 日志的坐标语义固定为 `from=(r0,c0) to=(r1,c1)`，对应 `tree.branches` 中的相邻点对
- edge 明细按实际提交顺序输出，不去重
- 若同一条 edge 被同一 net 重复提交，则日志也按出现次数重复输出，因为 `usage` 同样按次数累加
- `path.size() == 1` 的 branch 合法，但不输出 edge 行

## 接入

`Makefile`：将 `src/router.cc` 加入 `SOURCES`。

`main.cc`：在 parse + initialize 后接入：

```cpp
const RoutingResult result = RunInitialRouting(db);
std::cout << "main.route_ok routed_nets=" << result.routed_nets.size()
          << " wirelength=" << result.total_wirelength
          << " overflow=" << result.total_overflow << "\n";
```

或者显式调用通用入口：

```cpp
NetsToRoute all_net_ids;
for (const Net& net : db.problem->nets) {
    all_net_ids.insert(net.id);
}
const RoutingResult result = RunRouting(db, all_net_ids);
```

必要时可打开：

```cpp
SetRouterDebug(true);
```

## 边界条件

- `nets_to_route` 为空：`RunRouting(...)` 返回空的 `routed_nets`，并基于当前状态统计 wirelength / overflow
- `nets_to_route` 中包含不存在的 `Net.id`：抛 `std::invalid_argument` 或 `std::runtime_error`
- `net.pins.size() < 2`：可防御式报错
- `tree_points` 为空：`AStarToTree` 抛 `std::logic_error`
- 路径长度为 `1`：合法，但不会增加 edge usage
- path 中相邻点若不是 Manhattan 相邻：commit 时抛异常
- `db.problem == nullptr`：沿用现有 helper 的异常语义
- `NetsToRoute` 允许是严格真子集，也允许是全部 nets；但不允许同一个 `Net.id` 重复出现
- 若 `nets_to_route` 为空，`RunRouting(...)` 不应修改 usage 或 history cost

## 最终目标

实现一个能跑通 initial routing 最小闭环的 router：

- 读已有 `Problem / RoutingDB`
- 为 `NetsToRoute` 显式指定的 nets 构造 routed tree
- 用 multi-target A* 把 pins 逐步接入树
- 提交 edge usage
- 输出 wirelength / overflow
- 为后续 rrr 保留“对子集 reroute”的稳定接口

# 补充约束：

- 若 `start` 已经属于 `tree_points`，则返回 `path = {start}` 的合法零边路径。
- 建议 `base_cost[e] = 1`，不要重复叠加另一份长度惩罚。
- 对于 `overflow[e] == 0` 的 edge，`hist_cost[e]` 保持不变；本模块不实现 history decay、reset 或其他额外动态。
- `README` 与本文件若对 negotiated-congestion 的 present/history 更新时机有冲突，以本文件为准；实现完成后应同步保持两者一致。
