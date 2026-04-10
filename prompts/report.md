# Global Routing Project Report

## 1. 项目概述

本项目实现了一个基于二维网格模型的 global router。整体流程为：

1. 解析输入样例，得到 grid、capacity、block、net 等信息；
2. 初始化 routing database，建立横向/纵向边状态；
3. 执行 initial routing；
4. 在 initial routing 结果基础上执行 rip-up and reroute (RRR)；
5. 将最终布线结果写入输出文件。

程序主流程在 `main.cc` 中实现：先调用 `ParseInputFile` 和 `InitializeRoutingDB`，再依次调用 `RunInitialRouting`、`RunRRR` 和 `WriteRoutingSolution`。  
`RoutingDB` 中维护了横向边、纵向边的 usage / cost 状态，以及 router 当前轮使用的 working usage。  
整个工程的设计是“parser / initializer / router / rrr / writer”分层，模块边界较清晰。

---

## 2. 数据结构设计

### 2.1 输入问题建模

输入数据使用 `Problem` 表示，主要字段包括：

- `rows`, `cols`：网格大小
- `vertical_capacity`, `horizontal_capacity`：纵向/横向边容量
- `blocks`：阻塞点集合
- `nets`：待布线网络集合
- `blocked`：阻塞位图

每个 net 用 `Net` 表示，包含：

- `name`
- `id`
- `pins`

每个坐标点由 `Point(r, c)` 表示，并重载了比较运算，方便放入 `set` 中管理。

### 2.2 边状态建模

每条边使用 `EdgeState` 表示，字段包括：

- `usage`：当前已使用次数
- `base_cost`：基础代价
- `present_cost`：当前拥塞代价
- `history_cost`：历史拥塞代价

其中 `present_cost` 与 `history_cost` 共同支撑 negotiated-congestion routing。

### 2.3 布线结果建模

单个 net 的树形结果由 `RoutedTree` 表示，包含：

- `net_id`
- `net_name`
- `tree_points`
- `branches`

其中 `branches` 保存每次把一个 pin 接到已有树上的路径。  
最终结果由 `RoutingResult` 表示，包含：

- `routed_nets`
- `total_wirelength`
- `total_overflow`

---

## 3. 算法流程

## 3.1 Initial Routing 总体流程

initial routing 由 `RunInitialRouting` 调用 `RunRouting` 完成。  
`RunInitialRouting` 会先收集所有 `Net.id`，然后统一交给 `RunRouting`。

`RunRouting` 的主要步骤如下：

1. 校验 `db.problem` 和待布线 net id 合法性；
2. 从当前已提交的 edge state 加载 working usage；
3. 调用 `DetermineRoutingOrder` 确定本轮 net 顺序；
4. 按顺序逐条对 net 调用 `RouteNetAsTree`；
5. 每条 net 完成后，调用 `WriteRoutedTreeToUsage` 把该 net 占用写入 working usage；
6. 所有 net 完成后，根据最终 working usage 更新 history cost；
7. 调用 `WriteUsageToEdgeState` 将 working usage 提交回 edge state；
8. 统计总线长与总 overflow。

也就是说，本项目不是“所有 net 全部 route 完再统一更新当前拥塞”，而是 **每 route 完一条 net，就立刻让后续 net 感受到新的拥塞 usage**；而 history cost 则在整轮结束后统一更新。

## 3.2 Net 的布线顺序

`DetermineRoutingOrder` 先从全部 nets 中筛出本轮要 route 的子集，再用稳定排序确定顺序。排序规则为：

1. **bounding-box perimeter 降序**
2. **pin 数量降序**
3. **输入文件中的原始顺序优先**

这相当于优先处理规模更大、连接更复杂的 net，尽量减少后续更难处理的拥塞。

## 3.3 单个 Net 的树构造

单个 net 的布线由 `RouteNetAsTree` 完成，采用“逐步把 pin 接入已有树”的策略。

### (1) 选择 seed pin

`ChooseSeedPin` 对每个 pin 计算其到其他所有 pin 的曼哈顿距离总和，选取总和最小的 pin 作为 seed。  
这样做的直觉是：先从几何中心附近的 pin 建树，更容易让后续路径更短。

### (2) 选择下一个待连接 pin

`ChooseNextPinToConnect` 对每个未连接 pin，计算它到当前树上任意点的最小曼哈顿距离，选取最近的 pin 接入。  
因此整个过程类似于一个贪心式增量 Steiner tree 构造。

