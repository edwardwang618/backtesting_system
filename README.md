# 回测系统（C++ 实现）

基于事件驱动架构的股票回测框架，纯 C++17，无外部依赖。

---

## 整体架构

系统由六个模块组成，通过一条共享的事件队列松耦合地协作：

```
DataFeed ──MarketEvent──▶ Engine ──▶ Broker      (撮合上一 Bar 的挂单)
                            │
                            └──────▶ Strategy    (观察当前 Bar，产生信号)
                                        │
                                    SignalEvent
                                        │
                                    Portfolio   (决定下单量)
                                        │
                                    OrderEvent
                                        │
                                    Broker      (挂单，等下一 Bar 开盘成交)
                                        │
                                    FillEvent
                                        │
                                    Portfolio   (更新持仓 / 现金 / 净值)
```

关键时序：**当根 Bar 产生的信号，在下一根 Bar 的开盘价成交**。这是防止 lookahead bias（未来函数）的核心机制——策略只能用已经收盘的数据下单，成交价是下一个交易日的开盘价，与实盘行为一致。

---

## 模块详解

### 1. 事件系统（`include/event.h`）

整个系统的通信协议。定义了四种事件，覆盖回测生命周期的四个阶段：

- **`MarketEvent`**：DataFeed 产生，携带一根 OHLCV Bar。Engine 收到后先触发 Broker 撮合旧挂单，再把 Bar 送给 Strategy。
- **`SignalEvent`**：Strategy 产生，只含方向（LONG / SHORT），不含数量。方向是策略的职责，数量是组合管理的职责，两者分离。
- **`OrderEvent`**：Portfolio 产生，含具体股数。Portfolio 根据当前持仓和可用资金决定买多少，再交给 Broker。
- **`FillEvent`**：Broker 产生，记录实际成交价（含滑点）和手续费，送回 Portfolio 更新账户状态。

实现上，事件类型定义为 `std::variant<MarketEvent, SignalEvent, OrderEvent, FillEvent>`，队列是 `std::queue<Event>`。选择 variant 而非虚基类 + `shared_ptr` 的原因是：variant 内联存储在队列里，没有堆分配，dispatch 通过 `std::visit` 在编译期生成跳转逻辑，比虚函数快；更重要的是，如果新增事件类型但忘了在 visit 里处理，编译器会报错，而不是运行期悄悄跳过。

### 2. 数据层（`include/data_feed.h` / `src/data_feed.cpp`）

DataFeed 负责把 CSV 文件变成一个可逐步消费的 `MarketEvent` 序列。构造时一次性解析整个文件存入 `std::vector<Bar>`，对外只暴露 `has_next()` 和 `next()` 两个接口，Engine 感知不到"CSV"的存在。

CSV 解析时先读 header 行，确定各列的位置索引，再逐行按索引取值。这样列顺序变化时（不同数据源的导出格式不同）代码不用改。解析失败的单行会打印警告并跳过，不会中断整个加载过程。

### 3. 券商模拟（`include/broker.h` / `src/broker.cpp`）

Broker 维护一个挂单队列（`std::vector<OrderEvent>`）。每根新 Bar 到来时，Engine 先调用 `fill_pending_orders(bar)`，Broker 以该 Bar 的开盘价加上滑点计算成交价，扣除手续费，生成 `FillEvent` 推入事件队列。

滑点模型：固定基点数（bps）× 成交价。买入方向价格向上移，卖出方向价格向下移，模拟市场冲击。手续费模型：`max(最低手续费, 成交额 × 费率)`，匹配主流券商收费结构。

### 4. 组合管理（`include/portfolio.h` / `src/portfolio.cpp`）

Portfolio 是账户状态的唯一持有者，管理三样东西：

- **持仓**：`std::map<symbol, int>`，正数代表多头持仓，0 代表空仓（本系统 long-only，不做空）。
- **现金**：买入时减少，卖出时增加，每次 FillEvent 同步更新。
- **净值曲线**：每根 Bar 结束时，用 `持仓市值 + 现金` 记一个快照，存入 `std::vector<double>`，供最后绩效分析使用。

收到 `SignalEvent` 后，Portfolio 查当前持仓：如果是 LONG 信号且空仓，生成买入 OrderEvent；如果是 SHORT 信号且有持仓，生成卖出全部的 OrderEvent。订单数量由 `trade_size`（固定股数）决定，是已知限制之一（见下文）。

