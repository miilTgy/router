请将 `router.md` 中与 congestion cost / edge cost 相关的描述，修改为真正的 negotiated-congestion routing。不要重复已有的 router 主流程，不要改成内置 RRR 框架；只修改“edge cost 定义、usage 维护、present cost 更新时机、history cost 更新时机”这些部分。

请明确采用下面这套规则，并把文档中原先模糊或不完整的 cost 更新描述替换掉：

1. edge 维护以下状态  
- `base_cost[e]`：静态基础代价，默认为 1
- `usage[e]`：当前这一轮内，已经被已完成布线的 nets 占用了多少次  
- `capacity[e]`：edge 容量，从之前模块吐出的数据结构中获取，capacity是永恒固定的不要更新。
- `hist_cost[e]`：历史拥塞代价，跨轮累积，初始化为 1

其中，执行router开始，从 common.h 中定义的 EdgeState 里加载所有edge的usage，保存为router自己内部的变量，此后每route一条net只更新router自己的usage，route完所有nets后再将现在的usage commit给外部 EdgeState。

1. edge 的搜索代价公式  
在 Dijkstra / A* 扩展时，使用：
`edge_cost[e] = base_cost[e] * present_cost[e] * hist_cost[e]`

其中：
- 当 `usage[e] < capacity[e]` 时  
  `present_cost[e] = 1`
- 当 `usage[e] >= capacity[e]` 时  
  `present_cost[e] = 1 + lambda * (usage[e] - capacity[e] + 1)`

这里 `lambda` 是 present congestion penalty 系数，是当前轮内的即时拥塞惩罚强度。

3. present cost 的更新时机  
present cost 不单独存成需要整轮 commit 的数组，而是由当前 `usage[e]` 即时决定。  
因此每当 route 完一条 net、并将该 net 路径上的所有 edge 的 `usage[e] += 1` 之后，后续 nets 在搜索时看到的 `present_cost[e]` 就立刻变大。  
也就是说：
- **每 route 完一条 net，立刻更新该 net 覆盖到的 edge 的 usage**
- **present cost 由最新 usage 在线计算，因此等价于每条 net 后立即更新**

请在文档中明确写清：  
**present cost 的生效粒度是“每条 net 结束后立刻影响后续 net 的搜索”。**

4. history cost 的更新时机  
history cost 不在每条 net 后更新，而是在一整轮所有 nets 都 route 完之后，统一 commit 一次。  
对每条 edge，先计算：
`overflow[e] = max(0, usage[e] - capacity[e])`

然后统一执行：
`hist_cost[e] = hist_cost[e] + mu * overflow[e]`

其中 `mu` 是 history accumulation 系数。

请在文档中明确写清：  
- **history cost 是跨轮累积的长期惩罚**
- **只有在一整轮结束后，才统一根据 overflow 提交更新**
- **history cost 不是保存上一轮 current cost，也不是 present cost 的拷贝**

5. 一轮 routing 的 cost / usage 流程  
请把文档中的相关段落改成下面这个语义：
- 开始一轮前，加载上一轮累计下来的 `hist_cost`
- 加载 `usage`
- 按既定 net ordering 逐条 route net
- 每条 net 找到路径后，立刻把该路径上的 `usage[e] += 1`
- 后续 nets 的搜索直接使用更新后的 `usage`，从而感受到新的 `present_cost`
- 当整轮所有 nets 都完成后，遍历所有 edge，按 `overflow` 统一更新 `hist_cost`
- 下一轮 reroute 时继续使用更新后的 `hist_cost`

1. 文档中要强调的设计意图  
请把 negotiated-congestion 的意图写清楚：
- `present_cost` 负责反映“当前这一轮、此刻现场是否拥塞”
- `hist_cost` 负责反映“这条 edge 在前几轮里是否反复成为热点”
- 前者是短期即时惩罚，后者是长期累积惩罚
- A* 本身只负责在当前 `edge_cost` 下找最小代价路径，不负责定义 history 更新规则

1. 不要写成下面这些错误表述  
请删除或改写任何类似以下含义的描述：
- “history cost 保存上一轮的 current cost”
- “所有 cost 只在整轮结束后统一更新，包括 present cost”
- “route 一条 net 后不修改 usage，要等整轮结束再反映拥塞”
- “A* 自己负责 negotiated-congestion 更新”

1. 若文档里需要给出推荐默认参数，可写成示例而不是硬编码规范  
例如：
- `base_cost = 1`
- `lambda` 取一个中等正数，用于控制当前拥塞惩罚强度
- `mu` 取一个较小正数，用于控制跨轮历史惩罚累积速度

只改写 `router.md` 里关于 cost model / congestion update / route-after-commit 机制的说明文字，不要重复已有 parser、graph、A* 基础流程，不要引入额外的 RRR 总控框架。