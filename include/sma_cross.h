#pragma once
#include "strategy.h"
#include <deque>
#include <map>
#include <string>

// ─────────────────────────────────────────────────────────────
// SmaCross：双均线交叉策略（long-only）
//
// 逻辑：
//   金叉（fast 上穿 slow）→ SignalEvent::LONG  （开多仓）
//   死叉（fast 下穿 slow）→ SignalEvent::SHORT （平多仓）
//
// 参数：
//   fast_period : 快线周期（短均线），如 10
//   slow_period : 慢线周期（长均线），如 30
//   需满足 fast_period < slow_period
//
// 状态：
//   history_ : 每个 symbol 的收盘价历史（deque，长度固定为 slow+1）
//   state_   : 每个 symbol 的当前仓位状态（+1 多头，0 空仓）
//              Strategy 自己维护状态，避免向 Portfolio 查询
//              （两者通过事件解耦，不直接通信）
// ─────────────────────────────────────────────────────────────
class SmaCross : public Strategy {
public:
  SmaCross(int fast_period, int slow_period);

  void on_bar(const MarketEvent& market, EventQueue& queue) override;

private:
  int fast_period_;
  int slow_period_;

  std::map<std::string, std::deque<double>> history_;
  std::map<std::string, int>                state_;   // +1 or 0

  // 计算 deque 中从 start 开始，长度为 len 的子段的均值
  static double sma(const std::deque<double>& d, int start, int len);
};