### 5. 策略层（`include/strategy.h` / `include/sma_cross.h` / `src/sma_cross.cpp`）

`Strategy` 是纯虚基类，只有一个接口：`on_bar(symbol, bar, queue)`。子类在这里实现信号逻辑，把 `SignalEvent` 推入队列。

内置策略 `SmaCross` 是双均线交叉：维护每个 symbol 的收盘价历史，每根 Bar 计算快线（短周期 SMA）和慢线（长周期 SMA），检测金叉（快线上穿慢线，发 LONG 信号）和死叉（快线下穿慢线，发 SHORT 信号）。使用 `std::deque<double>` 存历史价格，只保留计算所需的最少数量，内存占用恒定。

### 6. 主引擎（`include/engine.h` / `src/engine.cpp`）

Engine 是整个系统的调度中心，持有其他所有模块的引用（不拥有，不负责生命周期），驱动一个大循环：

```
while DataFeed.has_next():
  bar = DataFeed.next()
  Broker.fill_pending_orders(bar)   // 先撮合旧订单
  process_queue()                   // 处理 FillEvent → 更新 Portfolio
  Strategy.on_bar(bar)              // 策略观察新 Bar
  process_queue()                   // 处理 Signal → Order → 进入 Broker 挂单
  Portfolio.update_nav(bar.close)   // 记录净值
```

`process_queue()` 内部用 `std::visit` 把每个事件分发到对应的处理函数。

### 7. 绩效分析（`include/metrics.h` / `src/metrics.cpp`）

回测结束后，从 Portfolio 取出净值曲线，计算以下指标并打印到控制台：

- **总收益率**：`(期末净值 - 初始资金) / 初始资金`
- **年化收益率**：假设每年 252 个交易日，复利折算
- **年化波动率**：日收益率标准差 × √252
- **Sharpe 比率**：`(年化收益 - 无风险利率) / 年化波动率`，默认无风险利率为 0
- **最大回撤**：从历史最高净值到后续最低点的最大跌幅，衡量最坏情况下的亏损

---

## 模块间的连接方式

所有模块之间**只通过事件队列通信，不互相持有引用**（除 Engine 外）。具体来说：

- Strategy 不知道 Portfolio 存在，它只往队列里 push `SignalEvent`
- Portfolio 不知道 Strategy 存在，它只从队列里 pop `SignalEvent`，再往里 push `OrderEvent`
- Broker 不知道 Portfolio 存在，它消费 `OrderEvent`，产出 `FillEvent`

Engine 是唯一持有所有模块引用的地方，负责按正确顺序调用它们，但它不包含任何业务逻辑。

这种设计的好处：替换策略不需要改 Portfolio，替换撮合逻辑不需要改 Strategy，加新模块（比如风控层）只需要在 Engine 里加一个事件处理分支。

---

## 已知限制

这些限制在 v1 是有意为之的简化，不是遗漏。

| 限制 | 当前行为 | 扩展方向 |
|------|----------|----------|
| 单标的 | DataFeed 只加载一个 symbol | v2：多 DataFeed 按时间戳做 k-way merge |
| 固定仓位 | 每次下单固定 `trade_size` 股 | v2：SignalEvent 改用 `target_weight ∈ [-1,1]`，Portfolio 换算股数 |
| 只有市价单 | Broker 只支持市价，下一 Bar 开盘成交 | v2：加限价单，在 Broker 里判断 high/low 是否触及 |
| 不做空 | SHORT 信号只平仓，不开空仓 | v2：Portfolio 允许负持仓，Broker 支持融券手续费 |
| 无涨跌停约束 | 开盘一定能成交 | v2：Broker 检查开盘价是否触及涨跌停，拒绝成交 |
| 无流动性约束 | 不管成交量多少都能全量成交 | v2：订单量超过当日成交量的一定比例时做部分成交 |
| 固定滑点 | 滑点是常数 bps | v2：`slippage ∝ √(order_qty / bar_volume)`，市场冲击模型 |
| 假设 CSV 已复权 | 不处理除权除息 | v2：DataFeed 支持前复权因子列 |
| 无基准对比 | 只输出绝对指标 | v2：加 Alpha、Beta、Information Ratio（需传入基准净值） |
| 控制台输出 | 只打印 summary | v2：输出 trade blotter CSV + 净值曲线 CSV，便于外部可视化 |

---

## 生产环境的改进方向

### 性能

