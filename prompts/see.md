请为这个 C++ global routing 项目实现一个独立的 Python 可视化脚本 `see.py`，用于把输入样例文件和 router 输出的 solution 文件一起读入，并在一个 grid 图中可视化显示：

- 所有 routed nets
- 所有 blocked cells
- 所有 pins

请严格按照以下要求实现，不要擅自改成别的风格。

---

# 一、目标

当前项目已经有：

1. 输入样例文件，例如 `sample0.txt`
2. router/writer 输出的 solution 文件，例如 `sample0_solution.txt`

现在需要实现一个 **独立的** Python 脚本：

```text
see.py
```

它读取：

- 原始输入文件（用于获得 rows / cols / blocks / nets / pins）
- solution 文件（用于获得 routed edges）

并生成一张可视化图，把整个 routing 结果画在 grid 上。

这个脚本是辅助调试与结果检查工具，不是评分程序，不要改 writer 格式，不要改 router 逻辑。

---

# 二、显示要求（这是最重要的）

请严格满足以下显示风格：

1. **在 grid 中显示所有 nets、所有 blocked cells、所有 pins**
2. **不要显示任何图中 label 文字**
   - 不要给 net 写名字
   - 不要给 pin 写文字
   - 不要给 edge 写编号
   - 不要给 block 写文字
3. **不要区分颜色**
   - 所有 routed nets 的线条统一使用同一种颜色
   - 所有 pins 统一使用同一种颜色
   - 所有 blocked cells 统一使用同一种颜色
   - 不要为不同 net 分配不同颜色
4. **不要图标**
   - 不要用星形、三角形、十字、emoji、箭头图标
   - 使用最普通的 matplotlib 基本图元即可
5. **图标题和简单注释要显示**
   - 标题要显示样例名
   - 可以在图外或标题附近用简短文字显示 rows / cols / net_count / block_count
   - 这些属于允许显示的信息
6. **坐标轴数字要显示，且间距合理**
   - 不要求每个格点都显示坐标数字
   - 当设计规模较大时，坐标刻度要自动抽稀，否则会挤爆
   - 目标是“能看清大致坐标范围”，不是每个格都打数字
7. **不要求交互式 GUI**
   - 只需要能显示和/或保存静态图片
8. **优先考虑大规模设计的可读性**
   - 不要让图面充满文字
   - 重点是看整体 topology / blocks / pins / routed edges

以上约束必须严格遵守。用户明确要求：  
**不要 label，不要区分颜色，不要图标，只保留标题注释和合理间距的坐标。**

---

# 三、脚本输入方式

请让 `see.py` 支持如下命令行调用：

```bash
python3 see.py <input_sample_path> <solution_path>
```

例如：

```bash
python3 see.py samples/sample0.txt samples/sample0_solution.txt
```

并且支持可选第三个参数，指定输出图片路径：

```bash
python3 see.py <input_sample_path> <solution_path> [output_png_path]
```

例如：

```bash
python3 see.py samples/sample0.txt samples/sample0_solution.txt samples/sample0_vis.png
```

行为约定：

- 若提供 `output_png_path`，则保存图片到该路径
- 若未提供，则默认保存为与 solution 同目录、同 stem 的 png，例如：
  - `samples/sample0_solution.txt` -> `samples/sample0_solution.png`
- 保存完成后，也可以调用 `plt.show()` 显示；若你担心无显示环境，可只保存不 show，但要保证脚本能在无 GUI 环境下运行
- 推荐使用 matplotlib 的非交互后端安全保存

---

# 四、需要解析的两类文件

## 1. 输入样例文件

原始 sample 文件中至少包含这些信息：

- grid 大小：rows, cols
- blocked cells
- nets
- 每个 net 的 pins

请复用当前项目 parser 的语义来写 Python 版本的轻量 parser，但不要依赖 C++ 代码。  
也就是说，`see.py` 自己解析 sample 文件。

