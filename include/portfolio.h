#pragma once
#include "event.h"
#include <map>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────
// Portfolio：账户状态的唯一持有者
//
// 持有：
//   cash_        现金余额
//   positions_   每个 symbol 的持仓股数（正 = 多头，0 = 空仓）
//   last_prices_ 每个 symbol 最近一次成交或收盘价，用于 NAV 估值
//   equity_curve_ 每根 Bar 结束时的净值快照
//
// 已知限制：
//   - Long-only：SHORT 信号只平仓，不开空头
//   - 固定仓位：每次买入固定 trade_size_ 股，不做按比例建仓
//     v2 可改为 SignalEvent 携带 target_weight，Portfolio 换算成股数
//   - 不检查现金是否充足（超买时现金会变负），假设 trade_size 配置合理
// ─────────────────────────────────────────────────────────────
class Portfolio {
public:
  Portfolio(double initial_cash, int trade_size);

  // 收到策略信号，根据当前持仓决定是否下单
  // 产出的 OrderEvent 直接 push 进 queue
  void on_signal(const SignalEvent& signal, EventQueue& queue);

  // 收到成交回报，更新持仓和现金
  void on_fill(const FillEvent& fill);

  // 每根 Bar 结束时调用，记录当前净值快照
  // close_price 是该 Bar 的收盘价，用于估算持仓市值
  void update_nav(const std::string& date,
                  const std::string& symbol,
                  double close_price);

  // ── 查询接口（供 Engine / Metrics 读取）──────────────────
  double cash()                          const { return cash_; }
  int    position(const std::string& sym) const;
  double total_commission()              const { return total_commission_; }

  const std::vector<double>&      equity_curve() const { return equity_curve_; }
  const std::vector<std::string>& dates()        const { return dates_; }

private:
  double initial_cash_;
  double cash_;
  int    trade_size_;

  std::map<std::string, int>    positions_;    // symbol → 持仓股数
  std::map<std::string, double> last_prices_;  // symbol → 最近价格（NAV 估值用）

  std::vector<double>      equity_curve_;
  std::vector<std::string> dates_;
  double                   total_commission_ = 0.0;

  // 当前账户总净值 = 现金 + Σ(持仓 × 最近价格)
  double current_nav() const;
};
