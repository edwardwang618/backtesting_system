#pragma once

#include <queue>
#include <string>
#include <variant>

// ─────────────────────────────────────────────────────────────
// Bar：一根 OHLCV K 线，数据层的原子单位
//
// 设计说明：
//   - date 保留 string，只在 CSV 解析和输出时使用
//   - 内部比较/排序如果有需要，可以在 DataFeed 里转 int64_t
//   - 已知限制：暂不处理时区、纳秒精度（v2 可改为 int64_t ts_ns）
// ─────────────────────────────────────────────────────────────
struct Bar {
  std::string date;
  double open, high, low, close;
  long long volume;
};

// ─────────────────────────────────────────────────────────────
// 方向枚举
// LONG  = 买入 / 开多
// SHORT = 卖出 / 平仓（本系统 long-only，SHORT 仅表示"平仓意图"）
// ─────────────────────────────────────────────────────────────
enum class Direction { LONG, SHORT };

// ═════════════════════════════════════════════════════════════
// 四类事件（按回测生命周期排序）
//
// MarketEvent  → DataFeed 产生，Engine 分发给 Strategy 和 Broker
// SignalEvent  → Strategy 产生，只含方向，不含数量
// OrderEvent   → Portfolio 产生，含具体股数，交给 Broker
// FillEvent    → Broker 产生，含实际成交价和手续费，回到 Portfolio
// ═════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────
// MarketEvent
// symbol 字段是多标的扩展的基础：
//   即使现在只跑单只股票，Engine 也通过 symbol 路由到对应 Strategy
// ─────────────────────────────────────────────────────────────
struct MarketEvent {
  std::string symbol;
  Bar bar;
};

// ─────────────────────────────────────────────────────────────
// SignalEvent
// Strategy 只表达"想做什么"，不决定"做多少"
//   → 仓位大小是 Portfolio 的职责，让 Strategy 可以无视账户规模复用
//
// 已知限制：
//   - 暂用枚举方向，v2 可改为 target_weight: double ∈ [-1, 1]
//     以支持按比例建仓、多策略权重叠加
// ─────────────────────────────────────────────────────────────
struct SignalEvent {
  std::string symbol;
  Direction direction;
};

// ─────────────────────────────────────────────────────────────
// OrderEvent
// Portfolio 根据当前持仓和资金决定 quantity
// Broker 收到后挂入待成交队列，等下一 Bar 开盘撮合
// ─────────────────────────────────────────────────────────────
struct OrderEvent {
  std::string symbol;
  Direction direction;
  int quantity; // 股数，正整数
};

// ─────────────────────────────────────────────────────────────
// FillEvent
// fill_price 已含滑点（Broker 计算后写入）
// commission 是本笔交易的实际手续费
// ─────────────────────────────────────────────────────────────
struct FillEvent {
  std::string symbol;
  Direction direction;
  int quantity;
  double fill_price;
  double commission;
};

// ═════════════════════════════════════════════════════════════
// Event = std::variant<...>
//
// 为什么不用 shared_ptr<BaseEvent> + 虚函数？
//
//   旧做法的代价：
//     1. 每个事件一次 heap 分配（new + delete）
//     2. 虚函数 dispatch → 依赖 vtable 指针，缓存不友好
//     3. 引用计数原子操作（shared_ptr），事件没有共享所有权，纯浪费
//
//   variant 的收益：
//     1. 事件内联存在 queue 里，无 heap 分配
//     2. std::visit → 编译器生成跳转表，无虚表
//     3. 类型封闭：加新事件类型时，所有 visit 不处理它会报编译错误
//        → 编译器帮你检查"有没有漏掉处理某类事件"
//
// EventQueue：整个系统的消息总线
// ═════════════════════════════════════════════════════════════
using Event = std::variant<MarketEvent, SignalEvent, OrderEvent, FillEvent>;
using EventQueue = std::queue<Event>;
