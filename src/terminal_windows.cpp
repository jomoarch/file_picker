// Windows 终端实现（Win32 控制台 API）。
// 说明：本文件在 Linux 上不参与编译（由 CMake 按平台选择源文件），
// 实现遵循 Win10+ 的虚拟终端处理方案，兼容 Windows Terminal 与新版 cmd.exe。
#if defined(_WIN32)

#include "terminal.hpp"

#include <windows.h>

#include <string>

namespace fp {

struct Terminal::Impl {
  HANDLE h_in = INVALID_HANDLE_VALUE;
  HANDLE h_out = INVALID_HANDLE_VALUE;
  DWORD saved_in_mode = 0;
  DWORD saved_out_mode = 0;
  UINT saved_in_cp = 0;
  UINT saved_out_cp = 0;
  wchar_t high_surrogate = 0; // 代理对处理（补充平面字符）
};

namespace {

bool write_all(HANDLE h, const char *data, size_t n) {
  while (n > 0) {
    DWORD written = 0;
    if (!::WriteFile(h, data, static_cast<DWORD>(n), &written, nullptr))
      return false;
    if (written == 0)
      return false;
    data += written;
    n -= written;
  }
  return true;
}

} // namespace

Terminal::Terminal() = default;

Terminal::~Terminal() { restore(); }

bool Terminal::init() {
  if (inited_)
    return true;
  impl_ = new Impl;
  impl_->h_in = ::GetStdHandle(STD_INPUT_HANDLE);
  impl_->h_out = ::GetStdHandle(STD_OUTPUT_HANDLE);
  if (impl_->h_in == INVALID_HANDLE_VALUE ||
      impl_->h_out == INVALID_HANDLE_VALUE || impl_->h_in == nullptr ||
      impl_->h_out == nullptr) {
    delete impl_;
    impl_ = nullptr;
    return false;
  }
  DWORD in_mode = 0, out_mode = 0;
  tty_ = ::GetConsoleMode(impl_->h_in, &in_mode) &&
         ::GetConsoleMode(impl_->h_out, &out_mode);
  if (!tty_) {
    delete impl_;
    impl_ = nullptr;
    return false;
  }
  impl_->saved_in_mode = in_mode;
  impl_->saved_out_mode = out_mode;
  impl_->saved_in_cp = ::GetConsoleCP();
  impl_->saved_out_cp = ::GetConsoleOutputCP();

  // 输入：关闭行缓冲/回显/进程信号处理/快速编辑模式
  DWORD new_in = in_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
                             ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE);
  new_in |= ENABLE_EXTENDED_FLAGS;
  ::SetConsoleMode(impl_->h_in, new_in);

  // 输出：启用虚拟终端处理（ANSI 转义），并切到 UTF-8 代码页
  ::SetConsoleMode(impl_->h_out, out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                                     ENABLE_PROCESSED_OUTPUT |
                                     ENABLE_WRAP_AT_EOL_OUTPUT);
  ::SetConsoleCP(CP_UTF8);
  ::SetConsoleOutputCP(CP_UTF8);

  const char esc[] = "\033[?1049h\033[?25l\033[2J\033[H";
  write_all(impl_->h_out, esc, sizeof(esc) - 1);
  inited_ = true;
  return true;
}

void Terminal::restore() {
  if (!inited_)
    return;
  inited_ = false;
  if (impl_) {
    const char esc[] = "\033[0m\033[?25h\033[?1049l";
    write_all(impl_->h_out, esc, sizeof(esc) - 1);
    ::SetConsoleMode(impl_->h_in, impl_->saved_in_mode);
    ::SetConsoleMode(impl_->h_out, impl_->saved_out_mode);
    ::SetConsoleCP(impl_->saved_in_cp);
    ::SetConsoleOutputCP(impl_->saved_out_cp);
  }
  delete impl_;
  impl_ = nullptr;
}

Size Terminal::size() const {
  Size s;
  if (impl_ && impl_->h_out != INVALID_HANDLE_VALUE) {
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (::GetConsoleScreenBufferInfo(impl_->h_out, &csbi)) {
      s.cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
      s.rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
      if (s.cols > 0 && s.rows > 0)
        return s;
    }
  }
  s.cols = 80;
  s.rows = 24;
  return s;
}

void Terminal::clear_screen() { write("\033[2J\033[H"); }

void Terminal::write(const std::string &s) { write(s.data(), s.size()); }

void Terminal::write(const char *data, size_t n) {
  if (inited_ && impl_)
    write_all(impl_->h_out, data, n);
}

KeyEvent Terminal::read_key(int timeout_ms) {
  if (!impl_)
    return {KeyKind::None};
  for (;;) {
    if (::WaitForSingleObject(impl_->h_in, static_cast<DWORD>(timeout_ms)) !=
        WAIT_OBJECT_0) {
      return {KeyKind::None};
    }
    INPUT_RECORD recs[16];
    DWORD n = 0;
    if (!::ReadConsoleInputW(impl_->h_in, recs, 16, &n))
      return {KeyKind::None};
    for (DWORD i = 0; i < n; ++i) {
      if (recs[i].EventType != KEY_EVENT)
        continue; // 忽略窗口/鼠标事件
      const KEY_EVENT_RECORD &ke = recs[i].Event.KeyEvent;
      if (!ke.bKeyDown)
        continue;
      const bool ctrl = (ke.dwControlKeyState &
                         (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;

      switch (ke.wVirtualKeyCode) {
      case VK_UP:
        return {KeyKind::Up};
      case VK_DOWN:
        return {KeyKind::Down};
      case VK_LEFT:
        return {KeyKind::Left};
      case VK_RIGHT:
        return {KeyKind::Right};
      case VK_HOME:
        return {KeyKind::Home};
      case VK_END:
        return {KeyKind::End};
      case VK_PRIOR:
        return {KeyKind::PgUp};
      case VK_NEXT:
        return {KeyKind::PgDn};
      case VK_RETURN:
        return {KeyKind::Enter};
      case VK_BACK:
        return {KeyKind::Backspace};
      case VK_SPACE:
        return {KeyKind::Space};
      default:
        break;
      }
      if (ctrl && ke.wVirtualKeyCode >= 'A' && ke.wVirtualKeyCode <= 'Z') {
        return ke.wVirtualKeyCode == 'C' ? KeyEvent{KeyKind::CtrlC}
                                         : KeyEvent{KeyKind::Unknown};
      }

      wchar_t uc = ke.uChar.UnicodeChar;
      if (uc == 0)
        continue; // 无字符的功能键
      if (uc <
          0x20) { // 控制字符（PROCESSED_INPUT 关闭时 Ctrl+C 以字符 3 到达）
        return uc == 3 ? KeyEvent{KeyKind::CtrlC} : KeyEvent{KeyKind::Unknown};
      }
      if (uc >= 0xD800 && uc <= 0xDBFF) {
        impl_->high_surrogate = uc;
        continue;
      }
      if (uc >= 0xDC00 && uc <= 0xDFFF) {
        if (impl_->high_surrogate != 0) {
          char32_t cp =
              0x10000 +
              ((static_cast<char32_t>(impl_->high_surrogate - 0xD800) << 10) +
               (uc - 0xDC00));
          impl_->high_surrogate = 0;
          return {KeyKind::Char, cp};
        }
        continue;
      }
      impl_->high_surrogate = 0;
      return {KeyKind::Char, static_cast<char32_t>(uc)};
    }
    // 有事件但不是按键（如窗口尺寸变化）：立即再查
    timeout_ms = 0;
  }
}

} // namespace fp

#endif // defined(_WIN32)
