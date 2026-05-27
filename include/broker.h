#pragma once
#include "event.h"
#include <vector>

// ─────────────────────────────────────────────────────────────
// BrokerConfig：撮合参数，集中在一个 struct 里方便传递和测试
//
// slippage_bps   : 滑点，单位基点（1 bps = 0.01%）
//                  买入时成交价 = open × (1 + bps/10000)
//                  卖出时成交价 = open × (1 - bps/10000)
// commission_rate: 手续费率（按成交额比例）
// min_commission : 每笔最低手续费（防止小单手续费为零）
//
// 已知限制：
//   - 滑点是常数，生产环境可换成 √(qty/volume) 的冲击模型
//   - 暂不支持限价单、止损单
// ─────────────────────────────────────────────────────────────
struct BrokerConfig {
  double slippage_bps    = 5.0;    // 5 个基点
  double commission_rate = 0.001;  // 0.1%
  double min_commission  = 1.0;    // 最低 $1
};

// ─────────────────────────────────────────────────────────────
// Broker：模拟券商，负责挂单 → 撮合 → 产出 FillEvent
//
// 工作流程：
//   1. Engine 调用 place_order()，OrderEvent 进入挂单队列
//   2. 下一根 Bar 到来时，Engine 最先调用 fill_pending_orders()
//   3. Broker 以 bar.open 加滑点计算成交价，扣手续费，
//      把 FillEvent push 进 EventQueue
//   4. 挂单队列清空，等待下一批订单
//
// 为什么 fill_pending_orders 接收 EventQueue& 而不是返回 vector？
//   保持与系统其他部分一致的模式：产生事件就 push 进队列，
//   Engine 统一从队列里消费，不需要额外的中间容器
// ─────────────────────────────────────────────────────────────
class Broker {
public:
  explicit Broker(BrokerConfig config = {});

  // Engine 在处理 OrderEvent 时调用，把订单放入挂单队列
  void place_order(const OrderEvent& order);

  // Engine 在每根新 Bar 最开始调用
  // 以 bar.open 撮合所有挂单，FillEvent 直接 push 进 queue
  void fill_pending_orders(const Bar& bar, EventQueue& queue);

  int total_fills() const { return total_fills_; }

private:
  BrokerConfig              config_;
  std::vector<OrderEvent>   pending_;   // 等待撮合的订单
  int                       total_fills_ = 0;

  // 计算含滑点的成交价
  double apply_slippage(double price, Direction dir) const;

  // 计算手续费
  double calc_commission(double fill_price, int qty) const;
};
