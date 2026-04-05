请为这个 C++ global routing 项目实现 initializer 模块。你需要严格基于现有 parser 输出的数据结构工作，不要重复解析输入文件。原始提示词见已上传文件，可作为你修改时的参考。:contentReference[oaicite:0]{index=0}

====================
一、项目上下文
====================

当前项目的数据流是：

main
  -> parser 解析输入文件，返回 Problem
  -> initializer 基于 Problem 构建 routing 初始化数据
  -> 后续 orderer / router / rip-up-reroute 使用 initializer 的结果

README 中 initializer 负责以下工作：
1. Construct implicit grid graph
2. Initialize edge usage and negotiated-congestion cost data
3. Build net list

其中：
- “Build net list” 不是重新 parse 输入文件
- parser 已经把原始 net 信息读进 `Problem.nets`
- initializer 中这一步更偏向于：校验、规范化、并为后续 router 提供可直接使用的 net 访问方式

现有 common.h 中已经有基础结构，parser.h 中已有对外接口。

所以 initializer 的输入必须是：

```cpp
const Problem& problem
```

不要自己读 sample 文件，不要自己 parse。

====================
二、你的实现目标
====================

请实现一个独立的 initializer 模块，建议新增：

- include/tools.h
- include/initializer.h
- src/initializer.cc

必要时可以扩展 include/common.h，加入 initializer 产出的共享数据结构，因为这些结构后续 router / cost update / rip-up-reroute 都会复用。与网格索引、边访问、邻居枚举相关的 inline helper 请放在 `include/tools.h`，不要放在 `common.h` 或 `initializer.h`。

目标是生成一个“初始化后的 routing database（名叫 RoutingDB）”，它不是 `Problem` 的拷贝，而是**在只读 Problem 之上附加的运行时 routing 状态**。也就是说：

- `Problem` 是静态输入
- `RoutingDB` 是动态 routing state
- `RoutingDB` 内部应持有一个指向 `Problem` 的只读指针，而不是复制 rows / cols / nets / blocked / capacities

RoutingDB 至少要描述：

1. 对原始 Problem 的只读引用
2. 隐式 grid graph 的边存在性
3. 每条边的 usage / history cost / present cost / base cost
4. 便于后续 A* 搜索快速查询邻居
5. 便于后续按 net 遍历和访问 problem 中已有的 net list

注意：
- graph 是 implicit grid graph，不要显式存一大堆 adjacency vector 给每个点塞所有边对象
- 但要提供足够方便的访问函数，让后续 A* 可以快速枚举四邻接边
- 重点是“轻量、清晰、适合 negotiated congestion router”
- 不要复制 `Problem.nets`
- 不要复制 `rows/cols/horizontal_capacity/vertical_capacity`
- 不要复制 `problem.blocked`，除非你有非常强的理由；默认直接通过 `problem` 访问即可

====================
三、设计原则
====================

请遵守以下设计原则：

1. 使用二维 grid，但底层表存储尽量采用一维连续数组，提升缓存友好性
2. 节点是 cell，边是相邻 cell 之间的 Manhattan edge
3. 一个 cell 最多有 4 个邻居：up/down/left/right
4. blocked cell 不可作为 routing node 使用
5. 若某条边任一端点 blocked，则该边视为不可用
6. 所有 horizontal edge 的 capacity 相同，均来自 `problem.horizontal_capacity`
7. 所有 vertical edge 的 capacity 相同，均来自 `problem.vertical_capacity`
8. 因为 edge capacity 在同方向上是全局常数，所以**不要为每条 edge 单独存 capacity**
9. edge usage 初始为 0
10. history cost 初始建议为 1.0
11. present congestion cost 初始建议为 1.0
12. base cost 初始建议按 unit wirelength 处理，即 1.0
13. overflow 可以由 `usage` 和全局 capacity 现场计算，所以**不要维护 overflow 表，也不要在 EdgeState 中存 overflow 字段**
14. 后续 router 会频繁访问边，所以边表必须清晰区分 horizontal edge 和 vertical edge
15. 不要引入过度复杂的 OO 层次，不需要 class hierarchy，不需要虚函数
16. 优先使用 plain struct + free functions 风格
17. 代码风格要求简洁、工程化、可 debug

