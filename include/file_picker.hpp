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
//   opts.mode = SelectionMode::MULTI_FILE;
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
//   h / ←              返回上一级目录（根目录时无操作）
//   l / → / Enter      进入当前光标所指目录（若为文件则静默忽略）
//   j / ↓ / k / ↑      光标下/上移动一行
//   Backspace          返回上一级目录（根目录时静默忽略）
//   .                  显示/隐藏以点开头的隐藏文件或文件夹
//   H                  打开/关闭帮助页（内容来自外部帮助文件，支持 j/k 滚动）
//   其他按键           忽略
//
// 说明：Esc/Ctrl+C 取消是规格外的实用扩展；符号链接显示为“@”后缀，
// 不可被选中，但指向目录的链接可以进入。
// =============================================================================

#ifndef FILE_PICKER_HPP
#define FILE_PICKER_HPP

#include <filesystem>
#include <vector>

enum class SelectionMode {
  SINGLE_FILE,      // 单选文件
  MULTI_FILE,       // 多选文件
  SINGLE_DIRECTORY, // 单选目录
  MULTI_DIRECTORY,  // 多选目录
};

struct SelectionOptions {
  std::filesystem::path initial_path; // 初始目录；为空时使用当前工作目录

  // mode 决定“可选条目的类型”与“单/多选”：
  //   *_FILE 模式只能选文件，*_DIRECTORY 模式只能选目录
  //   SINGLE_* 模式同时最多保留一个选中项，MULTI_* 不限制数量
  SelectionMode mode = SelectionMode::MULTI_FILE;

  // allow_* 是比 mode 更细粒度的开关：条目类型必须先通过 mode
  // 再通过对应的 allow_* 才会可选中（即 allow_* 只会进一步限制）
  // 例如 mode=MULTI_FILE 且 allow_directories=true 时，目录仍不可选中
  // （因为 mode 不允许目录）；而 mode=MULTI_DIRECTORY 且
  // allow_directories=false 时，目录被 allow_*
  // 禁止。导航（进入目录）不受这些开关限制
  bool allow_files = true;
  bool allow_directories = true;
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