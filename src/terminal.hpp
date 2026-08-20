#ifndef FILE_PICKER_TERMINAL_HPP
#define FILE_PICKER_TERMINAL_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace fp {

struct Size {
  int cols = 80;
  int rows = 24;
};

enum class KeyKind {
  None, // 超时/无输入
  Up,
  Down,
  Left,
  Right,
  Home,
  End,
  PgUp,
  PgDn,
  Enter,
  Backspace,
  Space,
  Char, // 可打印字符，码点在 ch 中
  Esc,  // 单独按下 Esc
  CtrlC,
  WheelUp,   // 鼠标滚轮向上（启用鼠标追踪后）
  WheelDown, // 鼠标滚轮向下
  MouseLeft, // 鼠标左键按下，坐标在 x/y（1-based 终端行列）
  Unknown,   // 识别但未使用的键（Delete、Tab、功能键、鼠标点击等）
};

struct KeyEvent {
  KeyKind kind = KeyKind::None;
  char32_t ch = 0; // kind==Char 时的 Unicode 码点
  int x = 0;       // kind==MouseLeft 时的列（1-based）
  int y = 0;       // kind==MouseLeft 时的行（1-based）
};

// 平台无关的终端抽象。
//   Linux/macOS: termios 原始模式 + ANSI 转义序列 + ioctl(TIOCGWINSZ)
//   Windows:     Win32 控制台 API（启用 ENABLE_VIRTUAL_TERMINAL_PROCESSING
//                ReadConsoleInputW 读取按键）
// 进入时：禁用回显/行缓冲、隐藏光标、切换备用屏幕，并启用鼠标追踪
// （POSIX: \033[?1000h + \033[?1006h SGR 模式；Windows: ENABLE_MOUSE_INPUT）
// 滚轮事件以 KeyKind::WheelUp/WheelDown 上报；退出时完全恢复原状
class Terminal {
public:
  Terminal();
  ~Terminal(); // 未显式 restore 时自动恢复
  Terminal(const Terminal &) = delete;
  Terminal &operator=(const Terminal &) = delete;

  // 进入交互模式。失败（非 TTY）返回 false，不会改动终端状态
  bool init();
  // 恢复终端原始设置（可重复调用；析构时自动调用）
  void restore();

  bool initialized() const { return inited_; }
  bool is_tty() const { return tty_; }

  Size size() const;   // 查询当前终端尺寸（列 x 行）
  void clear_screen(); // 清屏并回到原点
  void write(const std::string &s);
  void write(const char *data, size_t n);

  // 读取一个按键，最多等待 timeout_ms 毫秒；无输入返回 {None}
  // 内部完成 UTF-8 解码与方向键等转义序列解析。
  KeyEvent read_key(int timeout_ms);

  // 平台私有数据（在 *_posix.cpp / *_windows.cpp 中定义），公开前向声明
  // 便于平台实现文件内的辅助函数访问；其定义只存在于平台源文件中
  struct Impl;
  Impl *impl_ = nullptr;

private:
  bool inited_ = false;
  bool tty_ = false;
};

} // namespace fp

#endif // FILE_PICKER_TERMINAL_HPP