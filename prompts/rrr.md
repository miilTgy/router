你现在为该 C++ 仓库实现一个全新的 RRR 模块，**绝对禁止改动任何已有 router 模块代码**，包括但不限于 `include/router.h`、`src/router.cc` 内的任何函数、签名、实现、常量、调试输出、history cost 更新逻辑。只能新增文件，并在 `main.cc` 做最小接入改动。

## 目标
新增 `rrr` 模块，实现如下流程：

1. 以 `RunInitialRouting(db)` 的结果作为 RRR 初始全局解。
2. 在 RRR 中维护一个**全局 RoutingResult**，表示当前完整 routing 结果。
3. 每一轮 RRR：
   - 找出所有 overflow edge。
   - 找出所有“趟过 overflow edge”的 nets，作为 victims。
   - 从 RRR 维护的全局 `RoutingResult` 中删除这些 victims 对应的 `RoutedTree`。
   - 基于删除 victims 后剩余的全局解，**在RRR内部重建 db 的 usage/edge state**。
   - 调用现有 `RunRouting(db, victim_net_ids)` 复用 router 完成 reroute。
   - `RunRouting()` 返回的只是 victims 的 reroute 结果，把它们并回 RRR 的全局 `RoutingResult`。
4. 若本轮没有 victim，或 overflow 已为 0，则停止。
5. 返回 RRR 后的**完整全局 routing 结果**并更新外部 routingDB，供 writer 直接输出。

## 强约束
- **禁止修改 router 模块分毫。**
- **禁止在 rrr 中再次更新 history cost。** 因为 `RunRouting()` 内部已经做了 history cost 更新，RRR 只负责 orchestration。
- 不要重复实现 A*、tree routing、cost 计算、history cost 更新；这些全部调用 router 中的 **API**。
- RRR 不要维护自己的 edge cost 模型，只维护“全局解”和“victim 选择/删改/重建/合并”。
- 代码风格保持与现有仓库一致：明确、保守、可读、异常检查充分。

## 需要新增的文件
- `include/rrr.h`
- `src/rrr.cc`

## main.cc 的最小改动
只做线性接入：
- `#include "rrr.h"`
- initial routing 后调用 `RunRRR(db, initial_result)`
- writer 输出 `final_result`

目标流程：
1. parse
2. init db
3. initial routing
4. (option) write initial routing 输出写死在 main 中就像当前这样，后面我不想输出了再直接注释掉
5. rrr
6. write

## rrr.h 要暴露的 API
只暴露一个主接口，签名如下：

```cpp
#pragma once

#include "common.h"

RoutingResult RunRRR(
    RoutingDB& db,
    const RoutingResult& initial_result);
```

不要暴露内部辅助函数。

## rrr.cc 内部设计要求

### 1. 建立 net_id -> problem->nets index 的映射
需要一个内部辅助函数，从 `db.problem->nets` 构建 `unordered_map<int, int>`，用于按 `net_id` 定位 net。

### 2. RRR 自己维护完整解
内部维护：
- `std::vector<RoutedTree> current_routed_nets`
- 必须表示“当前完整全局解”
- 初始值来自 `initial_result.routed_nets`

可选维护：
- `best_routed_nets`
- `best_total_overflow`
- `best_total_wirelength`

建议保留 best solution，避免某轮变差后丢掉更优解。

### 3. 必须支持从“当前完整解”重建 db usage / edge state
实现内部辅助函数，功能如下：

#### ClearWorkingUsage
- 将 `db.working_horizontal_usage`
- `db.working_vertical_usage`
清零并 resize 到与 edge 数一致。为了在删除 victim 后干净地重建 usage。

#### ClearCommittedEdgeUsageOnly
- 将 `db.horizontal_edges[i].usage = 0`
- 将 `db.vertical_edges[i].usage = 0`
- **不要改 base_cost / present_cost / history_cost**
- present_cost 后续通过 `WriteUsageToEdgeState(db)` 统一刷新

#### RebuildDBFromGlobalSolution
输入完整 `std::vector<RoutedTree>`
执行：
1. 清空 working usage
2. 清空 committed edge usage
3. 对每棵非空 tree 调用现有 `WriteRoutedTreeToUsage(db, tree)`
4. 调用现有 `WriteUsageToEdgeState(db)`

注意：
- 这是 RRR 最核心的状态同步函数
- 不能自己手写 edge index 累加逻辑，尽量复用 router 现有公开 API：`WriteRoutedTreeToUsage`、`WriteUsageToEdgeState`
- tree 为空的判断方式要稳妥，例如 `net_id == -1` 或 `branches.empty() && tree_points.empty()`；请定义一个内部统一判断函数

### 4. victim 选择逻辑
实现内部辅助函数，从“当前完整全局解 + 当前 db edge usage”中选出 victims。

要求：
1. 先扫描所有 horizontal/vertical edge，找出 overflow edge。
2. 如果没有 overflow edge，返回空的 `NetsToRoute`。
3. 对每棵 `RoutedTree`，判断它是否经过任意 overflow edge。
4. 若经过，则把该 tree 的 `net_id` 加入 victims。

