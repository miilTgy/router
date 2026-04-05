#!/usr/bin/env python3

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import os
import re
import sys
import tempfile

mpl_config_dir = Path(tempfile.gettempdir()) / "see_py_matplotlib"
mpl_config_dir.mkdir(parents=True, exist_ok=True)
os.environ.setdefault("MPLCONFIGDIR", str(mpl_config_dir))

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle


EDGE_LINE_RE = re.compile(
    r"^\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\)\s*-\s*\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\)\s*$"
)


@dataclass(frozen=True, order=True)
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


def usage() -> str:
    return "Usage: python3 see.py <input_sample_path> <solution_path> [output_png_path]"


def read_token(tokens: list[str], index: int, expected_description: str) -> tuple[str, int]:
    if index >= len(tokens):
        raise ValueError(f"Expected token '{expected_description}', got end of input")
    return tokens[index], index + 1


def expect_token(tokens: list[str], index: int, expected: str) -> int:
    token, next_index = read_token(tokens, index, expected)
    if token != expected:
        raise ValueError(f"Expected token '{expected}', got '{token}'")
    return next_index


def read_int(tokens: list[str], index: int, label: str) -> tuple[int, int]:
    token, next_index = read_token(tokens, index, label)
    try:
        return int(token), next_index
    except ValueError as exc:
        raise ValueError(f"Expected integer for {label}, got '{token}'") from exc


def ensure_positive(name: str, value: int) -> None:
    if value <= 0:
        raise ValueError(f"{name} must be positive, got {value}")


def ensure_non_negative(name: str, value: int) -> None:
    if value < 0:
        raise ValueError(f"{name} must be non-negative, got {value}")


def parse_net_id(name: str) -> int:
    if len(name) < 4 or not name.startswith("net") or not name[3:].isdigit():
        raise ValueError(f"Invalid net name format: '{name}'")
    return int(name[3:])


def format_point(point: Point) -> str:
    return f"({point.r}, {point.c})"


def check_in_bounds(point: Point, rows: int, cols: int, label: str) -> None:
    if point.r < 0 or point.r >= rows or point.c < 0 or point.c >= cols:
        raise ValueError(f"{label} out of bounds: {format_point(point)}")


def parse_sample_file(path: str) -> Problem:
    tokens = Path(path).read_text(encoding="utf-8").split()
    index = 0

    index = expect_token(tokens, index, "grid")
    rows, index = read_int(tokens, index, "grid rows")
    cols, index = read_int(tokens, index, "grid cols")
    ensure_positive("rows", rows)
    ensure_positive("cols", cols)

    blocked = [0] * (rows * cols)
    blocks: list[Point] = []
    nets: list[Net] = []

    index = expect_token(tokens, index, "vertical")
    index = expect_token(tokens, index, "capacity")
    vertical_capacity, index = read_int(tokens, index, "vertical capacity")
    ensure_non_negative("vertical capacity", vertical_capacity)

    index = expect_token(tokens, index, "horizontal")
    index = expect_token(tokens, index, "capacity")
    horizontal_capacity, index = read_int(tokens, index, "horizontal capacity")
    ensure_non_negative("horizontal capacity", horizontal_capacity)

    index = expect_token(tokens, index, "num")
    index = expect_token(tokens, index, "block")
    block_count, index = read_int(tokens, index, "num block")
    ensure_non_negative("num block", block_count)

    for _ in range(block_count):
        index = expect_token(tokens, index, "block")
        r, index = read_int(tokens, index, "block row")
        c, index = read_int(tokens, index, "block col")
        block = Point(r, c)
        check_in_bounds(block, rows, cols, "Block coordinate")
        block_index = block.r * cols + block.c
        if blocked[block_index]:
            raise ValueError(f"Duplicate block coordinate: {format_point(block)}")
        blocked[block_index] = 1
        blocks.append(block)

    index = expect_token(tokens, index, "num")
    index = expect_token(tokens, index, "net")
    net_count, index = read_int(tokens, index, "num net")
    ensure_non_negative("num net", net_count)

    for _ in range(net_count):
        name, index = read_token(tokens, index, "net name")
        parse_net_id(name)
        pin_count, index = read_int(tokens, index, "pin count")
        ensure_non_negative("pin_count", pin_count)
        pins: list[Point] = []
        seen_pins: set[Point] = set()
        for _ in range(pin_count):
            r, index = read_int(tokens, index, "pin row")
            c, index = read_int(tokens, index, "pin col")
            pin = Point(r, c)
            check_in_bounds(pin, rows, cols, f"Pin coordinate in net {name}")
            if pin in seen_pins:
                raise ValueError(f"Duplicate pin in net {name}: {format_point(pin)}")
            if blocked[pin.r * cols + pin.c]:
                raise ValueError(f"Pin falls on blocked cell in net {name}: {format_point(pin)}")
            seen_pins.add(pin)
            pins.append(pin)
        nets.append(Net(name=name, pins=pins))

    if index != len(tokens):
        raise ValueError(f"Unexpected trailing token '{tokens[index]}' in sample file")

    return Problem(rows=rows, cols=cols, blocks=blocks, nets=nets)


