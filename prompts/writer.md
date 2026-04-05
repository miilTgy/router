请为这个 C++ global routing 项目实现 `writer` 模块，用于把当前已经得到的 routing 结果写成作业要求的输出文件格式，并能直接被现有可视化脚本读取。

请严格复用当前仓库已有的数据结构与模块语义，尤其是：

- `common.h` 中的 `Point / Net / Problem / RoutedTree / RoutingResult`
- `router.h` 中 `RunInitialRouting(...)` 返回的 `RoutingResult`
- `RoutingResult.routed_nets` 中保存的每个 `RoutedTree`
- `RoutedTree.net_name`
- `RoutedTree.branches`

不要重新 parse 输入，不要重新做 routing，不要改动 router 的核心逻辑。`writer` 只负责把已有结果序列化到文件。

---

# 一、目标

实现一个最小但完整可用的 `writer` 模块，负责：

1. 接收输入文件路径与 `RoutingResult`
2. 根据输入文件名自动生成输出文件名
3. 将每个 net 的 routed tree 展开为 edge 列表
4. 按指定格式写入输出文件
5. 让输出文件能够直接被现有可视化脚本读取

输出文件命名规则必须为：

- 若输入文件为 `sample0.txt`
- 则输出文件为 `sample0_solution.txt`

也就是说：
- 统一输出到仓库根目录下的 `results/` 目录
- 取去掉扩展名后的 stem
- 在 stem 后追加 `_solution`
- 扩展名统一仍为 `.txt`

例如：
- `samples/sample0.txt` -> `results/sample0_solution.txt`
- `./sample1.txt` -> `results/sample1_solution.txt`

注意：你上条消息中英文拼写有一处 `soluition`，这里请按更合理、规范的文件名 `solution` 实现：
- `sample0_solution.txt`

---

# 二、输出格式

输出格式严格遵循如下规范：

```text
net0
(<row>, <col>)-(<row>, <col>)
(<row>, <col>)-(<row>, <col>)
...
!
net1
(<row>, <col>)-(<row>, <col>)
...
!
```

说明：

- 每个 net 先写一行 net 名称，即 `RoutedTree.net_name`
- 后续每一行写一个 edge
- 一个 edge 由两个 Manhattan 相邻点组成
- 格式严格为：
  `(<r0>, <c0>)-(<r1>, <c1>)`
- 不要多余空格，逗号后保留一个空格，和示例一致
- 每个 net 的所有 edge 写完后，单独写一行 `!`
- `!` 用来分隔 nets
- 最后一个 net 后面也照样写 `!`

参考格式如下：

```text
net0
(1, 2)-(2, 2)
(2, 2)-(3, 2)
(2, 2)-(2, 3)
!
net1
(0, 0)-(1, 0)
!
```

---

# 三、接入现有数据结构的要求

当前仓库中，router 已经返回：

```cpp
struct RoutingResult {
    std::vector<RoutedTree> routed_nets;
    int total_wirelength = 0;
    int total_overflow = 0;
};
```

而每个 `RoutedTree` 内有：

```cpp
struct RoutedTree {
    int net_id = -1;
    std::string net_name;
    std::set<Point> tree_points;
    std::vector<std::vector<Point>> branches;
};
```

writer 必须直接使用这些结构，不要新造一套平行结果结构。

`branches` 的语义是：
- 每个 branch 是一条 path
- path 中相邻两个点构成一个 routed edge
- path 可能长度为 1；这代表零边路径，此时不输出任何 edge 行
- 不同 branch 中的 edge 不需要做去重
- 如果同一条 edge 在同一个 net 的多个 branch 中重复出现，则按出现次数原样输出
- edge 输出顺序必须保持 `branches` 的原始顺序，以及每条 branch 内点的顺序

这点很重要：  
writer 不负责“美化”或“去重”输出，只负责把当前 routing 结果忠实展开成 edge 列表。

---

# 四、建议新增文件

请新增：

```text
include/writer.h
src/writer.cc
```

必要时允许小幅修改：

```text
src/main.cc
Makefile
```

不要把 writer 逻辑直接塞进 `main.cc`。

---

# 五、writer API 设计

请在 `include/writer.h` 中声明至少以下接口：

```cpp
#pragma once

#include <string>
#include <vector>

#include "common.h"

std::string MakeSolutionFilePath(const std::string& input_file_path);

std::vector<std::pair<Point, Point>> ExpandTreeToEdges(const RoutedTree& tree);

void WriteRoutingSolution(
    const std::string& input_file_path,
    const RoutingResult& result);

void WriteRoutingSolutionToPath(
    const std::string& output_file_path,
    const RoutingResult& result);
```

各接口语义如下：

## 1. `MakeSolutionFilePath(...)`
输入：
- 原始输入文件路径，例如 `samples/sample0.txt`

输出：
- 对应输出文件路径，例如 `results/sample0_solution.txt`

实现要求：
- 使用标准 C++ 路径工具（推荐 `std::filesystem::path`）
- 固定输出到 `results/` 目录
- 取 stem 后追加 `_solution`
- 扩展名固定为 `.txt`

