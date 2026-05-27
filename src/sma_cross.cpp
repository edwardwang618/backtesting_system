#include "sma_cross.h"
#include <stdexcept>

SmaCross::SmaCross(int fast_period, int slow_period)
  : fast_period_(fast_period), slow_period_(slow_period)
{
  if (fast_period_ >= slow_period_)
    throw std::invalid_argument("fast_period must be < slow_period");
}

// ─────────────────────────────────────────────────────────────
// sma：deque 子段均值
// 计算 d[start], d[start+1], ..., d[start+len-1] 的平均值
// ─────────────────────────────────────────────────────────────
double SmaCross::sma(const std::deque<double>& d, int start, int len) {
  double sum = 0.0;
  for (int i = start; i < start + len; ++i)
    sum += d[i];
  return sum / len;
}

// ─────────────────────────────────────────────────────────────
// on_bar：核心信号逻辑
//
// 历史队列的窗口布局（deque 长度固定为 slow_period_ + 1）：
//
//   index:  0  1  2 ... (n-f)  ... (n-1)  n      （n = slow_period_）
//           ├────────── prev 慢线窗口 ────────┤
//              ├────────── curr 慢线窗口 ────────┤
//                        ├─ prev 快线 ─┤
//                              ├─ curr 快线 ─┤
//
//   prev 慢线 = sma(hist, 0, n)          → 用 index [0, n-1]
//   curr 慢线 = sma(hist, 1, n)          → 用 index [1, n]
//   prev 快线 = sma(hist, n-f,   f)      → 用 index [n-f, n-1]
//   curr 快线 = sma(hist, n-f+1, f)      → 用 index [n-f+1, n]
//
// 金叉：prev_fast <= prev_slow && curr_fast > curr_slow
// 死叉：prev_fast >= prev_slow && curr_fast < curr_slow
// ─────────────────────────────────────────────────────────────
void SmaCross::on_bar(const MarketEvent& market, EventQueue& queue) {
  const std::string& sym = market.symbol;
  auto& hist = history_[sym];

  hist.push_back(market.bar.close);

  // 队列未满，还不能计算两个时间点的 SMA，直接返回
  if ((int)hist.size() < slow_period_ + 1)
    return;

  // 保持队列长度恒为 slow_period_ + 1，淘汰最旧数据
  if ((int)hist.size() > slow_period_ + 1)
    hist.pop_front();

  int n = slow_period_;
  int f = fast_period_;

  double slow_prev = sma(hist, 0,     n);
  double slow_curr = sma(hist, 1,     n);
  double fast_prev = sma(hist, n - f,     f);
  double fast_curr = sma(hist, n - f + 1, f);

  int& st = state_[sym];   // 当前仓位状态：+1 多头，0 空仓

  // 金叉：快线从下方穿越慢线，且当前不是多头
  if (fast_prev <= slow_prev && fast_curr > slow_curr && st != 1) {
    queue.push(SignalEvent{sym, Direction::LONG});
    st = 1;
  }
  // 死叉：快线从上方穿越慢线，且当前持有多头
  else if (fast_prev >= slow_prev && fast_curr < slow_curr && st == 1) {
    queue.push(SignalEvent{sym, Direction::SHORT});
    st = 0;
  }
}