你不需要完美复刻 C++ parser 的调试输出，但解析出的信息必须足以支持可视化。

建议在 Python 中建立这些轻量结构：

```python
from dataclasses import dataclass

@dataclass
class Point:
    r: int
    c: int

@dataclass
class Net:
    name: str
    pins: list[Point]

@dataclass
class Problem:
    rows: int
    cols: int
    blocks: list[Point]
    nets: list[Net]
```

注意：
- 这里是可视化脚本内部自用的轻量结构
- 不需要复制 C++ 全部字段
- 只保留可视化需要的信息即可

## 2. solution 文件

solution 文件格式严格是：

```text
net0
(1, 2)-(2, 2)
(2, 2)-(3, 2)
!
net1
(0, 0)-(1, 0)
!
```

规则：

- 一段以 net 名开头
- 后面若干行 edge
- `!` 分隔不同 nets

你必须实现 solution parser，解析出：

```python
dict[str, list[tuple[Point, Point]]]
```

例如：

```python
{
    "net0": [(Point(1,2), Point(2,2)), (Point(2,2), Point(3,2))],
    "net1": [(Point(0,0), Point(1,0))],
}
```

要求：

- 保持 solution 中 edge 顺序
- 不做去重
- 若遇到非法 edge 行，报错
- 若 `!` 前没有 net 名，报错
- 若 net 没有任何 edge，允许为空列表

---

# 五、可视化的具体绘制规则

## 1. 坐标系统

请用“行列坐标”和 grid 对齐，推荐：

- x 轴对应 `column`
- y 轴对应 `row`

为了让图像直觉上像矩阵/版图，推荐让 `(0,0)` 在左上角附近，即：

- 画完后调用 `ax.invert_yaxis()`

这样 row 越大越往下，符合 grid 直觉。

## 2. grid

要把整个 `rows x cols` 的格子画出来。

推荐方式：

- 画浅灰色网格线
- 网格密度覆盖所有 cell 边界
- 线条要很细，不要喧宾夺主

## 3. blocked cells

blocked cells 要画出来。

要求：

- 用统一颜色填充 blocked cell
- 不要写文字
- 不要特殊图标
- 推荐画实心矩形 patch
- blocked cell `(r, c)` 对应 grid 中一个 cell 方块

## 4. pins

pins 必须显示。

要求：

- 用统一样式显示所有 pins
- 不区分不同 net
- 不显示名字
- 不使用花哨 marker
- 推荐用较小的实心圆点即可

注意：
- pin 是 point，不是 cell
- 请把 pin 画在格点/单元中心的定义说清楚，并保持一致
- 推荐：将 point `(r, c)` 画在 cell center，即 `(c + 0.5, r + 0.5)`，并让 edge 也连接这些 center
- 只要整张图语义自洽即可

## 5. routed edges

所有 routed edges 都必须显示。

要求：

- 所有 nets 使用同一种颜色
- 不区分 net
- 不显示名字
- 不画箭头
- 不做特殊样式
- 推荐用统一宽度的线段

每条 edge `(p0, p1)` 直接画成连接两个点中心的线段。

如果同一条 edge 被多个 nets 重复使用：
- 原样重复画即可
- 不需要合并
- 也不需要透明度编码，但可以适度设置 alpha 以免完全遮住

## 6. 图标题与注释

标题必须保留。

建议标题内容类似：

```text
Routing Visualization: sample0
```

并允许添加一个简短副标题或 fig text，例如：

```text
rows=20 cols=20 nets=35 blocks=12
```

这是允许显示的“标题注释”，但不要把这些注释放到图中每个对象旁边。

---

# 六、坐标刻度的“合理间距”要求

由于设计规模可能很大，不能把每个整数坐标都写出来。

请实现一个自动刻度抽稀策略：

- 若 `max(rows, cols) <= 20`：可每格显示一个刻度
- 若 `20 < max(rows, cols) <= 50`：刻度间隔设为 2 或 5
- 若 `50 < max(rows, cols) <= 100`：刻度间隔设为 5 或 10
- 若更大：自动取更稀疏的间隔，如 10 / 20 / 50
- 目标是最多几十个 tick，而不是几百个

