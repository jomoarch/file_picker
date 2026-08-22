#include "picker.hpp"
#include "listing.hpp"
#include "text.hpp"

#include <algorithm>
#include <utility>

namespace fp {

namespace fs = std::filesystem;

namespace {

// 在按 entry_sorted_before 排序的 entries 中按 (is_dir, name) 二分定位条目
// 返回 {插入位置, 是否精确命中}；插入位置即“该条目若存在应处的下标”
// 对隐藏文件而言也就是“下一个不隐藏条目”的位置（没有则为 size，即上一个）
std::pair<size_t, bool> locate_entry(const std::vector<Entry> &entries,
                                     bool is_dir, const std::string &name) {
  Entry t;
  t.is_dir = is_dir;
  t.name = name;
  auto it =
      std::lower_bound(entries.begin(), entries.end(), t, entry_sorted_before);
  bool found = it != entries.end() && it->is_dir == is_dir && it->name == name;
  return {static_cast<size_t>(it - entries.begin()), found};
}

} // namespace

PickerState::PickerState(const SelectionOptions &opts) : opts_(opts) {
  std::error_code ec;
  if (opts_.initial_path.empty()) {
    opts_.initial_path = fs::current_path(ec);
    if (ec)
      opts_.initial_path = fs::path("/");
  }
  fs::path abs = fs::absolute(opts_.initial_path, ec);
  if (ec)
    abs = opts_.initial_path;
  abs = abs.lexically_normal();
  if (abs != abs.root_path() && abs.filename().empty())
    abs = abs.parent_path();
  opts_.initial_path = abs;

  cur_.dir = opts_.initial_path;
  rebuild_listings();
  // 初始路径各级祖先目录先处理入缓存（"没来过就初始化"的种子）：
  // 每个祖先当前视图的光标偏移指向通往初始路径的直接子级
  seed_ancestors(opts_.initial_path);

  for (const auto &p : opts_.initial_selection) {
    fs::path ap = fs::absolute(p, ec);
    if (ec)
      ap = p;
    ap = ap.lexically_normal();
    if (ap != ap.root_path() && ap.filename().empty())
      ap = ap.parent_path();

    Entry e;
    e.path = ap;
    e.key = path_key(ap);
    std::error_code e2, e3;
    e.is_link = fs::is_symlink(fs::symlink_status(ap, e2));
    if (e2)
      e.is_link = false;
    e.is_dir = fs::is_directory(ap, e3);
    if (e3)
      e.is_dir = false;
    if (!selectable(e))
      continue;

    std::string k = path_key(ap);
    if (sel_set_.count(k))
      continue;
    if (is_single_mode() && !sel_set_.empty())
      continue;
    sel_set_.insert(k);
  }
}

void PickerState::seed_ancestors(const std::filesystem::path &start) {
  fs::path child = start;
  while (true) {
    fs::path par = child.parent_path();
    if (par.empty() || par == child)
      break;
    Listing L;
    if (!load_listing(par, opts_.show_hidden, L)) {
      L = scan_directory(par, opts_.show_hidden);
      store_listing(par, opts_.show_hidden, L);
    }
    if (L.ok) {
      std::string child_key = path_key(child);
      for (size_t i = 0; i < L.entries.size(); ++i) {
        if (L.entries[i].key == child_key) {
          CursorMem &m = cursor_mem_[path_key(par)];
          if (opts_.show_hidden) {
            m.full = i;
            m.full_set = true;
          } else {
            m.plain = i;
            m.plain_set = true;
          }
          break;
        }
      }
    }
    child = par;
  }
}

void PickerState::refresh_cache() {
  // 清空目录缓存与光标记忆后重建；当前目录的光标位置尽量保留
  size_t keep_cursor = cur_.cursor;
  std::string dk = path_key(cur_.dir);
  listing_cache_.clear();
  cursor_mem_.clear();
  rebuild_listings();
  seed_ancestors(cur_.dir);
  CursorMem &m = cursor_mem_[dk];
  if (opts_.show_hidden) {
    m.full = std::min(keep_cursor,
                      cur_.entries.empty() ? 0 : cur_.entries.size() - 1);
    m.full_set = true;
    cur_.cursor = m.full;
  } else {
    m.plain = std::min(keep_cursor,
                       cur_.entries.empty() ? 0 : cur_.entries.size() - 1);
    m.plain_set = true;
    cur_.cursor = m.plain;
  }
  // rebuild 时光标还是 0，右栏按第一个条目构建；光标恢复后再重建右栏，
  // 保证右栏内容与光标所指目录一致（右栏标题按光标渲染，内容不能对不上）
  relist_right();
}
void PickerState::move_cursor(int delta) {
  if (cur_.entries.empty())
    return;
  size_t nc;
  if (delta < 0) {
    if (cur_.cursor == 0)
      return;
    nc = cur_.cursor - 1;
  } else {
    if (cur_.cursor + 1 >= cur_.entries.size())
      return;
    nc = cur_.cursor + 1;
  }
  CursorMem &mem = cursor_mem_[path_key(cur_.dir)];
  if (opts_.show_hidden) {
    mem.full = cur_.cursor;
    mem.full_set = true;
  } else {
    mem.plain = cur_.cursor;
    mem.plain_set = true;
  }
  mem.pending_full_restore = false; // 有效移动：取消“切回时恢复原隐藏位置”
  cur_.cursor = nc;
  relist_right();
}

void PickerState::jump_cursor(size_t index) {
  if (cur_.entries.empty())
    return;
  size_t clamped = std::min(index, cur_.entries.size() - 1);
  CursorMem &mem = cursor_mem_[path_key(cur_.dir)];
  if (opts_.show_hidden) {
    mem.full = clamped;
    mem.full_set = true;
  } else {
    mem.plain = clamped;
    mem.plain_set = true;
  }
  mem.pending_full_restore = false; // 有效移动：取消“切回时恢复原隐藏位置”
  cur_.cursor = clamped;
  relist_right();
}

void PickerState::go_parent() {
  fs::path par = cur_.dir.parent_path();
  if (par.empty() || par == cur_.dir)
    return;
  CursorMem &mem = cursor_mem_[path_key(cur_.dir)];
  if (opts_.show_hidden) {
    mem.full = cur_.cursor;
    mem.full_set = true;
  } else {
    mem.plain = cur_.cursor;
    mem.plain_set = true;
  }
  cur_.dir = par;
  relist_cur_and_parent();
  relist_right();
}

bool PickerState::enter_cursor() {
  const Entry *e = current_entry();
  if (!e)
    return false;
  bool is_dir = e->is_dir;
  if (e->is_link) {
    std::error_code ec;
    is_dir = std::filesystem::is_directory(e->path, ec);
    if (ec)
      is_dir = false;
  }
  if (!is_dir)
    return false;
  CursorMem &mem = cursor_mem_[path_key(cur_.dir)];
  if (opts_.show_hidden) {
    mem.full = cur_.cursor;
    mem.full_set = true;
  } else {
    mem.plain = cur_.cursor;
    mem.plain_set = true;
  }
  mem.pending_full_restore = false; // 有效的进入：取消“切回时恢复原隐藏位置”
  cur_.dir = e->path;
  relist_cur_and_parent();
  relist_right();
  return true;
}

void PickerState::toggle_hidden() {
  std::string dk = path_key(cur_.dir);
  // 切出前记录当前视图的光标位置
  {
    CursorMem &mem = cursor_mem_[dk];
    if (opts_.show_hidden) {
      mem.full = cur_.cursor;
      mem.full_set = true;
    } else {
      mem.plain = cur_.cursor;
      mem.plain_set = true;
    }
  }

  // 切换前光标指向的文件（切换后 cur_ 会被重新装载替换）
  const Entry *cur = current_entry();
  std::string cur_name = cur ? cur->name : std::string();
  bool cur_is_dir = cur ? cur->is_dir : false;

  opts_.show_hidden = !opts_.show_hidden;
  relist_cur_and_parent();

  CursorMem &mem = cursor_mem_[dk];
  if (opts_.show_hidden) {
    // —— 切到隐藏视图（显示隐藏文件）——
    if (mem.pending_full_restore) {
      // 期间无有效导航：恢复到原隐藏视图位置（可能指向隐藏文件）
      mem.pending_full_restore = false;
      mem.full_set = true;
      cur_.cursor = cur_.entries.empty()
                        ? 0
                        : std::min(mem.full, cur_.entries.size() - 1);
    } else {
      // 期间发生过有效导航：跟随当前光标文件
      auto [pos, found] = locate_entry(cur_.entries, cur_is_dir, cur_name);
      cur_.cursor = found ? pos
                          : (cur_.entries.empty()
                                 ? 0
                                 : std::min(pos, cur_.entries.size() - 1));
      mem.full = cur_.cursor;
      mem.full_set = true;
    }
  } else {
    // —— 切到不隐藏视图（隐藏隐藏文件）——
    mem.pending_full_restore = true; // 无导航时切回应回到原隐藏文件
    auto [pos, found] = locate_entry(cur_.entries, cur_is_dir, cur_name);
    if (found) {
      cur_.cursor = pos; // 当前文件仍可见：跟随它
    } else if (cur_.entries.empty()) {
      cur_.cursor = 0; // 空目录：维持现有处理
    } else if (pos < cur_.entries.size()) {
      cur_.cursor = pos; // 指向隐藏文件：光标移到下一个不隐藏的文件
    } else {
      cur_.cursor = cur_.entries.size() - 1; // 没有下一个则指向上一个
    }
    mem.plain = cur_.cursor;
    mem.plain_set = true;
  }
  // 光标确定后再重建右栏：relist 时 restore_cursor 用的是新视图的旧偏移，
  // 与切换逻辑算出的最终光标可能不同，右栏必须按最终光标构建
  relist_right();
  cur_.scroll = 0;
}

void PickerState::toggle_cursor_selection() {
  const Entry *e = current_entry();
  if (!e || !selectable(*e))
    return;

  std::string k = e->key;
  if (sel_set_.count(k)) {
    sel_set_.erase(k);
  } else {
    if (is_single_mode())
      clear_selection();
    sel_set_.insert(k);
  }
}

const Entry *PickerState::current_entry() const {
  if (cur_.entries.empty() || cur_.cursor >= cur_.entries.size())
    return nullptr;
  return &cur_.entries[cur_.cursor];
}

bool PickerState::is_selected(const std::string &key) const {
  return sel_set_.count(key) != 0;
}

bool PickerState::selectable(const Entry &e) const {
  if (opts_.filter)
    return opts_.filter(
        e.path); // 只由过滤器决定；filter 不允许的一律不可选（灰色）
  return true;   // 未设 filter：除链接外全部可选
}

std::vector<std::filesystem::path> PickerState::selected_paths() const {
  // 不保留选中顺序：从 sel_set_ 的 key 反解回规范化绝对路径
  std::vector<std::filesystem::path> out;
  out.reserve(sel_set_.size());
  for (const auto &k : sel_set_)
    out.push_back(path_from_utf8(k));
  return out;
}

std::string PickerState::mode_name() const {
  switch (opts_.mode) {
  case SelectionMode::SINGLE:
    return "SINGLE";
  case SelectionMode::MULTI:
    return "MULTI";
  }
  return "?";
}

void PickerState::clamp_scrolls(int entry_rows) {
  auto fix = [&](Listing &L, long target) {
    if (L.entries.size() <= static_cast<size_t>(entry_rows)) {
      L.scroll = 0;
      return;
    }
    long max_scroll = static_cast<long>(L.entries.size()) - entry_rows;
    if (target >= 0) {
      if (target < static_cast<long>(L.scroll))
        L.scroll = static_cast<size_t>(std::max(0L, target));
      else if (target >= static_cast<long>(L.scroll) + entry_rows)
        L.scroll = static_cast<size_t>(target - entry_rows + 1);
    }
    if (static_cast<long>(L.scroll) > max_scroll)
      L.scroll = static_cast<size_t>(max_scroll);
  };
  fix(cur_, static_cast<long>(cur_.cursor));
  if (parent_follow_) {
    fix(parent_, static_cast<long>(parent_.highlight));
  } else {
    // 左栏被手动滚动过：只夹紧合法范围，不强制高亮（当前目录）在视图内
    if (parent_.entries.size() <= static_cast<size_t>(entry_rows))
      parent_.scroll = 0;
    else
      parent_.scroll =
          std::min(parent_.scroll,
                   parent_.entries.size() - static_cast<size_t>(entry_rows));
  }
  // 右栏：只夹紧合法范围（可被滚轮直接滚动；光标移动换目录时 relist_right
  // 重置）
  if (right_.entries.size() <= static_cast<size_t>(entry_rows))
    right_.scroll = 0;
  else
    right_.scroll = std::min(
        right_.scroll, right_.entries.size() - static_cast<size_t>(entry_rows));
}

void PickerState::scroll_left(int delta) {
  if (parent_.entries.empty())
    return;
  long ns = static_cast<long>(parent_.scroll) + (delta < 0 ? -1 : 1);
  parent_.scroll = ns < 0 ? 0 : static_cast<size_t>(ns);
  parent_follow_ = false; // 手动滚动：不再强制左栏高亮在视图内
}

void PickerState::scroll_right(int delta) {
  if (right_.entries.empty())
    return;
  long ns = static_cast<long>(right_.scroll) + (delta < 0 ? -1 : 1);
  right_.scroll = ns < 0 ? 0 : static_cast<size_t>(ns);
}

void PickerState::rebuild_listings() {
  relist_cur_and_parent();
  relist_right();
}

void PickerState::relist_cur_and_parent() {
  if (!load_listing(cur_.dir, opts_.show_hidden, cur_)) {
    cur_ = scan_directory(cur_.dir, opts_.show_hidden);
    store_listing(cur_.dir, opts_.show_hidden, cur_);
  }
  restore_cursor(cur_.dir);
  cur_.scroll = 0;

  fs::path par = cur_.dir.parent_path();
  if (par.empty() || par == cur_.dir) {
    parent_.dir = cur_.dir;
    parent_.ok = true;
    parent_.error.clear();
    parent_.entries.clear();
    parent_.highlight = -1;
    parent_.scroll = 0;
  } else {
    if (!load_listing(par, opts_.show_hidden, parent_)) {
      parent_ = scan_directory(par, opts_.show_hidden);
      store_listing(par, opts_.show_hidden, parent_);
    }
    parent_.highlight = -1;
    std::string cur_key = path_key(cur_.dir);
    for (size_t i = 0; i < parent_.entries.size(); ++i) {
      if (parent_.entries[i].key == cur_key) {
        parent_.highlight = static_cast<int>(i);
        break;
      }
    }
    parent_.scroll = 0;
  }
  parent_follow_ = true; // 父目录重新装载：左栏恢复跟随高亮
}

void PickerState::relist_right() {
  const Entry *e = current_entry();
  if (!e || !e->is_dir) {
    right_.dir = e ? e->path : cur_.dir;
    right_.ok = true;
    right_.error.clear();
    right_.entries.clear();
    right_.scroll = 0;
    return;
  }
  fs::path old_dir = right_.dir;
  size_t old_scroll = right_.scroll; // 缓存装载会覆盖 scroll，需显式恢复
  if (!load_listing(e->path, opts_.show_hidden, right_)) {
    right_ = scan_directory(e->path, opts_.show_hidden);
    store_listing(e->path, opts_.show_hidden, right_);
  }
  // 右栏目录未变（如定位到同一行）时保留手动滚动；换目录则重置
  right_.scroll = (right_.dir == old_dir) ? old_scroll : 0;
}

bool PickerState::load_listing(const std::filesystem::path &dir,
                               bool show_hidden, Listing &out) {
  std::string k = (show_hidden ? "1|" : "0|") + path_key(dir);
  auto it = listing_cache_.find(k);
  if (it == listing_cache_.end())
    return false;
  out = it->second;
  return true;
}

void PickerState::store_listing(const std::filesystem::path &dir,
                                bool show_hidden, const Listing &L) {
  if (!L.ok)
    return; // 扫描失败的目录不缓存，下次仍重新扫描
  std::string k = (show_hidden ? "1|" : "0|") + path_key(dir);
  if (listing_cache_.size() >= kListingCacheMax)
    listing_cache_.clear();
  listing_cache_[k] = L;
}

void PickerState::remember_cursor() {
  CursorMem &mem = cursor_mem_[path_key(cur_.dir)];
  if (opts_.show_hidden) {
    mem.full = cur_.cursor;
    mem.full_set = true;
  } else {
    mem.plain = cur_.cursor;
    mem.plain_set = true;
  }
}

void PickerState::restore_cursor(const std::filesystem::path &dir) {
  bool cur_view =
      opts_.show_hidden; // true=隐藏视图(full) / false=不隐藏视图(plain)
  std::string dk = path_key(dir);
  auto it = cursor_mem_.find(dk);
  if (it == cursor_mem_.end()) {
    // 目录第一次来：初始化当前视图偏移为 0，另一视图偏移留待首次使用时映射
    CursorMem &m = cursor_mem_[dk];
    if (cur_view) {
      m.full = 0;
      m.full_set = true;
    } else {
      m.plain = 0;
      m.plain_set = true;
    }
    cur_.cursor = 0;
    return;
  }

  CursorMem &mem = it->second;
  size_t c;
  bool cur_set = cur_view ? mem.full_set : mem.plain_set;
  if (cur_set) {
    c = cur_view ? mem.full : mem.plain;
  } else {
    // 当前视图没来过、另一视图来过：把另一视图记住的文件映射到当前视图
    // （同切换逻辑：可见则跟随，隐藏则下一个/上一个不隐藏；空则 0）
    // 这样“没有隐藏文件的目录”切到另一视图时不会跳到第一个条目
    bool other_view = !cur_view;
    Listing other;
    if (!load_listing(dir, other_view, other)) {
      other = scan_directory(dir, other_view);
      store_listing(dir, other_view, other);
    }
    size_t other_off = other_view ? mem.full : mem.plain;
    if (other.ok && !other.entries.empty()) {
      const Entry &oe =
          other.entries[std::min(other_off, other.entries.size() - 1)];
      auto [pos, found] = locate_entry(cur_.entries, oe.is_dir, oe.name);
      if (found) {
        c = pos;
      } else if (cur_.entries.empty()) {
        c = 0;
      } else if (pos < cur_.entries.size()) {
        c = pos;
      } else {
        c = cur_.entries.size() - 1;
      }
    } else {
      c = 0;
    }
    if (cur_view) {
      mem.full = c;
      mem.full_set = true;
    } else {
      mem.plain = c;
      mem.plain_set = true;
    }
  }
  cur_.cursor = cur_.entries.empty() ? 0 : std::min(c, cur_.entries.size() - 1);
}

void PickerState::clear_selection() { sel_set_.clear(); }

bool PickerState::is_single_mode() const {
  return opts_.mode == SelectionMode::SINGLE;
}

std::string PickerState::path_key(const std::filesystem::path &p) {
  return path_key_string(p);
}

} // namespace fp