## 2. `ExpandTreeToEdges(...)`
输入：
- 一个 `RoutedTree`

输出：
- 按 writer 输出顺序展开后的 edge 列表，每个元素是 `(Point, Point)`

规则：
- 遍历 `tree.branches`
- 对每条 branch，遍历相邻点对
- 每一对相邻点 `(branch[i-1], branch[i])` 形成一个 edge
- 若 branch 长度 < 2，则跳过
- 若发现任意相邻点不是 Manhattan 相邻，抛出异常
- 不做去重
- 保持 branch 原顺序和 branch 内点原顺序

## 3. `WriteRoutingSolution(...)`
输入：
- 输入文件路径
- `RoutingResult`

行为：
- 内部调用 `MakeSolutionFilePath(...)`
- 再调用 `WriteRoutingSolutionToPath(...)`

## 4. `WriteRoutingSolutionToPath(...)`
输入：
- 明确指定的输出文件路径
- `RoutingResult`

行为：
- 以文本方式写出完整 solution 文件
- 写文件失败时抛异常

---

# 六、具体输出逻辑

对于 `result.routed_nets` 中的每个 `RoutedTree tree`：

1. 先输出：
```text
<tree.net_name>
```

2. 调用 `ExpandTreeToEdges(tree)` 得到 edge 列表

3. 对每条 edge `(u, v)` 输出一行：
```text
(<u.r>, <u.c>)-(<v.r>, <v.c>)
```

4. 当前 net 输出完后，再输出一行：
```text
!
```

注意：
- 不要输出 net id
- 不要输出 wirelength / overflow 摘要到 solution 文件
- 不要输出额外注释
- 不要插入空行
- 只输出作业要求的纯文本内容

---

# 七、调试与异常处理

可选但推荐提供 debug 开关：

```cpp
void SetWriterDebug(bool enabled);
```

若启用，可在终端打印类似：

```text
[writer] output path=results/sample0_solution.txt
[writer] write net name=net0 edge_count=5
[writer] write net name=net1 edge_count=2
[writer] solution written nets=20
```

异常处理要求：

- 输出文件无法打开：抛 `std::runtime_error`
- `results/` 不存在时，writer 应自动创建；若创建失败，抛 `std::runtime_error`
- branch 内出现非 Manhattan 相邻点：抛 `std::runtime_error`
- `result.routed_nets` 为空：允许写出空文件，或写出 0 个 net 的空结果，不报错
- net 名为空：必须抛 `std::runtime_error`
- 不要 silent fail

---

# 八、main 接入方式

当前 `main.cc` 已经大致是：

```cpp
const Problem problem = ParseInputFile(input_file);
RoutingDB db = InitializeRoutingDB(problem);
const RoutingResult result = RunInitialRouting(db);
```

请在此基础上接入 writer：

```cpp
WriteRoutingSolution(input_file, result);
```

然后在终端打印类似摘要：

```cpp
const std::string output_file = MakeSolutionFilePath(input_file);
std::cout << "main.write_ok output=" << output_file << "\n";
```

要求：
- 不要破坏现有 parse / initialize / route 的流程
- 不要让 writer 重新依赖 parser 或 router 内部状态
- writer 只依赖 `RoutingResult`

---

# 九、Makefile 接入

请将 `src/writer.cc` 加入 `SOURCES`，例如：

```make
SOURCES := src/main.cc src/parser.cc src/initializer.cc src/router.cc src/writer.cc
```

---

# 十、实现细节建议

建议在 `src/writer.cc` 中补充内部 helper：

```cpp
std::string FormatPoint(const Point& p);
bool AreManhattanAdjacent(const Point& a, const Point& b);
```

其中：

- `FormatPoint({1,2})` -> `"(1, 2)"`
- `AreManhattanAdjacent(a, b)` 判断 `abs(dr) + abs(dc) == 1`

输出 edge 时直接拼成：

```cpp
FormatPoint(u) + "-" + FormatPoint(v)
```

---

# 十一、边界与一致性要求

请严格遵守以下一致性：

1. writer 输出的 net 顺序必须与输入中的 net 顺序一致，也就是 `net0, net1, net2, ...` 这样的输入顺次序  
2. 如果 `result.routed_nets` 当前顺序与输入顺序不一致，writer 应在写出前按输入顺序重排  
3. 同一个 net 内 edge 的顺序必须与 `branches` 展开顺序一致  
4. 不做 edge 去重  
5. edge 方向与 branch 内相邻点顺序保持一致，不重排  
6. writer 不负责验证该 routed tree 是否最优，只负责忠实输出  
7. writer 不引入新数据结构来重新解释树，只做轻量展开

---

# 十二、最终目标

实现一个能直接把当前 initial routing 结果落盘的 writer：

- 输入 `sample0.txt`
- 运行 parser / initializer / router
- 自动生成 `results/sample0_solution.txt`
- 文件内容严格符合作业要求
- 可以直接交给现有可视化脚本读取

只实现 writer 模块及其接入，不要实现可视化脚本本身，不要实现 RRR，不要改 router 核心算法。
