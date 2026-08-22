// =============================================================================
// 交互式文件选择器（跨平台：Linux/macOS 使用 termios + ANSI 转义序列，
// Windows 使用 Win32 控制台 API + 虚拟终端处理）。
//
// 界面文案全部为英文（含模式名、状态栏、提示信息）；帮助页文本保存在
// 独立的外部文件中（默认 file_picker_help.txt，内容可为任意语言，可用中文）。
//
// 用法示例：
//   SelectionOptions opts;
//   opts.initial_path = "/home/user";
//   opts.mode = SelectionMode::MULTI; // 多选
//   opts.filter = select_file_only;   // 只允许选择普通文件（目录/链接不可选）
//   SelectionResult result = pick_files(opts);
//   if (result.ok) {
//     for (const auto &p : result.paths) { /* ... */ }
//   }
//
// 按键说明：
//   q / Esc / Ctrl+C   取消选择，立即退出（ok=false，paths 为空）
//   c                  确认选择。若无选中项则提示 “Empty selection”，
//                      按任意其他键后继续；若有选中项则退出并返回选中路径列表
//   Space              切换当前光标条目的选中状态（不可选条目静默忽略；
//                      选中条目以绿色背景标识）
//   h / Left           返回上一级目录（根目录时无操作）
//   l / Right / Enter  进入当前光标所指目录（若为文件则静默忽略）
//   j / Down / k / Up  光标下/上移动一行
//   鼠标滚轮           光标上/下移动（帮助页中滚动）
//   Backspace          返回上一级目录（根目录时静默忽略）
//   .                  显示/隐藏以点开头的隐藏文件或文件夹
//   u                  刷新缓存（重新扫描当前目录及其路径上的所有目录）
//   H                  打开/关闭帮助页（内容来自外部帮助文件，支持 j/k 滚动）
//   其他按键           忽略
//
// 说明：Esc/Ctrl+C 取消是规格外的实用扩展；符号链接显示为“@”后缀，
// 不可被选中，但指向目录的链接可以进入。
// =============================================================================

#ifndef FILE_PICKER_HPP
#define FILE_PICKER_HPP

#include <filesystem>
#include <functional>
#include <system_error>
#include <vector>

// 选择模式：只区分单/多选；可选条目的类型由 filter（或 allow_*）决定
enum class SelectionMode {
  SINGLE, // 单选：同一时刻最多保留一个选中项
  MULTI,  // 多选：不限制数量
};

// 常用选择过滤器模板（符号链接一律不可选）：

// 只选普通文件（目录不可选）
inline bool select_file_only(const std::filesystem::path &p) {
  std::error_code ec;
  std::filesystem::file_status ss = std::filesystem::symlink_status(p, ec);
  if (ec || std::filesystem::is_symlink(ss))
    return false;
  return std::filesystem::is_regular_file(ss);
}

// 只选目录（文件不可选）
inline bool select_directory_only(const std::filesystem::path &p) {
  std::error_code ec;
  std::filesystem::file_status ss = std::filesystem::symlink_status(p, ec);
  if (ec || std::filesystem::is_symlink(ss))
    return false;
  return std::filesystem::is_directory(ss);
}

struct SelectionOptions {
  std::filesystem::path initial_path; // 初始目录；为空时使用当前工作目录

  // 选择过滤器：决定某个条目是否可选中（true=可选）。
  // 未设置时除符号链接外全部可选；设置后只由过滤器决定，
  // 过滤器不允许的条目一律显示为灰色且不可选中。
  // 符号链接无论过滤器如何都不可选中；导航（进入目录）不受过滤器限制。
  std::function<bool(const std::filesystem::path &)> filter;

  // 单选/多选（单选模式同时最多保留一个选中项）
  SelectionMode mode = SelectionMode::MULTI;

  bool show_hidden = false;

  // 初始选中路径。单选模式只保留第一个；无效/不可选的路径被忽略
  std::vector<std::filesystem::path> initial_selection;

  // 帮助页文本文件（H 键显示）。为空时按默认规则查找 file_picker_help.txt：
  // 可执行文件所在目录 → 其 docs/ 子目录 → 当前目录 → 当前目录/docs。
  // 找不到时显示内置英文按键说明。
  std::filesystem::path help_file;
};

struct SelectionResult {
  bool ok = false;
  std::vector<std::filesystem::path> paths;
};

SelectionResult pick_files(const SelectionOptions &opts = {});

#endif // FILE_PICKER_HPP