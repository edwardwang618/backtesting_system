#include "data_feed.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────
// 小工具：去掉字符串两端的空白（处理 Windows \r\n 换行）
// ─────────────────────────────────────────────────────────────
static std::string trim(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

// ─────────────────────────────────────────────────────────────
// 把一行按逗号拆成 token 数组
// ─────────────────────────────────────────────────────────────
static std::vector<std::string> split_csv(const std::string &line) {
  std::vector<std::string> tokens;
  std::istringstream ss(line);
  std::string tok;
  while (std::getline(ss, tok, ','))
    tokens.push_back(trim(tok));
  return tokens;
}

// ─────────────────────────────────────────────────────────────
// 解析 header 行，找到每列的位置
//
// 为什么不 hardcode 列号？
//   不同数据源列顺序可能不同（比如有的 CSV 把 volume 放第二列）
//   解析 header 之后，parse_row 可以按名字取值，顺序无关
// ─────────────────────────────────────────────────────────────
DataFeed::ColIndex DataFeed::parse_header(const std::string &header_line) {
  auto tokens = split_csv(header_line);
  ColIndex idx;
  for (int i = 0; i < (int)tokens.size(); ++i) {
    std::string col = tokens[i];
    // 转小写再比较，兼容 Date/DATE/date 等变体
    std::transform(col.begin(), col.end(), col.begin(), ::tolower);
    if (col == "date")
      idx.date = i;
    else if (col == "open")
      idx.open = i;
    else if (col == "high")
      idx.high = i;
    else if (col == "low")
      idx.low = i;
    else if (col == "close" || col == "adj close")
      idx.close = i;
    else if (col == "volume")
      idx.volume = i;
  }
  // 检查必要列是否都找到了
  if (idx.date < 0 || idx.open < 0 || idx.high < 0 || idx.low < 0 ||
      idx.close < 0 || idx.volume < 0) {
    throw std::runtime_error("CSV header missing required columns (need: "
                             "date,open,high,low,close,volume)");
  }
  return idx;
}

// ─────────────────────────────────────────────────────────────
// 解析一行数据 → Bar
// ─────────────────────────────────────────────────────────────
Bar DataFeed::parse_row(const std::string &line, const ColIndex &idx) {
  auto tokens = split_csv(line);
  // 防止列数不够（比如末尾有空行或格式错误的行）
  int max_idx =
      std::max({idx.date, idx.open, idx.high, idx.low, idx.close, idx.volume});
  if ((int)tokens.size() <= max_idx)
    throw std::runtime_error("Malformed CSV row: " + line);

  Bar bar;
  bar.date = tokens[idx.date];
  bar.open = std::stod(tokens[idx.open]);
  bar.high = std::stod(tokens[idx.high]);
  bar.low = std::stod(tokens[idx.low]);
  bar.close = std::stod(tokens[idx.close]);
  bar.volume = std::stoll(tokens[idx.volume]);
  return bar;
}

// ─────────────────────────────────────────────────────────────
// 构造函数：打开文件，解析 header，逐行读取 Bar
// ─────────────────────────────────────────────────────────────
DataFeed::DataFeed(std::string symbol, const std::string &csv_path)
    : symbol_(std::move(symbol)) {
  std::ifstream file(csv_path);
  if (!file.is_open())
    throw std::runtime_error("Cannot open CSV: " + csv_path);

  std::string line;

  // 第一行是 header
  if (!std::getline(file, line))
    throw std::runtime_error("CSV is empty: " + csv_path);
  ColIndex idx = parse_header(line);

  // 逐行解析数据
  int line_num = 1;
  while (std::getline(file, line)) {
    ++line_num;
    if (trim(line).empty())
      continue; // 跳过空行
    try {
      bars_.push_back(parse_row(line, idx));
    } catch (const std::exception &e) {
      // 单行解析失败不终止，打印警告后继续
      // 生产环境可以改为严格模式（直接 throw）
      fprintf(stderr, "Warning: skipping line %d: %s\n", line_num, e.what());
    }
  }

  if (bars_.empty())
    throw std::runtime_error("No valid data rows in: " + csv_path);
}

// ─────────────────────────────────────────────────────────────
// 迭代接口
// ─────────────────────────────────────────────────────────────
bool DataFeed::has_next() const { return index_ < bars_.size(); }

MarketEvent DataFeed::next() {
  // 返回值语义：Bar 被 move 进 MarketEvent
  // 注意：bars_ 里的 Bar 仍然有效（vector 不释放），这里是拷贝
  return MarketEvent{symbol_, bars_[index_++]};
}
