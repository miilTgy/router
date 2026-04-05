请为这个 C++ global routing 项目实现 `tools` 模块。该模块只负责提供基于 `Problem` / `RoutingDB` 的 header-only 访问辅助，不负责 parser、initializer、router、rip-up-reroute 或任何会修改 routing state 的流程控制逻辑。

以下所有 `.h` 文件都在 `include/` 下，所有 `.cc` 都在 `src/` 下。

## 目标

实现一个公共 `include/tools.h`，用于集中放置 routing 相关的 inline helper，使：

1. `common.h` 只保留共享数据结构
2. `initializer.h` 只保留 initializer 对外 API
3. 后续 router / orderer / rip-up-reroute 可以直接复用这些 helper

`tools.h` 必须是 header-only，不要创建 `tools.cc`。

## 文件组织要求

### `common.h`
只放共享数据结构，例如：

- `Point`
- `Net`
- `Problem`
- `Dir`
- `EdgeState`
- `RoutingDB`

不要把索引函数、边界判断函数、edge access helper 放进 `common.h`。

### `tools.h`
只放 inline helper 定义，以及 helper 自己需要的最小 include。

建议包含：

```cpp
#include <algorithm>
#include <stdexcept>

#include "common.h"
```

不要在 `tools.h` 中重新定义 `Point`、`Problem`、`RoutingDB`。

### `initializer.h`
如果希望保留现有调用体验，可以让 `initializer.h` 包含 `tools.h`，但 `initializer.h` 自身不应再定义这些 helper。

## helper 职责

`tools.h` 只负责无状态、轻量、可复用的数据访问辅助，包括：

### 1. 索引规则

```cpp
inline int CellIndex(int r, int c, int cols);
inline int HorizontalEdgeIndex(int r, int c, int cols);
inline int VerticalEdgeIndex(int r, int c, int cols);
```

约定：

- `CellIndex(r, c, cols) = r * cols + c`
- `HorizontalEdgeIndex(r, c, cols)` 对应边 `(r, c) <-> (r, c + 1)`
- `VerticalEdgeIndex(r, c, cols)` 对应边 `(r, c) <-> (r + 1, c)`

### 2. 边界与方向判断

```cpp
inline bool InBounds(int r, int c, int rows, int cols);
inline bool HasRightEdge(int r, int c, int rows, int cols);
inline bool HasLeftEdge(int r, int c, int rows, int cols);
inline bool HasUpEdge(int r, int c, int rows, int cols);
inline bool HasDownEdge(int r, int c, int rows, int cols);
```

坐标约定：

- `r` 向下增大
- `c` 向右增大

### 3. `RoutingDB` 只读访问

```cpp
inline int Rows(const RoutingDB& db);
inline int Cols(const RoutingDB& db);
inline int HorizontalCapacity(const RoutingDB& db);
inline int VerticalCapacity(const RoutingDB& db);
```

如果 `db.problem == nullptr`，应抛出 `std::logic_error`。

### 4. blocked / edge 可用性辅助

```cpp
inline bool IsBlockedCell(const Problem& problem, int r, int c);
inline bool IsBlockedCell(const RoutingDB& db, int r, int c);
inline bool EdgeEndpointsAvailable(const RoutingDB& db, int r0, int c0, int r1, int c1);
inline bool IsHorizontalEdgeAvailable(const RoutingDB& db, int r, int c);
inline bool IsVerticalEdgeAvailable(const RoutingDB& db, int r, int c);
inline bool HasEdge(const RoutingDB& db, int r, int c, Dir dir);
inline bool IsEdgeAvailable(const RoutingDB& db, int r, int c, Dir dir);
```

语义要求：

- blocked cell 不可作为 routing node
- 若边任一端点 blocked，则该边不可用
- helper 不应复制 `problem.blocked`

### 5. 邻居与边状态访问

```cpp
inline Point NeighborPoint(int r, int c, Dir dir);
inline EdgeState& MutableEdgeState(RoutingDB& db, int r, int c, Dir dir);
inline const EdgeState& GetEdgeState(const RoutingDB& db, int r, int c, Dir dir);
inline int EdgeCapacity(const RoutingDB& db, Dir dir);
inline int EdgeOverflow(const RoutingDB& db, int r, int c, Dir dir);
```

语义要求：

- `EdgeCapacity` 按方向返回全局 capacity，而不是 per-edge capacity
- `EdgeOverflow = max(0, usage - capacity)`
- 不引入 overflow 表

## 实现风格要求

- 使用 C++17
- 全部 helper 使用 `inline`
- 不引入第三方库
- 不要使用 `using namespace std;`
- 逻辑简单直接，适合被高频调用
- 必要时报 `std::logic_error`，错误信息简洁清晰

## 最终目标

我要的是一个清晰的公共工具层：

- `common.h` 负责结构
- `tools.h` 负责访问辅助
- `initializer.h` 负责 initializer API

三者职责边界必须稳定且清晰，方便后续 router 模块继续扩展。