====================
四、推荐的数据结构设计
====================

请在 common.h 中新增或改造出适合 initializer 的共享结构。

推荐采用如下方向（可以小幅调整，但必须保持同等清晰度）：

```cpp
enum class Dir : uint8_t {
    kUp = 0,
    kDown = 1,
    kLeft = 2,
    kRight = 3
};
```

```cpp
struct EdgeState {
    int usage = 0;

    double base_cost = 1.0;
    double present_cost = 1.0;
    double history_cost = 1.0;
};
```

注意：
- 不要给每一条 edge 存 from/to Point，这会冗余
- 不要给每一条 edge 存 capacity
- 不要给每一条 edge 存 overflow
- edge 的几何位置由所在表下标隐含表示

推荐将 edge 分成两张表：

1. horizontal_edges
   表示 (r, c) <-> (r, c+1)
   尺寸 = rows * (cols - 1)

2. vertical_edges
   表示 (r, c) <-> (r+1, c)
   尺寸 = (rows - 1) * cols

再定义初始化结果总结构：

```cpp
struct RoutingDB {
    const Problem* problem = nullptr;

    std::vector<EdgeState> horizontal_edges;
    std::vector<EdgeState> vertical_edges;
};
```

你也可以加少量统计字段，例如：

但不要把整份 Problem 再复制进去。

推荐在 `tools.h` 中补充一些只读 helper，让后续代码不必到处写 `db.problem->xxx`：

```cpp
inline int Rows(const RoutingDB& db);
inline int Cols(const RoutingDB& db);
inline int HorizontalCapacity(const RoutingDB& db);
inline int VerticalCapacity(const RoutingDB& db);
```

====================
五、索引规则（必须明确）
====================

请统一定义以下索引辅助函数，并放入 `tools.h` 中可 inline：

```cpp
inline int CellIndex(int r, int c, int cols) {
    return r * cols + c;
}
```

```cpp
inline int HorizontalEdgeIndex(int r, int c, int cols) {
    // edge between (r,c) and (r,c+1)
    return r * (cols - 1) + c;
}
```

```cpp
inline int VerticalEdgeIndex(int r, int c, int cols) {
    // edge between (r,c) and (r+1,c)
    return r * cols + c;
}
```

请同时提供边界判断函数，例如：

```cpp
inline bool InBounds(int r, int c, int rows, int cols);
inline bool HasRightEdge(int r, int c, int rows, int cols);
inline bool HasLeftEdge(int r, int c, int rows, int cols);
inline bool HasUpEdge(int r, int c, int rows, int cols);
inline bool HasDownEdge(int r, int c, int rows, int cols);
```

注意坐标约定：
- r 向下增大
- c 向右增大

所以：
- right: (r, c+1)
- left:  (r, c-1)
- down:  (r+1, c)
- up:    (r-1, c)

====================
六、initializer 需要提供的对外 API
====================

请实现以下接口，必要时可通过 `tools.h` 补充少量 helper，但不要过度设计：

```cpp
#pragma once

#include "tools.h"

RoutingDB InitializeRoutingDB(const Problem& problem);
void SetInitializerDebug(bool enabled);
```

====================
七、initializer 内部实现细节
====================

请严格按以下步骤实现 `InitializeRoutingDB(const Problem& problem)`：

--------------------------------
Step 1. 绑定 Problem，不复制 Problem
--------------------------------

RoutingDB 初始化时应保存：

```cpp
db.problem = &problem;
```

不要复制：
- rows
- cols
- horizontal_capacity
- vertical_capacity
- blocked
- nets

并校验：
- `problem.rows > 0`
- `problem.cols > 0`
- `problem.horizontal_capacity >= 0`
- `problem.vertical_capacity >= 0`

如发现非法输入，请抛出 `std::runtime_error`，报错信息要清晰。

