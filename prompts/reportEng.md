# Global Routing Project Report

## 1. Project Overview

This project implements a global router based on a two-dimensional grid model. The overall workflow is:

1. Parse the input benchmark to obtain information such as the grid, capacity, blocks, and nets;
2. Initialize the routing database and build the horizontal/vertical edge states;
3. Perform initial routing;
4. Perform rip-up and reroute (RRR) based on the initial routing result;
5. Write the final routing result to the output file.

The main program flow is implemented in `main.cc`: it first calls `ParseInputFile` and `InitializeRoutingDB`, and then calls `RunInitialRouting`, `RunRRR`, and `WriteRoutingSolution` in sequence.  
`RoutingDB` maintains the usage / cost states of horizontal and vertical edges, as well as the working usage used by the router in the current round.  
The overall project follows a layered design of "parser / initializer / router / rrr / writer", with relatively clear module boundaries.

---

## 2. Data Structure Design

### 2.1 Input Problem Modeling

The input data is represented by `Problem`, whose main fields include:

- `rows`, `cols`: grid dimensions
- `vertical_capacity`, `horizontal_capacity`: capacities of vertical / horizontal edges
- `blocks`: set of blocked cells
- `nets`: set of nets to be routed
- `blocked`: blocked-cell bitmap

Each net is represented by `Net`, which contains:

- `name`
- `id`
- `pins`

Each coordinate point is represented by `Point(r, c)`, and comparison operators are overloaded so that it can be conveniently managed in a `set`.

### 2.2 Edge State Modeling

Each edge is represented by `EdgeState`, with the following fields:

- `usage`: current usage count
- `base_cost`: base cost
- `present_cost`: current congestion cost
- `history_cost`: historical congestion cost

Here, `present_cost` and `history_cost` together support negotiated-congestion routing.

### 2.3 Routing Result Modeling

The tree-structured result of a single net is represented by `RoutedTree`, which contains:

- `net_id`
- `net_name`
- `tree_points`
- `branches`

Among them, `branches` stores the path used each time a pin is connected to the existing tree.  
The final result is represented by `RoutingResult`, which contains:

- `routed_nets`
- `total_wirelength`
- `total_overflow`

---

## 3. Algorithm Flow

## 3.1 Overall Flow of Initial Routing

Initial routing is completed by `RunInitialRouting`, which calls `RunRouting`.  
`RunInitialRouting` first collects all `Net.id` values and then passes them to `RunRouting` together.

The main steps of `RunRouting` are as follows:

1. Validate `db.problem` and the net IDs to be routed;
2. Load the working usage from the currently committed edge state;
3. Call `DetermineRoutingOrder` to determine the net order for this round;
4. Route each net in order by calling `RouteNetAsTree`;
5. After each net is completed, call `WriteRoutedTreeToUsage` to write that net's usage into working usage;
6. After all nets are completed, update the history cost according to the final working usage;
7. Call `WriteUsageToEdgeState` to commit the working usage back to the edge state;
8. Collect the total wirelength and total overflow.

That is, this project does not route all nets first and then update the current congestion in one batch. Instead, **after each net is routed, subsequent nets immediately feel the updated congestion usage**; by contrast, history cost is updated only once at the end of the round.

## 3.2 Net Routing Order

`DetermineRoutingOrder` first filters out the subset of nets to be routed in the current round from all nets, and then uses a stable sort to determine the order. The sorting rules are:

1. **bounding-box perimeter in descending order**
2. **number of pins in descending order**
3. **original order in the input file has priority**

This is equivalent to prioritizing larger and more complex nets first, so as to reduce congestion that would be harder to handle later.

## 3.3 Tree Construction for a Single Net

The routing of a single net is completed by `RouteNetAsTree`, using a strategy of "gradually connecting pins into the existing tree."

### (1) Choose the seed pin

`ChooseSeedPin` computes, for each pin, the sum of Manhattan distances from that pin to all other pins, and selects the pin with the minimum sum as the seed.  
The intuition is that building the tree from a pin near the geometric center makes later paths easier to keep short.

### (2) Choose the next pin to connect

`ChooseNextPinToConnect` computes, for each unconnected pin, the minimum Manhattan distance to any point already in the current tree, and selects the nearest pin to connect.  
Therefore, the whole process is similar to a greedy incremental Steiner tree construction.

