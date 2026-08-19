// POSIX 终端实现：Linux / macOS
// termios 原始模式 + ANSI 转义序列 + ioctl(TIOCGWINSZ) + poll/read 输入解析。
#if !defined(_WIN32)

#include "terminal.hpp"
#include "text.hpp" // utf8_decode / utf8_seq_len

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <string>

namespace fp {

struct Terminal::Impl {
  termios saved{}; // 原始终端属性（退出时恢复）
  bool raw_set = false;
  std::string pending_; // 尚未解析的输入字节缓冲
  int esc_state = 0;    // 0=普通, 1=收到 ESC, 2=CSI 序列中, 3=SS3 序列中
  std::string csi_params;
  int utf8_need = 0;     // 尚缺的 UTF-8 续字节数
  std::string utf8_part; // 已收到的 UTF-8 前缀字节
};

namespace {

// 信号处理：尽量恢复终端后退出，避免把调用方终端留在原始模式
Terminal *g_sig_target = nullptr;

void restore_and_exit(int sig) {
  if (g_sig_target)
    g_sig_target->restore();
  _exit(128 + sig);
}

ssize_t write_all(int fd, const char *data, size_t n) {
  size_t done = 0;
  while (done < n) {
    ssize_t r = ::write(fd, data + done, n - done);
    if (r < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    done += static_cast<size_t>(r);
  }
  return static_cast<ssize_t>(done);
}

// 解析一个输入字节；可能进入中间状态（返回 None 表示需更多字节）
KeyEvent parse_byte(Terminal::Impl *im, unsigned char b) {
  // 正在收 UTF-8 续字节
  if (im->utf8_need > 0) {
    im->utf8_part.push_back(static_cast<char>(b));
    --im->utf8_need;
    if (im->utf8_need == 0) {
      char32_t cp = 0;
      size_t consumed = 0;
      bool ok = utf8_decode(im->utf8_part, 0, cp, consumed);
      im->utf8_part.clear();
      if (!ok)
        return {KeyKind::Unknown};
      return {KeyKind::Char, cp};
    }
    return {KeyKind::None};
  }

  // ESC 之后
  if (im->esc_state == 1) {
    im->esc_state = 0;
    if (b == '[') {
      im->esc_state = 2;
      im->csi_params.clear();
      return {KeyKind::None};
    }
    if (b == 'O') {
      im->esc_state = 3;
      im->csi_params.clear();
      return {KeyKind::None};
    }
    return {KeyKind::Esc}; // ESC + 其他字符：当作单独 Esc（丢弃该字符）
  }

  // CSI 序列：ESC [ ... 终结字节(0x40-0x7E)
  if (im->esc_state == 2) {
    if (b >= 0x40 && b <= 0x7E) {
      char fin = static_cast<char>(b);
      std::string params = im->csi_params;
      im->esc_state = 0;
      im->csi_params.clear();
      int p1 = 1;
      size_t semi = params.find(';');
      std::string first = params.substr(0, semi);
      if (!first.empty())
        p1 = std::atoi(first.c_str());
      if (fin == '~') {
        switch (p1) {
        case 1:
        case 7:
          return {KeyKind::Home};
        case 4:
        case 8:
          return {KeyKind::End};
        case 5:
          return {KeyKind::PgUp};
        case 6:
          return {KeyKind::PgDn};
        default:
          return {KeyKind::Unknown};
        }
      }
      switch (fin) {
      case 'A':
        return {KeyKind::Up};
      case 'B':
        return {KeyKind::Down};
      case 'C':
        return {KeyKind::Right};
      case 'D':
        return {KeyKind::Left};
      case 'H':
        return {KeyKind::Home};
      case 'F':
        return {KeyKind::End};
      default:
        return {KeyKind::Unknown};
      }
    }
    im->csi_params.push_back(static_cast<char>(b));
    return {KeyKind::None};
  }

  // SS3 序列：ESC O x（部分终端的方向键）
  if (im->esc_state == 3) {
    im->esc_state = 0;
    if (b >= 0x40 && b <= 0x7E) {
      switch (static_cast<char>(b)) {
      case 'A':
        return {KeyKind::Up};
      case 'B':
        return {KeyKind::Down};
      case 'C':
        return {KeyKind::Right};
      case 'D':
        return {KeyKind::Left};
      case 'H':
        return {KeyKind::Home};
      case 'F':
        return {KeyKind::End};
      default:
        return {KeyKind::Unknown};
      }
    }
    return {KeyKind::Unknown};
  }

  // 普通字节
  if (b == 0x1B) {
    im->esc_state = 1;
    return {KeyKind::None};
  }
  if (b == '\r' || b == '\n')
    return {KeyKind::Enter};
  if (b == 0x7F || b == 0x08)
    return {KeyKind::Backspace};
  if (b == 0x03)
    return {KeyKind::CtrlC};
  if (b == ' ')
    return {KeyKind::Space};
  if (b < 0x20 || b == 0x7F)
    return {KeyKind::Unknown};
  if (b < 0x80)
    return {KeyKind::Char, static_cast<char32_t>(b)};

  // 多字节 UTF-8
  int need = utf8_seq_len(b);
  if (need <= 0)
    return {KeyKind::Unknown};
  im->utf8_need = need - 1;
  im->utf8_part.assign(1, static_cast<char>(b));
  return {KeyKind::None};
}

} // namespace

Terminal::Terminal() = default;

Terminal::~Terminal() { restore(); }

bool Terminal::init() {
  if (inited_)
    return true;
  impl_ = new Impl;
  tty_ = ::isatty(STDIN_FILENO) && ::isatty(STDOUT_FILENO);
  if (!tty_) {
    delete impl_;
    impl_ = nullptr;
    return false;
  }
  if (::tcgetattr(STDIN_FILENO, &impl_->saved) != 0) {
    delete impl_;
    impl_ = nullptr;
    return false;
  }
  termios raw = impl_->saved;
  // 输入：关闭行缓冲/回显/信号生成/软件流控/回车转换
  raw.c_iflag &=
      ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
  raw.c_oflag &= ~OPOST; // 输出不做 \n -> \r\n 翻译
  raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  raw.c_cflag &= ~(CSIZE | PARENB);
  raw.c_cflag |= CS8;
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  if (::tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
    delete impl_;
    impl_ = nullptr;
    return false;
  }
  impl_->raw_set = true;

  // 外部信号（SIGTERM 等）到来时先恢复终端再退出
  g_sig_target = this;
  struct sigaction sa{};
  sa.sa_handler = restore_and_exit;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  const int kSignals[] = {SIGINT,  SIGTERM, SIGHUP, SIGQUIT, SIGSEGV,
                          SIGABRT, SIGBUS,  SIGILL, SIGFPE};
  for (int s : kSignals)
    ::sigaction(s, &sa, nullptr);

  // 进入备用屏幕、隐藏光标、清屏
  const char esc[] = "\033[?1049h\033[?25l\033[2J\033[H";
  write_all(STDOUT_FILENO, esc, sizeof(esc) - 1);
  inited_ = true;
  return true;
}

void Terminal::restore() {
  if (!inited_)
    return;
  inited_ = false;
  if (impl_) {
    // 重置属性、显示光标、退出备用屏幕
    const char esc[] = "\033[0m\033[?25h\033[?1049l";
    write_all(STDOUT_FILENO, esc, sizeof(esc) - 1);
    if (impl_->raw_set) {
      ::tcsetattr(STDIN_FILENO, TCSANOW, &impl_->saved);
      impl_->raw_set = false;
    }
  }
  if (g_sig_target == this)
    g_sig_target = nullptr;
  delete impl_;
  impl_ = nullptr;
}

Size Terminal::size() const {
  Size s;
  struct winsize ws{};
  if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
      ws.ws_row > 0) {
    s.cols = ws.ws_col;
    s.rows = ws.ws_row;
    return s;
  }
  if (const char *c = std::getenv("COLUMNS")) {
    int v = std::atoi(c);
    if (v > 0)
      s.cols = v;
  }
  if (const char *r = std::getenv("LINES")) {
    int v = std::atoi(r);
    if (v > 0)
      s.rows = v;
  }
  if (s.cols <= 0)
    s.cols = 80;
  if (s.rows <= 0)
    s.rows = 24;
  return s;
}

void Terminal::clear_screen() { write("\033[2J\033[H"); }

void Terminal::write(const std::string &s) { write(s.data(), s.size()); }

void Terminal::write(const char *data, size_t n) {
  if (inited_)
    write_all(STDOUT_FILENO, data, n);
}

KeyEvent Terminal::read_key(int timeout_ms) {
  if (!impl_)
    return {KeyKind::None};
  const auto start = std::chrono::steady_clock::now();
  auto remaining_ms = [&]() -> int {
    auto el = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - start)
                  .count();
    return static_cast<int>(timeout_ms - el);
  };