### (3) A* 连到当前树

对于选出的 pin，调用 `AStarToTree(start, tree_points)`，从该 pin 出发，用 A* 搜索连到当前树中的任意一个点。  
一旦命中 `tree_points` 中的某个点，就停止搜索，并通过 parent 指针回溯出完整路径。

### (4) 合并进 routed tree

A* 返回路径后：

- 将路径上的所有点加入 `tree_points`
- 将整条路径加入 `branches`
- 从 `unconnected` 中删去刚接入的 pin

直到所有 pin 都接入树中为止。

---

## 4. A* 搜索与代价模型

## 4.1 搜索状态

A* 采用以下结构：

- `open_map`：记录当前 open 集中的节点状态
- `close_map`：记录已经扩展完成的节点状态
- `open_heap`：按 `(f, g, idx)` 排序的优先队列

其中：

- `g`：从起点到当前点的累计真实代价
- `f = g + h`
- `h`：到当前树的最小曼哈顿距离

本实现不支持 reopen：节点进入 `close_map` 后不会重新打开。

## 4.2 合法扩展条件

A* 每次从当前点尝试向四个方向扩展：

- `Up`
- `Down`
- `Left`
- `Right`

扩展前要求：

1. 该方向存在边；
2. 该边可用；
3. 边的两个端点都不在 blocked cell 上。

因此阻塞点会直接切断相关边的可用性。

## 4.3 代价函数

本项目采用 negotiated-congestion edge cost。  
边代价计算为：

\[
\text{edge\_cost} = \text{base\_cost} \times \text{present\_cost} \times \text{history\_cost}
\]

其中：

- `base_cost` 默认是 1
- `history_cost` 初值为 1，并在每轮结束后累积
- `present_cost` 由当前 working usage 在线计算：

\[
\text{present\_cost}=
\begin{cases}
1, & usage < capacity \\
1 + \lambda \cdot (usage - capacity + 1), & usage \ge capacity
\end{cases}
\]

代码中取：

- `lambda = 1.0`
- `mu = 0.5`

这意味着：

- **present cost** 负责“本轮即时拥塞惩罚”
- **history cost** 负责“多轮反复拥塞的长期惩罚”

因此该 router 不是简单最短路，而是在长度与拥塞之间做平衡。

---

## 5. Overflow 处理与 RRR

## 5.1 Victim 选择

RRR 在 `RunRRR` 中实现，最多迭代 50 轮。  
每一轮先根据当前全局解重建数据库状态，然后检查是否仍有 overflow edge。

若存在 overflow，则调用 `SelectVictimNetIds` 选择 victim nets。  
当前实现的 victim 判定规则比较直接：

- 只要某个 net 的 routed tree 经过了 overflow edge，它就会被选为 victim。

## 5.2 Rip-up 与局部重布

选出 victims 后，RRR 会：

1. 从当前全局解中移除这些 victim nets；
2. 用剩余 nets 重建数据库 usage；
3. 仅对 victim 子集调用 `RunRouting` 重新布线；
4. 将新的部分结果并回全局解；
5. 重新统计 overflow 与 wirelength。

因此该实现的 RRR 属于 **基于 overflow edge 的 victim reroute**，而不是全局全部重跑。

## 5.3 最优解保留策略

在 RRR 过程中，程序维护当前 best solution。  
比较规则为：

1. 优先 total overflow 更小；
2. 若 overflow 相同，则优先 total wirelength 更小。

最终返回的是整个迭代过程中观察到的 best solution，而不一定是最后一轮的结果。

---

## 6. 输出与合法性检查

最终结果写入 `results/xxx_solution.txt`。  
输出时按 `net_id` 升序排序，每个 net 输出形式为：

- 先输出 net 名称
- 再逐行输出 `(r1, c1)-(r2, c2)` 的边
- 最后以 `!` 结束

在输出前，`ExpandTreeToEdges` 会检查每一段路径是否真的是 Manhattan 相邻，若发现非法 branch 会直接报错。  
因此 writer 模块不仅负责写文件，也承担了最终结果的一部分一致性校验。

---

## 7. 实验结果

## 7.1 实验环境

- 编译环境：`Ubuntu 24.04 / g++ 14.2.0`
- 编译选项：`-std=c++17 -Wall -Wextra -Wpedantic -O3 -Iinclude`
- 测试数据集：`samples/sample0.txt ~ samples/sample4.txt`