def parse_solution_file(path: str) -> dict[str, list[tuple[Point, Point]]]:
    solution: dict[str, list[tuple[Point, Point]]] = {}
    current_net_name: str | None = None
    current_edges: list[tuple[Point, Point]] | None = None

    for line_number, raw_line in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line:
            continue

        if line == "!":
            if current_net_name is None or current_edges is None:
                raise ValueError(f"Line {line_number}: '!' appears before any net name")
            solution[current_net_name] = current_edges
            current_net_name = None
            current_edges = None
            continue

        edge_match = EDGE_LINE_RE.match(line)
        if edge_match:
            if current_net_name is None or current_edges is None:
                raise ValueError(f"Line {line_number}: edge appears before any net name")
            p0 = Point(int(edge_match.group(1)), int(edge_match.group(2)))
            p1 = Point(int(edge_match.group(3)), int(edge_match.group(4)))
            current_edges.append((p0, p1))
            continue

        if current_net_name is not None:
            raise ValueError(
                f"Line {line_number}: missing '!' before new net '{line}'"
            )

        current_net_name = line
        current_edges = []

    if current_net_name is not None:
        raise ValueError(f"Solution file ended before closing net '{current_net_name}' with '!'")

    return solution


def validate_solution(problem: Problem, solution: dict[str, list[tuple[Point, Point]]]) -> None:
    sample_net_names = {net.name for net in problem.nets}
    for name, edges in solution.items():
        if name not in sample_net_names:
            print(f"Warning: solution contains unknown net '{name}'", file=sys.stderr)
        for edge_index, (p0, p1) in enumerate(edges):
            check_in_bounds(p0, problem.rows, problem.cols, f"Edge start for net {name}")
            check_in_bounds(p1, problem.rows, problem.cols, f"Edge end for net {name}")
            manhattan = abs(p0.r - p1.r) + abs(p0.c - p1.c)
            if manhattan != 1:
                raise ValueError(
                    f"Edge {edge_index} in net {name} is not Manhattan-adjacent: "
                    f"{format_point(p0)}-{format_point(p1)}"
                )


