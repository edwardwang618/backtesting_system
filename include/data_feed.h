#pragma once
#include "event.h"
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────
// DataFeed：数据层，CSV → MarketEvent 序列
//
// 设计原则：
//   - 构造时一次性加载所有 Bar（回测场景，内存够用）
//   - 对外只暴露 has_next / next 两个接口，Engine 感知不到"CSV"这件事
//   - next() 返回值类型，与 EventQueue 的值语义一致，无堆分配
//
// 已知限制：
//   - 单 symbol，v2 可改为多 DataFeed 合并（k-way merge by timestamp）
//   - 假设 CSV 已按日期升序排列
// ─────────────────────────────────────────────────────────────
class DataFeed {
public:
  // 构造时立即解析 CSV，失败则抛 std::runtime_error
  DataFeed(std::string symbol, const std::string &csv_path);

  bool has_next() const;
  MarketEvent next(); // 调用前必须先检查 has_next()

  const std::string &symbol() const { return symbol_; }
  size_t total_bars() const { return bars_.size(); }

private:
  std::string symbol_;
  std::vector<Bar> bars_;
  size_t index_ = 0;

  // 解析 header 行，返回各列的下标
  // 这样列顺序变化时代码不用改
  struct ColIndex {
    int date = -1, open = -1, high = -1, low = -1, close = -1, volume = -1;
  };
  static ColIndex parse_header(const std::string &header_line);
  static Bar parse_row(const std::string &line, const ColIndex &idx);
};