注意：
- 该设计要求 `Problem` 的生命周期长于 `RoutingDB`
- 请在代码注释中简要说明这一点
- 这是本项目默认使用前提，典型用法是：
  `Problem problem = ParseInputFile(...); RoutingDB db = InitializeRoutingDB(problem);`

--------------------------------
Step 3. 初始化 horizontal / vertical edge tables
--------------------------------

建立：

```cpp
db.horizontal_edges.resize(problem.rows * std::max(0, problem.cols - 1));
db.vertical_edges.resize(std::max(0, problem.rows - 1) * problem.cols);
```

对每条 edge 填充：

horizontal edge:
- usage = 0
- base_cost = 1.0
- present_cost = 1.0
- history_cost = 1.0

vertical edge:
- usage = 0
- base_cost = 1.0
- present_cost = 1.0
- history_cost = 1.0

不要写：
- capacity = ...
- overflow = ...


--------------------------------
Step 6. 预计算全局统计信息（可选但推荐）
--------------------------------

可以统计：
- blocked cell 数
- edge 数
- net 数
- total pin 数

其中 net / pin 信息直接来自 `problem.nets`，不要复制。

以仅在 debug 输出时现算。

====================
十、debug 输出要求
====================

请实现：

```cpp
void SetInitializerDebug(bool enabled);
```

当 debug 打开时，在 `InitializeRoutingDB` 末尾打印类似摘要：

```text
[initializer] grid size = 100 x 120
[initializer] blocked cells = 37
[initializer] horizontal edges = 11900
[initializer] vertical edges = 11880
[initializer] nets = 54
[initializer] total pins = 163
```

如发现问题也打印 warning，例如：
- duplicate pins removed
- net has fewer than 2 unique pins
- inconsistent blocked sources
- duplicate net ids

请不要打印过多逐点日志，除非必要。

====================
十一、代码文件组织要求
====================

请输出完整可编译代码，至少包括：

1. include/common.h
   - 如有必要，增加 `Dir / EdgeState / RoutingDB`

2. include/tools.h
   - 放所有 routing helper inline 定义
   - 包括索引函数、边界判断、blocked/edge 访问、邻居与边状态访问 helper

3. include/initializer.h
   - 对外 API 声明

4. src/initializer.cc
   - 完整实现

如果你修改了 common.h，请保留原有 `Point / Net / Problem` 结构，不要破坏 parser 已有代码兼容性。

====================
十二、实现风格要求
====================

1. 使用 C++17
2. 不要引入第三方库
3. 尽量使用 `std::vector`
4. helper 函数命名清晰
5. 不要写成伪代码，要给出完整可编译实现
6. 必要处加简短注释，解释索引语义
7. 对异常报错信息写清楚具体坐标和 net 名称
8. 不要实现 router，只实现 initializer
9. 不要实现 main
10. 保证代码能被后续 router 直接调用
11. 注意 `RoutingDB` 只保存 `const Problem*`，因此要避免悬空指针误用

====================
十三、额外注意事项
====================

1. implicit grid graph 的含义是：
   - 不为每个 cell 显式保存邻接表
   - 邻接关系由网格位置 + 边表隐式决定

2. capacity 的方向定义：
   - horizontal edge capacity 来自 `problem.horizontal_capacity`
   - vertical edge capacity 来自 `problem.vertical_capacity`
   - capacity 不按 edge 存储

3. overflow 的定义：
   - `overflow = max(0, usage - capacity)`
   - overflow 不按 edge 存储
   - 只在需要时现场计算

4. net pin 不能在 blocked cell 上，这一点必须严格检查

5. parser 可能已经提供了 `problem.blocked`
   - 你要兼容这个字段
   - 不要假设只有 `blocks`

6. 本模块的本质是：
   - 在 immutable `Problem` 之上构建 routing runtime state
   - 而不是复制出一个新的 Problem 副本

7. 将模块接入 main.cc
8. 将模块加入 Makefile 构建

====================
十四、输出格式要求
====================

请直接写入这些代码文件，并保证 `include/tools.h` 与 initializer 模块语义一致。
