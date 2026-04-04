请为一个 C++17 项目实现 `parser` 模块。要求你写入3个文件：

- `parser.h`
- `parser.cc`
- `common.h`

以下所有 `.h` 文件都在 `include/` 下，所有 `.cc` 都在 `src/` 下。

## 目标

实现一个**只负责读取 sample 输入文件并解析为内存数据结构**的 parser 模块，不做任何 routing、排序、图构建、A*、RRR、cost table 初始化等逻辑。

parser 模块的职责仅限于：

1. 从输入文本文件中读取题目格式的数据
2. 填充 `Problem` 结构体
3. 初始化 `blocked` 位图
4. 做必要且合理的输入合法性检查
5. 对外暴露简洁 API

---

## 文件组织要求

### `common.h`
后续可能被多个模块共用的数据结构必须放在 `common.h` 中。此次需要放入 `common.h` 的数据结构如下，**请原样使用，不要随意改字段名**：

```cpp
struct Point {
    int r;
    int c;

    bool operator==(const Point& other) const {
        return r == other.r && c == other.c;
    }

    bool operator<(const Point& other) const { // 字典序，不能当距离比较
        if (r != other.r) return r < other.r;
        else return c < other.c;
    }
};

struct Net {
    std::string name;          // "net0"
    int id = -1;               // 0
    std::vector<Point> pins;   // 输入pin
};

struct Problem {
    int rows = 0;
    int cols = 0;

    int vertical_capacity = 0;
    int horizontal_capacity = 0;

    std::vector<Point> blocks;
    std::vector<Net> nets;

    // blocked[r * cols + c] == 0 表示 free，!= 0 表示 blocked
    std::vector<uint8_t> blocked;   // rows * cols, 0/0xff
};
```

如果你认为 parser 模块内部还需要一些**不会被别的模块复用**的小辅助函数或私有工具，请放在 `parser.cc` 内部，不要污染 `common.h`。

### `parser.h`
只放 parser 对外暴露的 API 声明，以及确有必要的少量声明。  
不要在 `parser.h` 里重复定义 `Point`、`Net`、`Problem`，而是 `#include "common.h"`。

### `parser.cc`
放 parser 的完整实现，以及私有 helper 函数。

---

## 输入格式

输入文件格式如下：

- `grid <rows> <cols>`
- `vertical capacity <value>`
- `horizontal capacity <value>`
- `num block <count>`
- 接着有 `<count>` 行：`block <r> <c>`
- `num net <count>`
- 对于每个 net：
  - 一行：`netX <pin_count>`
  - 接着 `<pin_count>` 行，每行两个整数：`<r> <c>`

例如：

请参考 [sample](../samples/sample0.txt)

注意：
- pin 行前面可能有空格
- 应该用 `operator>>` 风格的流式解析，天然跳过空白
- 不要依赖固定缩进
- net 名称就是输入中的字符串，比如 `"net0"`

---

## 解析后需要满足的行为

### 1. 正确填充 `Problem`
包括：
- `rows`
- `cols`
- `vertical_capacity`
- `horizontal_capacity`
- `blocks`
- `nets`
- `blocked`

### 2. 初始化 `blocked`
要求：
- 大小为 `rows * cols`
- 初始全 0
- 对每个 block 点 `(r, c)`，设置：
  - `blocked[r * cols + c] = 0xff`

### 3. 基本合法性检查
请做以下检查；如果失败，抛出带清晰错误信息的 `std::runtime_error`：

- 文件无法打开
- token 不符合预期格式
- `rows <= 0` 或 `cols <= 0`
- capacity 为负数
- `num block` / `num net` / `pin_count` 为负数
- block 越界
- pin 越界
- block 重复
- 同一个 net 内 pin 重复
- pin 落在 blocked cell 上

说明：
- “同一个 net 内 pin 重复”请检查并报错
- 不要求检查不同 net 之间 pin 是否重合
- 不要求做任何路由层面的可达性检查

---

## API 设计要求

请在 `parser.h` 中暴露如下 API（名字可保持一致）：

```cpp
Problem ParseInputFile(const std::string& filename);
Problem ParseInputStream(std::istream& is);
void SetParserDebug(bool enabled);
```

明确：
- `ParseInputFile` 负责打开文件
- `ParseInputStream` 负责真正解析逻辑

这样更方便测试。

---

## 实现细节要求

### 1. 风格
- 使用现代 C++17
- 代码清晰、稳健、易读
- 适当加注释，但不要太啰嗦
- 不要写成宏风格代码
- 不要引入不必要的复杂模板

### 2. helper 函数
在 `parser.cc` 中建议实现一些私有 helper，例如：
- `int IndexOf(int r, int c, int cols)`
- `void CheckInBounds(...)`
- `void ExpectToken(...)`
- `int ParseNetIdFromName(const std::string& name)`  
  例如把 `"net37"` 解析为 `37`

`Net.id` 的值要求：
- 从 net 名字符串中解析数字部分
- 如果名字不是 `"net<nonnegative integer>"` 这种格式，则报错

### 3. 重复检查建议
- 对 block 重复检查，可以用 `blocked` 位图辅助完成
- 对同一个 net 内 pin 重复检查，可以使用 `std::set<Point>`，因为 `Point` 已定义 `operator<`

### 4. include 要合理
请补齐必要头文件，例如：
- `<string>`
- `<vector>`
- `<cstdint>`
- `<stdexcept>`
- `<fstream>`
- `<istream>`
- `<set>`
- `<cctype>`

但不要乱 include 一堆没用的头。

---

## 输出内容要求

请直接输出完整的：

1. `parser.h`
2. `parser.cc`

要求：
- 每个文件都给出完整源码
- 可直接保存编译
- 头文件保护请使用 `#pragma once`
- `parser.h` 要 include `common.h`
- 不要生成 main 函数
- 不要生成示例调用代码
- 不要生成单元测试
- 不要改动 `common.h` 中给定的数据结构定义

---

## 额外要求

### 错误信息
请让错误信息尽量具体，例如：
- `"Failed to open input file: xxx"`
- `"Expected token 'grid', got '...'"`
- `"Block coordinate out of bounds: (r, c)"`
- `"Duplicate pin in net net17: (r, c)"`

### debug输出

需要实现可在外部（main函数中）关掉的 debug 输出，默认关闭，而main函数中打开，debug 输出逻辑不应该过多地污染业务代码。

在 `parser.h` 中暴露：

```cpp
void SetParserDebug(bool enabled);
```

debug 信息也走 `std::cout` 输出，方便我重定向到文件里查看。

debug要输出的内容：
    grid 的大小，parse完成后的所有 nets 及其对应的 pins，所有 blocks。

格式尽量规整且稳定，便于重定向后人工查看。请使用固定、清晰、可重复的格式；不要输出花哨前缀或无意义装饰。

### 稳健性
不要假设输入一定完全正确；parser 应该尽可能在格式错误时尽早失败并报清晰错误。

### 边界处理
即使 `num block = 0` 或 `num net = 0`，也应该正确解析。

---

## 最终目标

我要的是一个**干净、可直接集成到后续 router 工程中的 parser 模块**，而不是 demo 代码。请严格按模块化方式实现。

### 编码风格要求

使用基于 `if` 检查后直接 `throw std::runtime_error(...)` 的风格；不要为了控制流程而滥用 `try/catch`。除非确有必要，不要写 `catch` 块。

- 不要在头文件或源文件中使用 `using namespace std;`
- 保持 `blocks`、`nets`、`pins` 的输入顺序，不要自行排序
- 遇到非法输入时不要尝试自动修复或跳过，应直接报错