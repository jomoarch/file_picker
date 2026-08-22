#ifndef FILE_PICKER_PICKER_HPP
#define FILE_PICKER_PICKER_HPP

#include "file_picker.hpp"
#include "listing.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fp {

// 每个目录在两个隐藏视图下各自记住的光标偏移：
//   plain —— 不隐藏视图（隐藏隐藏文件）下的偏移
//   full  —— 隐藏视图（显示隐藏文件）下的偏移
// plain_set / full_set 表示该视图的偏移是否已确定（未确定 = 该视图还没来过，
// 首次进入时由 restore_cursor
// 从另一视图记住的文件映射得出，避免跳到第一个条目）。 切换隐藏 ->
// 不隐藏时若光标指向隐藏文件，会记 pending_full_restore；
// 期间若无有效导航（上/下移动、进入），切回隐藏视图时恢复到 full 位置
// （即原隐藏文件处），否则跟随当前光标文件
struct CursorMem {
  size_t plain = 0;
  size_t full = 0;
  bool plain_set = false;
  bool full_set = false;
  bool pending_full_restore = false;
};

// 交互选择器的核心状态机：与终端无关，可独立单元测试
// 负责：目录导航、光标/滚动、选中集合维护、隐藏文件开关
class PickerState {
public:
  explicit PickerState(const SelectionOptions &opts);

  // ---- 导航 ----
  void move_cursor(int delta); // +1 下移 / -1 上移（边界处无操作）
  void jump_cursor(size_t index); // 直接把光标跳到指定条目（鼠标点击定位，越界夹紧）
  void go_parent();            // 根目录时无操作
  bool enter_cursor();         // 进入光标所指目录；非目录/坏链接返回 false
  void toggle_hidden();        // 显示/隐藏隐藏文件

  // ---- 栏滚动（鼠标在左/右栏滚轮直接滚动对应栏）----
  void scroll_left(int delta);  // 滚动左栏；滚动后左栏不再强制高亮在视图内
  void scroll_right(int delta); // 滚动右栏（光标子目录预览）

  // ---- 缓存 ----
  // 刷新钩子：清空目录缓存与光标记忆并重建
  void refresh_cache();

  // ---- 选择 ----
  void toggle_cursor_selection(); // Space；不可选条目静默忽略

  // ---- 查询 ----
  const SelectionOptions &options() const { return opts_; }
  const std::filesystem::path &current_dir() const { return cur_.dir; }
  const Listing &current_listing() const { return cur_; }
  const Listing &parent_listing() const { return parent_; }
  const Listing &right_listing() const { return right_; }
  const Entry *current_entry() const;
  size_t selection_count() const { return sel_set_.size(); }
  bool has_selection() const { return !sel_set_.empty(); }
  bool is_selected(const std::string &key) const; // 用 Entry::key 预计算键查询
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
  void relist_right(); // 右栏：光标条目的子目录（走通用目录缓存）
  void remember_cursor();
  void restore_cursor(const std::filesystem::path &dir);
  void seed_ancestors(const std::filesystem::path &start);
  void clear_selection();
  bool is_single_mode() const;
  static std::string path_key(const std::filesystem::path &p);

  // 静态目录缓存：key = show_hidden 标记 + 规范化路径。
  // 假定目录不再变化，因此不做新鲜度校验；refresh_cache() 整体清空
  bool load_listing(const std::filesystem::path &dir, bool show_hidden,
                    Listing &out);
  void store_listing(const std::filesystem::path &dir, bool show_hidden,
                     const Listing &L);

  SelectionOptions opts_;
  Listing cur_, parent_, right_;
  bool parent_follow_ = true; // 左栏是否跟随高亮（当前目录）滚动；手动滚动后关闭，
                              // 重新装载父目录（导航/切换/刷新）时恢复
  std::unordered_set<std::string> sel_set_; // 选中集合（key=规范化 UTF-8 路径）
  // 注意：不保留选中顺序；selected_paths() 从 key 反解路径
  std::unordered_map<std::string, CursorMem>
      cursor_mem_; // 每目录两个视图的光标偏移
  std::unordered_map<std::string, Listing> listing_cache_; // 静态目录缓存
  static constexpr size_t kListingCacheMax = 512;
};

} // namespace fp

#endif // FILE_PICKER_PICKER_HPP