## 7.2 结果汇总

| Case | Initial Wirelength | Initial Overflow | Final Wirelength | Final Overflow | Iterations |
|---|---:|---:|---:|---:|---:|
| sample0 | 12 | 0 | 12 | 0 | 1 |
| sample1 | 8233 | 1 | 8233 | 0 | 2 |
| sample2 | 10351 | 1 | 10350 | 0 | 2 |
| sample3 | 10497 | 2 | 10497 | 0 | 2 |
| sample4 | 12510 | 0 | 12510 | 0 | 1 |

## 7.3 结果分析

- initial routing 的表现：initial routing 在较简单样例上已经能直接得到可行解，`sample0` 和 `sample4` 的初始 overflow 都为 0；在规模更大、拥塞更明显的样例上，初始结果也只留下了较小的残余冲突，`sample1`、`sample2`、`sample3` 的初始 overflow 分别为 1、1、2。整体来看，initial routing 已经能够给出线长较短、接近可行的解，但在更困难的 benchmark 上仍会出现少量拥塞冲突。
- RRR 对 overflow 的改善效果：RRR 对剩余 overflow 的消除效果较明显。对于 `sample1`、`sample2` 和 `sample3`，都在第一次有效 reroute 后把 overflow 降到了 0，并在 `iter=2` 时检测到无 overflow 后停止；而 `sample0` 和 `sample4` 初始就没有 overflow，因此在 `iter=1` 就直接退出。说明当前的 victim-based RRR 对小规模残余拥塞具有较强修复能力。
- wirelength 与 congestion 的权衡：从结果看，当前数据集上消除 overflow 并没有带来明显的线长膨胀。`sample1` 和 `sample3` 都是在保持 wirelength 不变的情况下把 overflow 降为 0；`sample2` 甚至从 10351 进一步下降到 10350。这说明当前 RRR 在本实验中较好地控制了 wirelength 与 congestion 之间的权衡，能够以很小的代价完成拥塞修复。
- 典型案例分析：`sample3` 可以作为较典型的困难案例。它的 initial overflow 为 2，是五个样例中初始拥塞最严重的一个；在第一次 RRR 中，算法选择了 10 个 victim nets 进行 rip-up and reroute，随后就把 overflow 降到了 0，同时最终 wirelength 仍保持在 10497，没有额外增长。与之对比，`sample2` 也在一次有效 reroute 后把 overflow 从 1 降到 0，并且 wirelength 还减少了 1，说明当前 reroute 策略并不只是单纯用更长路径换取可行性。

---

## 8. 实现特点与不足

### 优点

1. **模块划分清晰**  
   parser、initializer、router、rrr、writer 分工明确，便于调试和扩展。

2. **代价模型完整**  
   同时考虑了 base / present / history 三类代价，具备 negotiated-congestion routing 的基本特征。

3. **支持子集 reroute**  
   `RunRouting` 可以只处理指定 victim 子集，为后续 RRR 复用提供了稳定接口。

4. **结果结构适合 rip-up**  
   `RoutedTree` 直接保存树点和 branch 路径，方便后续重建 usage 或执行 reroute。

### 不足

1. **树构造仍是贪心式的**  
   目前采用“最近 pin 接到当前树”的策略，容易陷入局部最优。

2. **A* 的 heuristic 较简单**  
   仅使用到树的最小曼哈顿距离，没有考虑更复杂的 congestion-aware 启发信息。

3. **victim 选择较粗糙**  
   当前只按是否经过 overflow edge 来选 victim，没有结合 detour、拥塞贡献度等更细粒度指标。

4. **RRR 主要依赖重复 reroute**  
   还没有加入更强的历史惩罚调度、区域引导或多源多汇优化。

---

## 9. 结论

本项目完成了一个可运行的 global routing 基本闭环：  
从输入解析、数据库初始化、initial routing，到基于 overflow 的 rip-up and reroute，再到结果输出，流程完整。

其核心思想是：

- 用 **incremental tree construction** 处理多 pin net；
- 用 **A\*** 实现单源到当前树的最小代价连接；
- 用 **negotiated-congestion cost** 在路径长度与拥塞之间进行权衡；
- 用 **victim-based RRR** 逐步降低 overflow。

从实现角度看，该项目已经具备课程作业级 global router 的完整框架；后续若继续改进，可重点从更强的 pin 接入策略、更聪明的 victim 选择和更有效的 congestion cost 调度三个方向提升结果质量。