  for (;;) {
    // 1) 从缓冲解析一个完整事件（每个字节都会被消费或进入中间状态）
    for (size_t i = 0; i < impl_->pending_.size(); ++i) {
      KeyEvent ev =
          parse_byte(impl_, static_cast<unsigned char>(impl_->pending_[i]));
      if (ev.kind != KeyKind::None) {
        impl_->pending_.erase(0, i + 1);
        return ev;
      }
    }

    // 2) 需要更多字节（缓冲为空，或处于未完成的 ESC/CSI/UTF-8 序列中）
    int wait_ms = remaining_ms();
    if (wait_ms <= 0) {
      if (impl_->esc_state != 0) {
        impl_->esc_state = 0;
        impl_->csi_params.clear();
        return {KeyKind::Esc};
      }
      if (impl_->utf8_need > 0) {
        impl_->utf8_need = 0;
        impl_->utf8_part.clear();
        return {KeyKind::Unknown};
      }
      return {KeyKind::None};
    }
    struct pollfd pfd{STDIN_FILENO, POLLIN, 0};
    int pr = ::poll(&pfd, 1, wait_ms);
    if (pr <= 0) {
      if (impl_->esc_state != 0) {
        impl_->esc_state = 0;
        impl_->csi_params.clear();
        return {KeyKind::Esc};
      }
      if (impl_->utf8_need > 0) {
        impl_->utf8_need = 0;
        impl_->utf8_part.clear();
        return {KeyKind::Unknown};
      }
      return {KeyKind::None};
    }
    char buf[256];
    ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0)
      return {KeyKind::None};
    impl_->pending_.append(buf, static_cast<size_t>(n));
  }
}

} // namespace fp

#endif // !defined(_WIN32)