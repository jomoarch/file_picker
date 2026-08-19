#ifndef FILE_PICKER_PICKER_HPP
#define FILE_PICKER_PICKER_HPP

#include "file_picker.hpp"
#include "listing.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fp {

// 交互选择器的核心状态机：与终端无关，可独立单元测试
// 负责：目录导航、光标/滚动、选中集合维护、隐藏文件开关
class PickerState {
public:
  explicit PickerState(const SelectionOptions &opts);

  // ---- 导航 ----
  void move_cursor(int delta); // +1 下移 / -1 上移（边界处无操作）
  void go_parent();            // 根目录时无操作
  bool enter_cursor();         // 进入光标所指目录；非目录/坏链接返回 false
  void toggle_hidden();        // 显示/隐藏隐藏文件

  // ---- 选择 ----
  void toggle_cursor_selection(); // Space；不可选条目静默忽略

  // ---- 查询 ----
  const SelectionOptions &options() const { return opts_; }
  const std::filesystem::path &current_dir() const { return cur_.dir; }
  const Listing &current_listing() const { return cur_; }
  const Listing &parent_listing() const { return parent_; }
  const Listing &right_listing() const { return right_; }
  const Entry *current_entry() const;
  size_t selection_count() const { return sel_order_.size(); }
  bool has_selection() const { return !sel_order_.empty(); }
  bool is_selected(const std::filesystem::path &p) const;
  bool selectable(const Entry &e) const; // 类型 + mode + allow_* 综合判定
  std::vector<std::filesystem::path>
  selected_paths() const; // 绝对路径，按选中顺序
  std::string mode_name() const;
  bool show_hidden() const { return opts_.show_hidden; }

  // 视图滚动约束（渲染器每帧调用）：保证光标与父栏高亮行在可视区内
  void clamp_scrolls(int entry_rows);

private:
  void rebuild_listings();
  void relist_cur_and_parent();
  void relist_right(); // 右栏：光标条目的子目录（带缓存）
  void remember_cursor();
  void restore_cursor(const std::filesystem::path &dir);
  void clear_selection();
  bool is_single_mode() const;
  bool mode_allows_dir() const;
  bool mode_allows_file() const;
  static std::string path_key(const std::filesystem::path &p);

  SelectionOptions opts_;
  Listing cur_, parent_, right_;
  std::unordered_set<std::string> sel_set_;
  std::vector<std::filesystem::path> sel_order_;
  std::unordered_map<std::string, size_t> sel_index_;
  std::unordered_map<std::string, size_t> cursor_mem_; // 每个目录记住的光标
  std::unordered_map<std::string, Listing>
      right_cache_; // 右栏缓存，避免光标移动时反复扫描
  static constexpr size_t kRightCacheMax = 128;
};

} // namespace fp

#endif // FILE_PICKER_PICKER_HPP