#include "renderer.hpp"

#include "picker.hpp"
#include "text.hpp"

#include <algorithm>
#include <string>

namespace fp {

namespace {

const char *kSep = "|";

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
// 选中标识：绿色背景（黑字）；光标/当前目录行：粗体反显，选中时改用
// 更浅更亮的亮绿色背景（\033[102m）。
std::string entry_cell(const Listing &L, int row, const PickerState &st,
                       int width, bool is_middle, bool is_left) {
  size_t idx = L.scroll + static_cast<size_t>(row);
  if (idx >= L.entries.size())
    return std::string(static_cast<size_t>(width), ' ');
  const Entry &e = L.entries[idx];

  bool is_current_dir = is_left && e.path == st.current_dir();
  bool is_cursor = is_middle && idx == L.cursor;
  bool sel = st.is_selected(e.key); // Entry::key 为扫描时预计算的键
  bool can = st.selectable(e);

  // 前缀：仅左栏当前目录带 "> " 标记；后缀：链接 "@"（目录链接再加 "/"）、
  // 目录 "/"
  std::string prefix = is_current_dir ? "> " : "";
  std::string suffix = e.is_link ? "@" : "";
  if (e.is_dir)
    suffix += "/";
  int name_budget =
      width - string_display_width(prefix) - string_display_width(suffix);
  if (name_budget < 0)
    name_budget = 0;
  std::string plain = prefix + truncate_to_width(e.name, name_budget) + suffix;

  std::string style;
  if (is_current_dir || is_cursor) {
    style = sel ? "\033[1;30;106m"
                : "\033[1;7m"; // 光标/当前目录：反显（选中时亮绿色背景）
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

// 栏标题：蓝色字体 + 下划线（\033[34;4m），与下方目录条目区分
std::string title_cell(const std::string &plain, int width) {
  std::string p = truncate_to_width(plain, width);
  return padded("\033[34;4m" + p + "\033[0m", p, width);
}

// 三栏宽度分配：左右各约 20%，中栏取剩余；过窄时收缩左右栏
void compute_widths(int cols, int &side, int &mid) {
  side = cols * 2 / 10;
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
  if (ce) {
    std::string sfx = ce->is_link ? "@" : "";
    if (ce->is_dir)
      sfx += "/";
    t_right = ce->name + sfx;
  } else
    t_right = "-";

  return title_cell(t_left, side) + kSep + title_cell(t_mid, mid) + kSep +
         title_cell(t_right, side);
}

std::string build_status(const std::string &left, const std::string &right,
                         int cols) {
  --cols;
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
  int entry_rows = rows - 4;
  if (entry_rows < 1)
    entry_rows = 1;

  st.clamp_scrolls(entry_rows);

  std::string out;
  out.reserve(static_cast<size_t>(rows) * (static_cast<size_t>(cols) + 16));
  // 逐行用 CSI 显式定位到 (row,1) 后再写入，而不是“写满整行 + \r\n”
  // Windows conhost 在整行恰好写满终端宽度时会按 ENABLE_WRAP_AT_EOL_OUTPUT
  // 立即折行，随后的 \r\n 会被吞掉或再换一行，导致整帧错乱（“exact wrap”问题）
  // 显式定位与折行语义无关，Linux / Windows 行为完全一致
  auto line_at = [&out](int row, const std::string &line) {
    out += "\033[" + std::to_string(row) + ";1H";
    out += line;
  };

  line_at(1, build_header(st, cols));
  line_at(2, build_titles(st, side, mid));

  for (int i = 0; i < entry_rows; ++i) {
    line_at(3 + i,
            col_cell(st.parent_listing(), i, st, side, false, true) + kSep +
                col_cell(st.current_listing(), i, st, mid, true, false) + kSep +
                col_cell(st.right_listing(), i, st, side, false, false));
  }
  line_at(3 + entry_rows, build_status(status_left, status_right, cols));
  std::string cur_path = path_to_utf8(st.current_dir());
  std::string path_plain = truncate_to_width(cur_path, cols);
  line_at(4 + entry_rows,
          padded("\033[2m" + path_plain + "\033[0m", path_plain, cols));
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
  // 与 render() 相同：显式逐行定位，避免 conhost 对写满整行后的 \r\n 处理不一致
  auto line_at = [&out](int row, const std::string &line) {
    out += "\033[" + std::to_string(row) + ";1H";
    out += line;
  };

  std::string title = "HELP  -  H/q/Esc: close,  j/k/Up/Down: scroll";
  line_at(1, padded("\033[1;7m" + title + "\033[0m", title, cols));

  for (int i = 0; i < body_rows; ++i) {
    size_t li = static_cast<size_t>(scroll) + static_cast<size_t>(i);
    std::string line;
    if (li < lines.size())
      line = truncate_to_width(lines[li], cols);
    line_at(2 + i, padded(line, line, cols));
  }

  std::string footer;
  if (lines.empty()) {
    footer = "(empty help file)";
  } else {
    int last = std::min(scroll + body_rows, static_cast<int>(lines.size()));
    footer = "Line " + std::to_string(scroll + 1) + "-" + std::to_string(last) +
             " / " + std::to_string(lines.size()) + "   press H to close";
  }
  line_at(2 + body_rows, padded("\033[7m" + footer + "\033[0m", footer, cols));
  return out;
}

} // namespace fp