def choose_tick_step(n: int) -> int:
    if n <= 20:
        return 1
    if n <= 50:
        return 5 if n > 30 else 2
    if n <= 100:
        return 10 if n > 75 else 5

    for step in (10, 20, 50, 100, 200, 500):
        if n / step <= 25:
            return step
    return max(1, (n + 24) // 25)


def point_center(point: Point) -> tuple[float, float]:
    return point.c + 0.5, point.r + 0.5


def default_output_png_path(solution_path: str) -> str:
    return str(Path(solution_path).with_suffix(".png"))


def figure_size(rows: int, cols: int) -> tuple[float, float]:
    longest = max(rows, cols)
    base = 8.0
    scale = min(14.0, max(base, longest * 0.18))
    width = min(14.0, max(8.0, scale * (cols / longest if longest else 1.0)))
    height = min(14.0, max(8.0, scale * (rows / longest if longest else 1.0)))
    return width, height


def configure_axis_ticks(ax: plt.Axes, rows: int, cols: int) -> None:
    x_step = choose_tick_step(cols)
    y_step = choose_tick_step(rows)

    x_major_positions = [c + 0.5 for c in range(0, cols, x_step)]
    y_major_positions = [r + 0.5 for r in range(0, rows, y_step)]
    x_major_labels = [str(c) for c in range(0, cols, x_step)]
    y_major_labels = [str(r) for r in range(0, rows, y_step)]

    ax.set_xticks(x_major_positions)
    ax.set_yticks(y_major_positions)
    ax.set_xticklabels(x_major_labels, fontsize=8)
    ax.set_yticklabels(y_major_labels, fontsize=8)

    ax.set_xticks(list(range(cols + 1)), minor=True)
    ax.set_yticks(list(range(rows + 1)), minor=True)
    ax.grid(which="minor", color="#d8d8d8", linewidth=0.45)
    ax.tick_params(which="minor", bottom=False, left=False)
    ax.tick_params(which="major", length=3, width=0.8)


def draw_problem_and_solution(
    problem: Problem,
    solution: dict[str, list[tuple[Point, Point]]],
    sample_name: str,
    output_png_path: str,
) -> None:
    fig, ax = plt.subplots(figsize=figure_size(problem.rows, problem.cols), dpi=180)

    for block in problem.blocks:
        ax.add_patch(
            Rectangle(
                (block.c, block.r),
                1.0,
                1.0,
                facecolor="#8d8d8d",
                edgecolor="none",
                zorder=1,
            )
        )

    for edges in solution.values():
        for p0, p1 in edges:
            x0, y0 = point_center(p0)
            x1, y1 = point_center(p1)
            ax.plot(
                [x0, x1],
                [y0, y1],
                color="#1f1f1f",
                linewidth=1.0,
                alpha=0.85,
                solid_capstyle="round",
                zorder=2,
            )

    pin_x: list[float] = []
    pin_y: list[float] = []
    for net in problem.nets:
        for pin in net.pins:
            x, y = point_center(pin)
            pin_x.append(x)
            pin_y.append(y)

    if pin_x:
        ax.scatter(pin_x, pin_y, s=14, c="#101010", marker="o", zorder=3)

    ax.set_xlim(0, problem.cols)
    ax.set_ylim(0, problem.rows)
    ax.set_aspect("equal")
    configure_axis_ticks(ax, problem.rows, problem.cols)

    ax.set_title(f"Routing Visualization: {sample_name}", fontsize=12, pad=14)
    fig.text(
        0.5,
        0.965,
        f"rows={problem.rows} cols={problem.cols} nets={len(problem.nets)} blocks={len(problem.blocks)}",
        ha="center",
        va="top",
        fontsize=9,
    )

    for spine in ax.spines.values():
        spine.set_linewidth(0.8)
        spine.set_color("#707070")

    fig.tight_layout(rect=(0, 0, 1, 0.94))
    output_path = Path(output_png_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path, bbox_inches="tight")
    plt.close(fig)


def main() -> int:
    if len(sys.argv) not in (3, 4):
        print(usage(), file=sys.stderr)
        return 1

    input_sample_path = sys.argv[1]
    solution_path = sys.argv[2]
    output_png_path = sys.argv[3] if len(sys.argv) == 4 else default_output_png_path(solution_path)

    try:
        problem = parse_sample_file(input_sample_path)
        solution = parse_solution_file(solution_path)
        validate_solution(problem, solution)
        sample_name = Path(input_sample_path).stem
        draw_problem_and_solution(problem, solution, sample_name, output_png_path)
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    print(f"Saved visualization to {output_png_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
