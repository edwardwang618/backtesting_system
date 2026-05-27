#include "broker.h"
#include <cmath>

Broker::Broker(BrokerConfig config) : config_(std::move(config)) {}

void Broker::place_order(const OrderEvent &order) { pending_.push_back(order); }

// ─────────────────────────────────────────────────────────────
// 核心撮合逻辑
//
// 调用时机：Engine 在每根新 Bar 最开始调用，早于 Strategy.on_bar()
// 这保证了：Bar[t] 的信号产生的订单，在 Bar[t+1].open 成交
//
// 遍历所有挂单 → 计算成交价 → 计算手续费 → push FillEvent → 清空挂单
// ─────────────────────────────────────────────────────────────
void Broker::fill_pending_orders(const Bar &bar, EventQueue &queue) {
  for (const auto &order : pending_) {
    double fill_price = apply_slippage(bar.open, order.direction);
    double commission = calc_commission(fill_price, order.quantity);

    queue.push(FillEvent{order.symbol, order.direction, order.quantity,
                         fill_price, commission});

    ++total_fills_;
  }
  pending_.clear();
}

// ─────────────────────────────────────────────────────────────
// 滑点：买入价上移，卖出价下移
//
// 例：open = 100, slippage = 5 bps = 0.0005
//   买入成交价 = 100 × 1.0005 = 100.05
//   卖出成交价 = 100 × 0.9995 =  99.95
// ─────────────────────────────────────────────────────────────
double Broker::apply_slippage(double price, Direction dir) const {
  double factor = config_.slippage_bps / 10000.0;
  return dir == Direction::LONG ? price * (1.0 + factor)
                                : price * (1.0 - factor);
}

// 手续费 = max(最低手续费, 成交额 × 费率)
double Broker::calc_commission(double fill_price, int qty) const {
  return std::max(config_.min_commission,
                  fill_price * qty * config_.commission_rate);
}
