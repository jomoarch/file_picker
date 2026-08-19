#include "picker.hpp"
#include "text.hpp"

#include <algorithm>

namespace fp {

namespace fs = std::filesystem;

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
  opts_.initial_path = abs.lexically_normal();

  cur_.dir = opts_.initial_path;
  rebuild_listings();

  for (const auto &p : opts_.initial_selection) {
    fs::path ap = fs::absolute(p, ec);
    if (ec)
      ap = p;
    ap = ap.lexically_normal();

    Entry e;
    e.path = ap;
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
    if (is_single_mode() && !sel_order_.empty())
      continue;
    sel_set_.insert(k);
    sel_index_[k] = sel_order_.size();
    sel_order_.push_back(ap);
  }
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
  cursor_mem_[path_key(cur_.dir)] = cur_.cursor;
  cur_.cursor = nc;
  relist_right();
}

void PickerState::go_parent() {
  fs::path par = cur_.dir.parent_path();
  if (par.empty() || par == cur_.dir)
    return;
  cursor_mem_[path_key(cur_.dir)] = cur_.cursor;
  cur_.dir = par;
  relist_cur_and_parent();
  relist_right();
}

bool PickerState::enter_cursor() {
  const Entry *e = current_entry();
  if (!e || !e->is_dir)
    return false;
  cursor_mem_[path_key(cur_.dir)] = cur_.cursor;
  cur_.dir = e->path;
  relist_cur_and_parent();
  relist_right();
  return true;
}

void PickerState::toggle_hidden() {
  opts_.show_hidden = !opts_.show_hidden;
  cursor_mem_[path_key(cur_.dir)] = cur_.cursor;
  right_cache_.clear();
  relist_cur_and_parent();
  relist_right();
}

void PickerState::toggle_cursor_selection() {
  const Entry *e = current_entry();
  if (!e || !selectable(*e))
    return;

  std::string k = path_key(e->path);
  auto it = sel_set_.find(k);
  if (it != sel_set_.end()) {
    sel_set_.erase(it);
    size_t idx = sel_index_[k];
    sel_order_.erase(sel_order_.begin() + static_cast<long>(idx));
    sel_index_.erase(k);
    for (size_t i = idx; i < sel_order_.size(); ++i) {
      sel_index_[path_key(sel_order_[i])] = i;
    }
  } else {
    if (is_single_mode())
      clear_selection();
    sel_set_.insert(k);
    sel_index_[k] = sel_order_.size();
    sel_order_.push_back(fs::absolute(e->path).lexically_normal());
  }
}

const Entry *PickerState::current_entry() const {
  if (cur_.entries.empty() || cur_.cursor >= cur_.entries.size())
    return nullptr;
  return &cur_.entries[cur_.cursor];
}

bool PickerState::is_selected(const std::filesystem::path &p) const {
  return sel_set_.count(path_key(p)) != 0;
}

bool PickerState::selectable(const Entry &e) const {
  if (e.is_link)
    return false;
  if (e.is_dir)
    return mode_allows_dir() && opts_.allow_directories;
  return mode_allows_file() && opts_.allow_files;
}

std::vector<std::filesystem::path> PickerState::selected_paths() const {
  return sel_order_;
}

std::string PickerState::mode_name() const {
  switch (opts_.mode) {
  case SelectionMode::SINGLE_FILE:
    return "SINGLE_FILE";
  case SelectionMode::MULTI_FILE:
    return "MULTI_FILE";
  case SelectionMode::SINGLE_DIRECTORY:
    return "SINGLE_DIRECTORY";
  case SelectionMode::MULTI_DIRECTORY:
    return "MULTI_DIRECTORY";
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
  fix(parent_, static_cast<long>(parent_.highlight));
  right_.scroll = 0;
}

void PickerState::rebuild_listings() {
  relist_cur_and_parent();
  relist_right();
}

void PickerState::relist_cur_and_parent() {
  cur_ = scan_directory(cur_.dir, opts_.show_hidden);
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
    parent_ = scan_directory(par, opts_.show_hidden);
    parent_.highlight = -1;
    for (size_t i = 0; i < parent_.entries.size(); ++i) {
      if (parent_.entries[i].path == cur_.dir) {
        parent_.highlight = static_cast<int>(i);
        break;
      }
    }
    parent_.scroll = 0;
  }
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
  std::string ck = (opts_.show_hidden ? "1|" : "0|") + path_key(e->path);
  auto it = right_cache_.find(ck);
  if (it != right_cache_.end()) {
    right_ = it->second;
    right_.scroll = 0;
    return;
  }
  right_ = scan_directory(e->path, opts_.show_hidden);
  right_.scroll = 0;
  if (right_cache_.size() >= kRightCacheMax)
    right_cache_.clear();
  right_cache_[ck] = right_;
}

void PickerState::remember_cursor() {
  cursor_mem_[path_key(cur_.dir)] = cur_.cursor;
}

void PickerState::restore_cursor(const std::filesystem::path &dir) {
  auto it = cursor_mem_.find(path_key(dir));
  size_t c = (it == cursor_mem_.end()) ? 0 : it->second;
  cur_.cursor = cur_.entries.empty() ? 0 : std::min(c, cur_.entries.size() - 1);
}

void PickerState::clear_selection() {
  sel_set_.clear();
  sel_order_.clear();
  sel_index_.clear();
}

bool PickerState::is_single_mode() const {
  return opts_.mode == SelectionMode::SINGLE_FILE ||
         opts_.mode == SelectionMode::SINGLE_DIRECTORY;
}

bool PickerState::mode_allows_dir() const {
  return opts_.mode == SelectionMode::SINGLE_DIRECTORY ||
         opts_.mode == SelectionMode::MULTI_DIRECTORY;
}

bool PickerState::mode_allows_file() const {
  return opts_.mode == SelectionMode::SINGLE_FILE ||
         opts_.mode == SelectionMode::MULTI_FILE;
}

std::string PickerState::path_key(const std::filesystem::path &p) {
  return path_to_utf8(p.lexically_normal());
}

} // namespace fp