### (3) Use A* to connect to the current tree

For the selected pin, the router calls `AStarToTree(start, tree_points)`, starting from that pin and using A* search to connect it to any point in the current tree.  
Once a point in `tree_points` is reached, the search stops, and the complete path is reconstructed through the parent pointers.

### (4) Merge into the routed tree

After A* returns a path:

- Add all points on the path into `tree_points`
- Add the entire path into `branches`
- Remove the newly connected pin from `unconnected`

This continues until all pins have been connected into the tree.

---

## 4. A* Search and Cost Model

## 4.1 Search State

A* uses the following structures:

- `open_map`: records the states of nodes currently in the open set
- `close_map`: records the states of nodes whose expansion has been completed
- `open_heap`: a priority queue ordered by `(f, g, idx)`

Among them:

- `g`: the accumulated true cost from the start point to the current point
- `f = g + h`
- `h`: the minimum Manhattan distance to the current tree

This implementation does not support reopen: once a node enters `close_map`, it will not be reopened.

## 4.2 Conditions for Legal Expansion

Each time, A* attempts to expand from the current point in four directions:

- `Up`
- `Down`
- `Left`
- `Right`

Before expansion, the following conditions must hold:

1. An edge exists in that direction;
2. The edge is usable;
3. Neither endpoint of the edge lies on a blocked cell.

Therefore, blocked cells directly cut off the availability of related edges.

## 4.3 Cost Function

This project uses negotiated-congestion edge cost.  
The edge cost is computed as:

\[
\text{edge\_cost} = \text{base\_cost} \times \text{present\_cost} \times \text{history\_cost}
\]

where:

- `base_cost` defaults to 1
- `history_cost` is initialized to 1 and accumulates after each round
- `present_cost` is computed online from the current working usage:

\[
\text{present\_cost}=
\begin{cases}
1, & usage < capacity \\
1 + \lambda \cdot (usage - capacity + 1), & usage \ge capacity
\end{cases}
\]

The code uses:

- `lambda = 1.0`
- `mu = 0.5`

This means:

- **present cost** is responsible for the immediate congestion penalty within the current round
- **history cost** is responsible for the long-term penalty of repeated congestion across rounds

Therefore, this router is not a simple shortest-path router, but one that balances path length and congestion.

---

## 5. Overflow Handling and RRR

## 5.1 Victim Selection

RRR is implemented in `RunRRR`, with at most 50 iterations.  
In each iteration, it first rebuilds the database state according to the current global solution, and then checks whether any overflow edge still exists.

If overflow exists, it calls `SelectVictimNetIds` to select victim nets.  
The current victim selection rule is fairly direct:

- As long as a net's routed tree passes through an overflow edge, it will be selected as a victim.

## 5.2 Rip-up and Local Rerouting

After selecting the victims, RRR will:

1. Remove those victim nets from the current global solution;
2. Rebuild database usage using the remaining nets;
3. Call `RunRouting` only for the victim subset to reroute them;
4. Merge the new partial result back into the global solution;
5. Recompute overflow and wirelength.

Therefore, this implementation of RRR is a **victim reroute based on overflow edges**, rather than rerunning all nets globally.

## 5.3 Best-Solution Retention Strategy

During RRR, the program maintains the current best solution.  
The comparison rule is:

1. Prefer a smaller total overflow;
2. If the overflow is the same, prefer a smaller total wirelength.

The final returned result is the best solution observed over the whole iteration process, not necessarily the result from the last iteration.

---

## 6. Output and Legality Checking

The final result is written to `results/xxx_solution.txt`.  
During output, nets are sorted in ascending order of `net_id`, and each net is output in the following form:

- First output the net name
- Then output the edges `(r1, c1)-(r2, c2)` line by line
- Finally end with `!`

Before output, `ExpandTreeToEdges` checks whether each path segment is truly Manhattan-adjacent, and if any illegal branch is found, it throws an error immediately.  
Therefore, the writer module not only writes the file, but also performs part of the final consistency checking.

---

## 7. Experimental Results

## 7.1 Experimental Environment

- Build environment: `Ubuntu 24.04 / g++ 14.2.0`
- Compiler options: `-std=c++17 -Wall -Wextra -Wpedantic -O3 -Iinclude`
- Test dataset: `samples/sample0.txt ~ samples/sample4.txt`