### 5. “tree 是否经过 overflow edge”的判断
实现内部辅助函数，例如：

```cpp
bool TreeTouchesOverflowEdge(const RoutingDB& db, const RoutedTree& tree);
```

要求：
- 遍历 `tree.branches`
- 对每条 branch 的相邻点对 `(from, to)` 求方向
- 用与 router 一致的方式获取该 edge 的 overflow 状态
- 可直接复用 `DirectionBetweenPoints` 的等价逻辑，但因为它在 router.cc 匿名 namespace 内不可见，所以在 rrr.cc 内实现一个自己的本地版本，逻辑必须一致：
  - up / down / left / right
  - 非 Manhattan 相邻点时报错
- 使用 `GetEdgeState(db, r, c, dir)` + `EdgeCapacity(db, dir)` 或 `EdgeOverflow(db, r, c, dir)` 判断 overflow
- 只要任意一条 edge overflow，即该 tree 是 victim

### 6. 从全局解中删除 victims
实现内部辅助函数：
- 输入 `current_routed_nets` 与 `victim_net_ids`
- 返回删去 victims 后的新 `std::vector<RoutedTree>`
- 或原地 erase 也可以
- 必须保证删掉的是完整 `RoutedTree` 级别，而不是只清 branches

建议直接过滤生成新 vector，更稳。

### 7. 合并 reroute 结果
实现内部辅助函数，将 `RunRouting(db, victim_net_ids)` 返回的 `partial_result.routed_nets` 并回当前完整解。

要求：
- 当前完整解此时已经删掉 victims，因此直接 append partial reroute 结果即可
- 合并后要保证：
  - net_id 不重复
  - 解的规模正确
- 最好在 debug/assert 中检查 net_id 唯一性

### 8. 线性主循环
`RunRRR(db, initial_result)` 逻辑要求如下：

1. 先校验 `db.problem != nullptr`
2. 用 `initial_result.routed_nets` 初始化当前完整解
3. 先从当前完整解重建一次 db
4. 计算初始 overflow / wirelength
5. 保存 best solution
6. 进入迭代循环，建议固定最大轮数，例如 `constexpr int kMaxRRRIterations = 50;`

每轮：
1. 基于当前完整解重建 db
2. 计算当前 total overflow
3. 若 overflow == 0，停止
4. 选择 victims
5. 若 victims 为空，停止
6. 从当前完整解中删除 victims
7. 基于“删 victim 后的剩余解”重建 db
8. 调用 `RunRouting(db, victims)` 得到 partial reroute
9. 将 partial reroute 合并回当前完整解
10. 基于合并后的完整解再次重建 db
11. 计算新的 total overflow / total wirelength
12. 若优于 best，则更新 best

循环结束后：
1. 用 best solution 重建 db
2. 组装并返回完整 `RoutingResult`
   - `routed_nets = best_routed_nets`
   - `total_wirelength = ComputeTotalWirelength(best_routed_nets)`
   - `total_overflow = ComputeTotalOverflow(db)`

### 9. 比较 best solution 的规则
优先级：
1. 更小的 total_overflow 更优
2. total_overflow 相同，则更小的 total_wirelength 更优

### 10. debug 输出
风格与现有仓库一致，简洁、单行、前缀明确。
新增一个本地 debug 开关，例如：

```cpp
namespace {
bool g_rrr_debug_enabled = true;
}
```

以及：
```cpp
void SetRRRDebug(bool enabled);
```

如果你愿意，也可以在 `rrr.h` 暴露 `SetRRRDebug(bool enabled);`，然后在 `main.cc` 调用；这是允许的新增接口，不影响 router。

建议输出：
- 每轮迭代编号
- 当前 overflow
- victim 数量
- reroute 后 overflow / wirelength
- best overflow / wirelength
- 停止原因

但不要冗长，不要打印每条边、每条 net 的细节。

## 边界与健壮性要求
- 若 `initial_result.routed_nets` 为空，直接返回它或基于它重建后返回。
- 若某个 `RoutedTree` branch 中存在非 Manhattan 相邻点，抛异常。
- 若 victim reroute 后返回了未知 net_id，抛异常。
- 若合并后 net_id 重复，抛异常。
- 若最终 best solution 为空但 problem 中有 nets，允许返回空结果但应保持逻辑自洽，不要崩溃。
- 不要依赖 `RoutingResult.routed_nets` 的顺序与 `problem->nets` 顺序一致；所有定位以 `net_id` 为准。

## 代码实现偏好
- 使用匿名 namespace 放内部辅助函数
- 使用 `std::unordered_map<int, int>` 做 net_id 映射
- 使用 `std::unordered_set<int>` 做去重/检测也可以
- 代码要能直接编译
- 不写伪代码，直接给出完整可编译实现
- 不要留 TODO
- 不要输出解释，直接给出需要新增/修改的完整代码内容
- 不要滥用 try-catch-except 异常控制语句，多用 if-else

## 需要输出的内容
请直接输出以下文件的完整最终代码：

1. `include/rrr.h`
2. `src/rrr.cc`
3. `src/main.cc` 的完整修改后版本

除这三个文件外，不要修改任何其他文件。