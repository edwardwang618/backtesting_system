#pragma once
#include "event.h"

// ─────────────────────────────────────────────────────────────
// Strategy：策略的纯虚基类
//
// 设计原则：
//   - Engine 只持有 Strategy& 引用，通过这个接口调用，不知道具体策略类型
//   - 策略只能往 queue 里 push SignalEvent，不能直接操作 Portfolio 或 Broker
//   - on_bar 在每根 Bar 收盘后调用，参数是完整的 MarketEvent
//     （含 symbol 和 bar，为未来多标的做好准备）
//
// 扩展：
//   - 可以加 on_fill(FillEvent) 让策略感知成交（用于动态仓位管理）
//   - 可以加 on_start() / on_end() 做初始化和清理
// ─────────────────────────────────────────────────────────────
class Strategy {
public:
  virtual ~Strategy() = default;
  virtual void on_bar(const MarketEvent& market, EventQueue& queue) = 0;
};