## 7.2 Result Summary

| Case | Initial Wirelength | Initial Overflow | Final Wirelength | Final Overflow | Iterations |
|---|---:|---:|---:|---:|---:|
| sample0 | 12 | 0 | 12 | 0 | 1 |
| sample1 | 8233 | 1 | 8233 | 0 | 2 |
| sample2 | 10351 | 1 | 10350 | 0 | 2 |
| sample3 | 10497 | 2 | 10497 | 0 | 2 |
| sample4 | 12510 | 0 | 12510 | 0 | 1 |

## 7.3 Result Analysis

- initial routing performance: the initial routing is already able to produce feasible solutions directly on easier cases, with both `sample0` and `sample4` starting from an initial overflow of 0; on larger and more congestion-prone cases, the initial result leaves only a small amount of residual conflict, with initial overflow values of 1, 1, and 2 for `sample1`, `sample2`, and `sample3`, respectively. Overall, the initial routing can already produce solutions with relatively short wirelength and near-feasible quality, but a small amount of congestion conflict still appears on harder benchmarks.
- effect of RRR on overflow improvement: RRR is quite effective at eliminating the remaining overflow. For `sample1`, `sample2`, and `sample3`, the overflow drops to 0 after the first effective reroute, and the algorithm then stops at `iter=2` after detecting no overflow; by contrast, `sample0` and `sample4` already have zero overflow initially, so RRR exits immediately at `iter=1`. This indicates that the current victim-based RRR is quite capable of repairing small residual congestion.
- trade-off between wirelength and congestion: from the results, eliminating overflow does not cause noticeable wirelength inflation on this dataset. `sample1` and `sample3` both reduce overflow to 0 while keeping the wirelength unchanged; `sample2` even improves slightly from 10351 to 10350. This shows that the current RRR controls the trade-off between wirelength and congestion well in this experiment, and can complete congestion repair at very low cost.
- representative case study: `sample3` can serve as a representative hard case. Its initial overflow is 2, the most severe initial congestion among the five samples; in the first RRR round, the algorithm selects 10 victim nets for rip-up and reroute, and then reduces the overflow to 0 while keeping the final wirelength unchanged at 10497. In contrast, `sample2` also reduces its overflow from 1 to 0 after one effective reroute, and even reduces wirelength by 1, indicating that the current reroute strategy is not merely trading longer paths for feasibility.

---

## 8. Implementation Features and Limitations

### Strengths

1. **Clear module decomposition**  
   The parser, initializer, router, rrr, and writer are clearly separated, making the system easier to debug and extend.

2. **Complete cost model**  
   It considers base, present, and history costs simultaneously, and therefore has the basic characteristics of negotiated-congestion routing.

3. **Support for subset reroute**  
   `RunRouting` can process only a specified victim subset, which provides a stable interface for later RRR reuse.

4. **Result structure suitable for rip-up**  
   `RoutedTree` directly stores tree points and branch paths, making it convenient for rebuilding usage or performing reroute later.

### Limitations

1. **Tree construction is still greedy**  
   The current strategy connects the nearest pin to the current tree, which can easily fall into a local optimum.

2. **The A* heuristic is relatively simple**  
   It only uses the minimum Manhattan distance to the tree, without considering more complex congestion-aware heuristic information.

3. **Victim selection is relatively coarse**  
   Victims are currently selected only based on whether they pass through an overflow edge, without incorporating finer-grained indicators such as detour or congestion contribution.

4. **RRR mainly relies on repeated reroute**  
   Stronger historical penalty scheduling, regional guidance, or multi-source multi-sink optimization has not yet been added.

---

## 9. Conclusion

This project completes a runnable basic closed loop for global routing:  
from input parsing and database initialization, to initial routing, to overflow-based rip-up and reroute, and finally to result output, the whole flow is complete.

Its core ideas are:

- use **incremental tree construction** to handle multi-pin nets;
- use **A\*** to implement minimum-cost connection from a single source to the current tree;
- use **negotiated-congestion cost** to balance path length and congestion;
- use **victim-based RRR** to gradually reduce overflow.

From an implementation perspective, this project already has the complete framework of a course-project-level global router. If it is improved further in the future, the main directions should be stronger pin-connection strategies, smarter victim selection, and more effective congestion-cost scheduling.