请封装函数例如：

```python
def choose_tick_step(n: int) -> int:
    ...
```

要求：
- 让坐标轴数字“能看”，不要重叠成一团
- 不需要完全精确控制，只要明显合理即可

---

# 七、推荐实现结构

请实现成一个清晰、可运行的单文件脚本 `see.py`，内部结构建议如下：

```python
from dataclasses import dataclass
from pathlib import Path
import re
import sys

import matplotlib
matplotlib.use("Agg")   # 若你认为需要无 GUI 兼容
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle
```

建议函数划分：

```python
@dataclass
class Point:
    r: int
    c: int

@dataclass
class Net:
    name: str
    pins: list[Point]

@dataclass
class Problem:
    rows: int
    cols: int
    blocks: list[Point]
    nets: list[Net]

def parse_sample_file(path: str) -> Problem:
    ...

def parse_solution_file(path: str) -> dict[str, list[tuple[Point, Point]]]:
    ...

def choose_tick_step(n: int) -> int:
    ...

def point_center(p: Point) -> tuple[float, float]:
    ...

def default_output_png_path(solution_path: str) -> str:
    ...

def draw_problem_and_solution(
    problem: Problem,
    solution: dict[str, list[tuple[Point, Point]]],
    sample_name: str,
    output_png_path: str,
) -> None:
    ...

def main() -> int:
    ...
```

---

# 八、绘图细节建议

请尽量实现以下视觉风格：

1. 画布大小随设计规模自适应，但要设上限，避免超大图爆掉  
   例如：
   - 小图可用 `(8, 8)`
   - 大图可增大到 `(12, 12)` 或 `(14, 14)`
   - 不必无限变大

2. 保持坐标比例一致  
   ```python
   ax.set_aspect("equal")
   ```

3. grid 线条要很细、很淡

4. blocked cells 的填充要明显，但不要太抢眼

5. routed edges 线宽中等偏细即可

6. pins 比 edge 稍醒目一点，但不要太大

7. 整张图不要出现 legend  
   因为用户明确说了不要图中 label 文字；legend 也属于多余标签，故不要加

---

# 九、校验与防御式处理

请加入基本检查：

1. 若 solution 中出现 sample 文件中不存在的 net 名，允许画，但建议在终端 warning
2. 若 sample 中某 net 没出现在 solution 里，也允许，只是该 net 只显示 pins，不显示 routed edge
3. 若 edge 端点越界，报错
4. 若 blocked cell 越界，报错
5. 若 pin 越界，报错
6. 若 solution 文件格式错误，抛出清晰异常
7. 命令行参数不足时，打印用法：

```bash
Usage: python3 see.py <input_sample_path> <solution_path> [output_png_path]
```

---

# 十、不要做的事

请不要加入以下内容：

- 不要使用不同 net 不同颜色
- 不要添加 legend
- 不要给每条 net 写 label
- 不要给每个 pin 写名字
- 不要在图中显示巨量文本
- 不要做交互式 hover
- 不要做动画
- 不要依赖 notebook
- 不要依赖 pandas
- 不要引入 seaborn
- 不要修改 writer 输出格式
- 不要修改 sample 文件格式
- 不要假设小规模数据才可运行

---

# 十一、最终目标

实现一个 `see.py`，满足：

```bash
python3 see.py samples/sample0.txt samples/sample0_solution.txt
```

即可生成一张可视化图片，图中：

- 能看到整个 grid
- 能看到所有 blocked cells
- 能看到所有 pins
- 能看到所有 routed edges
- 没有对象标签文字
- 没有多颜色区分
- 没有图标
- 只有标题和简短注释
- 坐标刻度自动抽稀，适合大规模设计检查

请直接输出完整、可运行的 `see.py` 源代码，不要只给伪代码。