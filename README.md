# 回测系统（C++ 实现）

基于事件驱动架构的股票回测框架，纯 C++17，无外部依赖。

## 架构概览

```
┌─────────────────────────────────────────────────────┐
│                     main.cpp                        │
│              配置参数 / 组装 / 启动                   │
└──────────────────────────┬──────────────────────────┘
                           │
                    ┌──────▼──────┐
                    │   Engine    │  ← 主事件循环
                    └──┬──┬──┬───┘
                       │  │  │
          ┌────────────┘  │  └──────────────┐
          │               │                 │
   ┌──────▼──────┐ ┌──────▼──────┐  ┌──────▼──────┐
   │  DataFeed   │ │  Strategy   │  │   Broker    │
   │  (CSV读取)   │ │  (信号生成)  │  │  (订单撮合) │
   └──────┬──────┘ └──────┬──────┘  └──────┬──────┘
          │               │                 │
          └───────────────▼─────────────────┘
                   ┌──────────────┐
                   │  Portfolio   │  ← 持仓/净值管理
                   └──────┬───────┘
                          │
                   ┌──────▼───────┐
                   │   Metrics    │  ← 绩效统计（控制台输出）
                   └──────────────┘
```

## 事件流（防未来函数设计）

```
Bar[t] 到来
  │
  ├─ Broker 以 Bar[t].open 撮合上一根 Bar 留下的挂单 → FillEvent
  │    └─ Portfolio.on_fill() → 更新持仓 / 现金
  │
  ├─ Strategy.on_bar(Bar[t]) → SignalEvent（基于 Bar[t].close）
  │    └─ Portfolio.on_signal() → OrderEvent → 进入 Broker 挂单队列
  │
  └─ Portfolio.update_nav(Bar[t].close) → 记录净值曲线

Bar[t+1] 到来时才撮合 Bar[t] 产生的订单  ← 关键：避免 lookahead bias
```

## 目录结构

```
.
├── CMakeLists.txt
├── README.md
├── main.cpp
├── include/
│   ├── event.h          # 四类事件数据结构
│   ├── data_feed.h      # CSV 数据加载与逐 Bar 迭代
│   ├── broker.h         # 模拟撮合（滑点 + 手续费）
│   ├── portfolio.h      # 持仓 / 现金 / 净值
│   ├── strategy.h       # 策略抽象基类
│   ├── sma_cross.h      # 双均线交叉策略
│   ├── engine.h         # 主事件循环
│   └── metrics.h        # 绩效指标
├── src/
│   ├── data_feed.cpp
│   ├── broker.cpp
│   ├── portfolio.cpp
│   ├── sma_cross.cpp
│   ├── engine.cpp
│   └── metrics.cpp
└── tools/
    └── gen_sample.py    # 生成示例 CSV 数据
```

## 模块说明

| 模块 | 职责 |
|------|------|
| `event.h` | `MarketEvent` / `SignalEvent` / `OrderEvent` / `FillEvent` 数据结构 |
| `DataFeed` | 加载 CSV，`has_next()` / `next()` 逐 Bar 推进 |
| `Broker` | 维护挂单队列，下一 Bar 开盘价成交，含滑点（bps）和手续费 |
| `Portfolio` | `on_signal()` → 生成订单；`on_fill()` → 更新持仓/现金；`update_nav()` → 净值曲线 |
| `Strategy` | 虚基类，子类实现 `on_bar()` |
| `SmaCross` | 快慢双均线金叉开多、死叉平仓；long-only |
| `Engine` | 驱动事件队列，协调所有模块 |
| `Metrics` | 年化收益、波动率、Sharpe、最大回撤，打印到控制台 |

## 关键设计

- **事件队列**：`std::queue<std::shared_ptr<Event>>`，模块间零直接依赖
- **防未来函数**：当 Bar 信号 → 次 Bar 开盘成交
- **Long-only**：金叉买入，死叉卖出所有持仓
- **无外部依赖**：纯标准库，CMake 构建

## 构建与运行

```bash
# 生成示例数据
python3 tools/gen_sample.py          # 生成 data/sample/AAPL.csv

# 编译
cmake -B build && cmake --build build

# 运行
./build/backtest --csv data/sample/AAPL.csv --symbol AAPL \
                 --cash 100000 --fast 10 --slow 30 --size 100
```

## CSV 格式

```
date,open,high,low,close,volume
2020-01-02,300.35,300.60,295.19,298.65,33870100
2020-01-03,297.15,300.58,296.50,297.43,36580700
```

## 输出示例

```
Running backtest: AAPL SMA(10,30) | Cash: $100000 | Trade size: 100 shares

========== Backtest Results ==========
Initial Cash:       $  100000.0000
Final Equity:       $  118432.5100
Total Return:           18.4325%
Annualized Return:       8.7612%
Volatility (ann):       22.1034%
Sharpe Ratio:            0.3965
Max Drawdown:           14.2210%
Num Bars:                    504
Num Trades:                   18
Total Commission:   $     183.4500
======================================
```