当前实现对单次回测已经足够快，但如果要做**参数优化**（网格搜索 fast/slow 的组合）或**蒙特卡洛模拟**，性能会成为瓶颈。可以改进的地方：

- **内存池（Memory Pool）**：虽然 variant 本身没有堆分配，但 `Bar` 里的 `std::string date` 每次构造都会分配内存。可以把 date 改成 `int32_t`（YYYYMMDD 整数），或用 `std::string_view` + 统一存储。
- **环形缓冲区**：Strategy 的历史价格用 `std::deque`，可以换成固定大小的 ring buffer，避免内存碎片。
- **并行回测**：参数优化时，不同参数组合的回测之间互相独立，可以用 `std::thread` 或 `OpenMP` 并行跑，共享只读的 DataFeed（DataFeed 加载后不再写，天然线程安全）。

### 正确性与可靠性

- **确定性**：Engine 里遍历 `std::map` 是有序的，但如果换成 `unordered_map` 就不确定了。所有影响下单顺序的容器都应使用有序版本，保证相同输入总产生相同结果。
- **整数价格**：用 `double` 存价格会有浮点误差，累积多笔交易后 PnL 计算可能出现微小偏差。生产环境通常把价格存成整数（以最小价格单位为单位），在显示时再转回小数。
- **时间戳精度**：Bar 里的 date 是字符串，适合日线。分钟线或 tick 数据需要改成 `int64_t` 纳秒戳，并在 DataFeed 里做时区转换。
- **单元测试**：至少要覆盖三个场景：Portfolio 的 PnL 手工验算、防 lookahead 验证（构造特定数据，确认信号在次 Bar 成交）、SMA 边界（窗口未满时不产生信号）。

### 可观测性

- **Trade Blotter**：每笔成交写一行 CSV（时间、方向、价格、数量、手续费、累计 PnL），是调试策略逻辑和审计的基础。
- **净值曲线 CSV**：逐 Bar 的净值快照，供 Python/Jupyter 做可视化和归因分析。
- **日志分级**：当前用 `fprintf(stderr, ...)` 打印警告，生产环境应接入结构化日志（`spdlog` 或自建），支持按模块过滤和日志持久化。

### 扩展性

- **多策略组合**：Engine 支持多个 Strategy 实例，各自产生 SignalEvent，Portfolio 负责聚合多个策略的信号并分配权重，最终生成一个净头寸的 OrderEvent。
- **实时行情接入**：DataFeed 换成从 WebSocket 或消息队列（Kafka）读取，Engine 的主循环从"迭代文件"变成"阻塞等待事件"，其他模块几乎不用改。这是事件驱动架构最大的优势。
- **风控层**：在 Portfolio → Broker 之间加一个 RiskManager，拦截 OrderEvent，检查单笔仓位上限、组合集中度、日内回撤熔断等约束，不满足时修改或拒绝订单。

---

## 目录结构

```
.
├── CMakeLists.txt
├── README.md
├── main.cpp
├── include/
│   ├── event.h        # 四类事件（variant）
│   ├── data_feed.h    # CSV 数据加载
│   ├── broker.h       # 订单撮合
│   ├── portfolio.h    # 持仓 / 现金 / 净值
│   ├── strategy.h     # 策略虚基类
│   ├── sma_cross.h    # 双均线策略
│   ├── engine.h       # 主事件循环
│   └── metrics.h      # 绩效指标
├── src/
│   ├── data_feed.cpp
│   ├── broker.cpp
│   ├── portfolio.cpp
│   ├── sma_cross.cpp
│   ├── engine.cpp
│   └── metrics.cpp
├── data/
│   └── sample/
│       └── AAPL.csv   # 由 tools/gen_sample.py 生成
└── tools/
    └── gen_sample.py  # 合成 OHLCV 数据生成器
```

---

## 构建与运行

```bash
# 生成示例数据（需要 Python 3）
cd backtesting_system
python3 tools/gen_sample.py

# 编译
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 运行（默认参数）
./build/backtest

# 自定义参数
./build/backtest --csv data/sample/AAPL.csv \
                 --symbol AAPL               \
                 --cash 100000               \
                 --fast 10                   \
                 --slow 30                   \
                 --size 100
```

## CSV 格式

列名大小写不敏感，列顺序任意：

```
date,open,high,low,close,volume
2020-01-02,300.35,300.60,295.19,298.65,33870100
2020-01-03,297.15,300.58,296.50,297.43,36580700
```
