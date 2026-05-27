#include "portfolio.h"

Portfolio::Portfolio(double initial_cash, int trade_size)
  : initial_cash_(initial_cash),
    cash_(initial_cash),
    trade_size_(trade_size) {}

// ─────────────────────────────────────────────────────────────
// on_signal：把策略信号转换成具体订单
//
// 逻辑：
//   LONG  信号 + 空仓 → 买入 trade_size_ 股
//   SHORT 信号 + 有仓 → 卖出全部持仓（平仓）
//   其他情况静默跳过，避免重复下单
//
// 为什么 SHORT 是卖出全部而不是卖出 trade_size_？
//   Long-only 系统里平仓必须把所有仓位清干净，
//   否则可能因建仓时分批买入导致持仓量与 trade_size_ 不一致
// ─────────────────────────────────────────────────────────────
void Portfolio::on_signal(const SignalEvent& signal, EventQueue& queue) {
  int pos = position(signal.symbol);

  if (signal.direction == Direction::LONG && pos == 0) {
    queue.push(OrderEvent{signal.symbol, Direction::LONG, trade_size_});

  } else if (signal.direction == Direction::SHORT && pos > 0) {
    // 卖出全部持仓，而不是固定的 trade_size_
    queue.push(OrderEvent{signal.symbol, Direction::SHORT, pos});
  }
  // pos > 0 收到 LONG，或 pos == 0 收到 SHORT：都不操作
}

// ─────────────────────────────────────────────────────────────
// on_fill：根据成交回报更新持仓和现金
//
// 买入：持仓增加，现金减少（成交额 + 手续费）
// 卖出：持仓减少，现金增加（成交额 - 手续费）
//
// 注意符号：
//   sign = +1（买入），-1（卖出）
//   持仓变化 = sign × quantity
//   现金变化 = -sign × quantity × fill_price - commission
//              （买入时现金减少，卖出时现金增加，手续费总是减少）
// ─────────────────────────────────────────────────────────────
void Portfolio::on_fill(const FillEvent& fill) {
  int sign = (fill.direction == Direction::LONG) ? +1 : -1;

  positions_[fill.symbol]  += sign * fill.quantity;
  cash_                    -= sign * fill.quantity * fill.fill_price;
  cash_                    -= fill.commission;
  total_commission_        += fill.commission;
  last_prices_[fill.symbol] = fill.fill_price;
}

// ─────────────────────────────────────────────────────────────
// update_nav：每根 Bar 收盘时记录一次净值快照
//
// 先用收盘价更新该 symbol 的 last_prices_，
// 再计算全账户净值（现金 + 所有持仓的市值）
// ─────────────────────────────────────────────────────────────
void Portfolio::update_nav(const std::string& date,
                           const std::string& symbol,
                           double close_price) {
  last_prices_[symbol] = close_price;
  equity_curve_.push_back(current_nav());
  dates_.push_back(date);
}

// ─────────────────────────────────────────────────────────────
// current_nav：遍历所有持仓，用 last_prices_ 估值
//
// 如果某个 symbol 没有 last_prices_ 记录（理论上不会发生），
// 跳过它，避免用脏数据污染净值
// ─────────────────────────────────────────────────────────────
double Portfolio::current_nav() const {
  double nav = cash_;
  for (const auto& [sym, qty] : positions_) {
    auto it = last_prices_.find(sym);
    if (it != last_prices_.end())
      nav += qty * it->second;
  }
  return nav;
}

int Portfolio::position(const std::string& sym) const {
  auto it = positions_.find(sym);
  return (it != positions_.end()) ? it->second : 0;
}
