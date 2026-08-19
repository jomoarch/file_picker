#include "renderer.hpp"

#include "picker.hpp"
#include "text.hpp"

#include <algorithm>
#include <string>

namespace fp {

namespace {

const char *kSep = "\xE2\x94\x82"; // "│" U+2502，显示宽度 1

// 在 styled 文本后补空格到指定宽度（宽度按 plain 的显示宽度计算）
std::string padded(const std::string &styled, const std::string &plain,
                   int width) {
  int pad = width - string_display_width(plain);
  std::string out = styled;
  if (pad > 0)
    out.append(static_cast<size_t>(pad), ' ');
  return out;
}

// 条目单元格。is_middle：中栏（光标高亮）；is_left：左栏（当前目录高亮）
// 选中标识：绿色背景（黑字）；光标/当前目录行：粗体反显，选中时绿色加粗。
std::string entry_cell(const Listing &L, int row, const PickerState &st,
                       int width, bool is_middle, bool is_left) {
  size_t idx = L.scroll + static_cast<size_t>(row);
  if (idx >= L.entries.size())
    return std::string(static_cast<size_t>(width), ' ');
  const Entry &e = L.entries[idx];

  bool is_current_dir = is_left && e.path == st.current_dir();
  bool is_cursor = is_middle && idx == L.cursor;
  bool sel = st.is_selected(e.path);
  bool can = st.selectable(e);

  // 前缀：仅左栏当前目录带 "▶ " 标记；后缀：目录 "/"、符号链接 "@"
  std::string prefix = is_current_dir ? "\xE2\x96\xB6 " : "";
  std::string suffix = e.is_dir ? "/" : (e.is_link ? "@" : "");
  int name_budget =
      width - static_cast<int>(prefix.size()) - static_cast<int>(suffix.size());
  if (name_budget < 0)
    name_budget = 0;
  std::string plain = prefix + truncate_to_width(e.name, name_budget) + suffix;

  std::string style;
  if (is_current_dir || is_cursor) {
    style = sel ? "\033[1;30;42m"
                : "\033[1;7m"; // 光标/当前目录：反显（选中时绿色加粗）
  } else if (sel) {
    style = "\033[30;42m"; // 已选中：黑字绿底
  } else if (!can) {
    style = "\033[2m"; // 不可选（链接、类型不符）：暗色
  }
  return padded(style + plain + "\033[0m", plain, width);
}

// 栏内提示（错误/空目录），显示在栏的第一行
std::string message_cell(const std::string &msg, int width, bool error) {
  std::string m = truncate_to_width(msg, width);
  std::string style = error ? "\033[31m" : "\033[2m"; // 错误=红色，空=暗色
  return padded(style + m + "\033[0m", m, width);
}

// 一栏的一行单元格（自动滚动已由 clamp_scrolls 保证）
std::string col_cell(const Listing &L, int row, const PickerState &st,
                     int width, bool is_middle, bool is_left) {
  if (row == 0) {
    if (!L.ok)
      return message_cell("(Error) " + L.error, width, true);
    if (L.entries.empty())
      return message_cell("(Empty)", width, false);
  }
  return entry_cell(L, row, st, width, is_middle, is_left);
}

std::string title_cell(const std::string &plain, int width) {
  std::string p = truncate_to_width(plain, width);
  return padded("\033[1m" + p + "\033[0m", p, width);
}

// 三栏宽度分配：左右各约 30%，中栏取剩余；过窄时收缩左右栏
void compute_widths(int cols, int &side, int &mid) {
  side = cols * 3 / 10;
  if (side < 6)
    side = 6;
  mid = cols - 2 * side - 2;
  if (mid < 14) {
    side = (cols - 2 - 14) / 2;
    if (side < 2)
      side = 2;
    mid = cols - 2 * side - 2;
    if (mid < 1)
      mid = 1;
    int total = 2 * side + 2 + mid;
    if (total > cols) {
      side = std::max(1, (cols - 2 - mid) / 2);
      total = 2 * side + 2 + mid;
    }
    if (total > cols)
      mid = std::max(1, cols - 2 * side - 2);
  }
}

// 顶栏：模式 + 当前中栏目录路径（保留尾部）+ 右侧选中数/隐藏状态
std::string build_header(const PickerState &st, int cols) {
  std::string right_part = "Sel:" + std::to_string(st.selection_count()) +
                           " Hide:" + (st.show_hidden() ? "on" : "off");
  right_part = truncate_to_width(right_part, std::max(cols / 2, 4));
  int right_w = string_display_width(right_part);
  int header_budget = cols - right_w - 1;
  if (header_budget < 4)
    header_budget = 4;

  std::string mode_tag = "[" + st.mode_name() + "]";
  std::string path_s = path_to_utf8(st.current_dir());
  int path_budget = header_budget - string_display_width(mode_tag) - 1;
  std::string header_plain =
      mode_tag + " " +
      (path_budget > 0 ? truncate_keep_tail(path_s, path_budget) : "");
  header_plain = truncate_to_width(header_plain, header_budget);

  std::string header =
      padded("\033[1m" + header_plain + "\033[0m", header_plain, header_budget);
  header += " " + right_part;
  return header;
}

std::string build_titles(const PickerState &st, int side, int mid) {
  const Listing &pl = st.parent_listing();
  std::string t_left;
  if (pl.dir == st.current_dir())
    t_left = "(No parent)";
  else if (pl.ok && !pl.entries.empty())
    t_left = path_to_utf8(pl.dir.filename());
  else if (pl.ok)
    t_left = "(Empty)";
  else
    t_left = "(Error)";

  const Listing &cl = st.current_listing();
  std::string t_mid = path_to_utf8(cl.dir.filename());
  if (t_mid.empty())
    t_mid = path_to_utf8(cl.dir);

  const Entry *ce = st.current_entry();
  std::string t_right;
  if (ce)
    t_right = ce->name + (ce->is_dir ? "/" : (ce->is_link ? "@" : ""));
  else
    t_right = "—";

  return title_cell(t_left, side) + kSep + title_cell(t_mid, mid) + kSep +
         title_cell(t_right, side);
}

std::string build_status(const std::string &left, const std::string &right,
                         int cols) {
  std::string r = truncate_to_width(right, std::max(cols / 3, 4));
  int left_budget = cols - string_display_width(r);
  if (left_budget < 2)
    left_budget = 2;
  std::string plain = truncate_to_width(left, left_budget);
  int w = string_display_width(plain);
  std::string out = "\033[7m" + plain + "\033[0m";
  out.append(static_cast<size_t>(left_budget - w), ' ');
  out += " " + r;
  return out;
}

} // namespace

std::string Renderer::render(PickerState &st, Size size,
                             const std::string &status_left,
                             const std::string &status_right) {
  int cols = std::max(size.cols, 8);
  int rows = std::max(size.rows, 5);
  int side = 0, mid = 0;
  compute_widths(cols, side, mid);
  int entry_rows = rows - 3;
  if (entry_rows < 1)
    entry_rows = 1;

  st.clamp_scrolls(entry_rows);

  std::string out;
  out.reserve(static_cast<size_t>(rows) * (static_cast<size_t>(cols) + 16));
  out += "\033[H";

  out += build_header(st, cols);
  out += "\r\n";
  out += build_titles(st, side, mid);
  out += "\r\n";

  for (int i = 0; i < entry_rows; ++i) {
    out += col_cell(st.parent_listing(), i, st, side, false, true);
    out += kSep;
    out += col_cell(st.current_listing(), i, st, mid, true, false);
    out += kSep;
    out += col_cell(st.right_listing(), i, st, side, false, false);
    if (i + 1 < entry_rows)
      out += "\r\n";
  }
  out += "\r\n";
  out += build_status(status_left, status_right, cols);
  return out;
}

std::string Renderer::render_help(const std::vector<std::string> &lines,
                                  int &scroll, Size size) {
  int cols = std::max(size.cols, 8);
  int rows = std::max(size.rows, 5);
  int body_rows = rows - 2;
  int max_scroll = std::max(0, static_cast<int>(lines.size()) - body_rows);
  if (scroll < 0)
    scroll = 0;
  if (scroll > max_scroll)
    scroll = max_scroll;

  std::string out;
  out.reserve(static_cast<size_t>(rows) * (static_cast<size_t>(cols) + 16));
  out += "\033[H";

  std::string title = "HELP  -  H/q/Esc: close,  j/k/Up/Down: scroll";
  out += padded("\033[1;7m" + title + "\033[0m", title, cols);
  out += "\r\n";

  for (int i = 0; i < body_rows; ++i) {
    size_t li = static_cast<size_t>(scroll) + static_cast<size_t>(i);
    std::string line;
    if (li < lines.size())
      line = truncate_to_width(lines[li], cols);
    out += padded(line, line, cols);
    if (i + 1 < body_rows)
      out += "\r\n";
  }

  std::string footer;
  if (lines.empty()) {
    footer = "(empty help file)";
  } else {
    int last = std::min(scroll + body_rows, static_cast<int>(lines.size()));
    footer = "Line " + std::to_string(scroll + 1) + "-" + std::to_string(last) +
             " / " + std::to_string(lines.size()) + "   press H to close";
  }
  out += "\r\n";
  out += padded("\033[7m" + footer + "\033[0m", footer, cols);
  return out;
}

} // namespace fp