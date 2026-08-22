#include "file_picker.hpp"

#include "helpfile.hpp"
#include "picker.hpp"
#include "renderer.hpp"
#include "terminal.hpp"
#include "text.hpp"

#include <cstdio>
#include <string>
#include <vector>

// 公共 API 与项目现有风格一致，位于全局作用域（见 file_picker.hpp）。
// 内部实现（picker/renderer/terminal 等）位于 namespace fp。
using namespace fp; // NOLINT: 本文件使用 fp 内部类型实现公共入口

namespace fs = std::filesystem;

SelectionResult pick_files(const SelectionOptions &user_opts) {
  SelectionOptions opts = user_opts;

  // ---- 初始路径校验（不进入交互模式即返回错误提示）----
  fs::path init =
      opts.initial_path.empty() ? fs::current_path() : opts.initial_path;
  std::error_code ec;
  fs::file_status st = fs::status(init, ec);
  if (ec || !fs::is_directory(st)) {
    std::fprintf(stderr,
                 "file_picker: invalid initial path (not an existing "
                 "directory): %s (%s)\n",
                 path_to_utf8(init).c_str(),
                 ec ? ec.message().c_str() : "not a directory");
    return {false, {}};
  }
  // filter 为空时全部（除链接）可选，无需额外校验
  Terminal term;
  if (!term.init()) {
    std::fprintf(stderr, "file_picker: cannot enter interactive mode "
                         "(stdin/stdout is not a terminal)\n");
    return {false, {}};
  }

  SelectionResult result;
  try {
    PickerState state(opts);
    Renderer renderer;
    bool msg_pending = false; // “Empty selection” 提示是否在显示中

    // ---- 帮助页状态（H 键开关）----
    bool help_open = false;
    int help_scroll = 0;
    std::vector<std::string> help_lines;
    auto open_help = [&]() {
      help_open = true;
      help_scroll = 0;
      help_lines.clear();
      fs::path hp = find_help_file(opts.help_file);
      if (hp.empty()) {
        // 帮助文件缺失时的内置英文备用说明
        help_lines = {
            "(help file not found: file_picker_help.txt)",
            "",
            "Keys:",
            "  q / Esc / Ctrl+C   quit / cancel",
            "  c                  confirm selection",
            "  Space              toggle selection",
            "  h / Left / Backspace   parent directory",
            "  l / Right / Enter      open directory",
            "  j / Down, k / Up       move cursor",
            "  .                  toggle hidden files",
            "  u                  refresh cache (rescan current dir & its "
            "path)",
            "  H                  toggle help",
        };
        return;
      }
      std::string err;
      if (!load_help_lines(hp, help_lines, err)) {
        help_lines = {"(failed to load help file: " + err + ")"};
      }
    };

    auto build_status = [&](bool pending, std::string &left,
                            std::string &right) {
      left =
          pending
              ? "Empty selection - press any key to continue"
              : "[q]Quit [c]Confirm [H]Help [Space]Select [h/Left]Up "
                "[l/Right/Enter]Open [j/k/Up/Down]Move [.]Hidden [u]Refresh ";
      right = "Mode:" + state.mode_name() +
              " Sel:" + std::to_string(state.selection_count()) +
              " Hide:" + (state.show_hidden() ? "on" : "off");
    };

    std::string status_left, status_right;
    build_status(false, status_left, status_right);

    auto render_now = [&](Size sz) {
      if (help_open) {
        term.write(renderer.render_help(help_lines, help_scroll, sz));
      } else {
        term.write(renderer.render(state, sz, status_left, status_right));
      }
    };

    Size last_size = term.size();
    render_now(last_size);

    bool done = false;
    while (!done) {
      KeyEvent ev = term.read_key(100);
      if (ev.kind == KeyKind::None) {
        // 空闲时轮询终端尺寸变化（窗口缩放自适应）
        Size sz = term.size();
        if (sz.cols != last_size.cols || sz.rows != last_size.rows) {
          last_size = sz;
          term.clear_screen();
          render_now(sz);
        }
        continue;
      }

      // ---- 帮助页模式：只响应帮助相关按键 ----
      if (help_open) {
        if (ev.kind == KeyKind::Char && (ev.ch == 'H' || ev.ch == 'q')) {
          help_open = false; // H 或 q 关闭帮助页
        } else if (ev.kind == KeyKind::Esc) {
          help_open = false;
        } else if (ev.kind == KeyKind::CtrlC) {
          done = true;
          result.ok = false;
          break;
        } else if (ev.kind == KeyKind::Char && ev.ch == 'j') {
          ++help_scroll;
        } else if (ev.kind == KeyKind::Char && ev.ch == 'k') {
          --help_scroll;
        } else if (ev.kind == KeyKind::Down) {
          ++help_scroll;
        } else if (ev.kind == KeyKind::Up) {
          --help_scroll;
        } else if (ev.kind == KeyKind::WheelDown) {
          ++help_scroll;
        } else if (ev.kind == KeyKind::WheelUp) {
          --help_scroll;
        }
        last_size = term.size();
        render_now(last_size);
        continue;
      }

      // “Empty selection” 提示：按任意非 c 键清除并继续正常操作
      const bool is_confirm = (ev.kind == KeyKind::Char && ev.ch == 'c');
      if (msg_pending && !is_confirm)
        msg_pending = false;

      switch (ev.kind) {
      case KeyKind::Char:
        switch (ev.ch) {
        case 'q':
          done = true;
          result.ok = false;
          break;
        case 'c':
          if (state.has_selection()) {
            result.ok = true;
            result.paths = state.selected_paths();
            done = true;
          } else {
            msg_pending = true;
          }
          break;
        case 'H':
          open_help();
          break;
        case 'j':
          state.move_cursor(1);
          break;
        case 'k':
          state.move_cursor(-1);
          break;
        case 'h':
          state.go_parent();
          break;
        case 'l':
          state.enter_cursor();
          break;
        case '.':
          state.toggle_hidden();
          break;
        case 'u':
          state
              .refresh_cache(); // 刷新缓存：当前目录及其路径上所有目录立即重扫入缓存
          break;
        default:
          break;
        }
        break;
      case KeyKind::Space:
        state.toggle_cursor_selection();
        break;
      case KeyKind::Enter:
      case KeyKind::Right:
        state.enter_cursor();
        break;
      case KeyKind::Up:
        state.move_cursor(-1);
        break;
      case KeyKind::Down:
        state.move_cursor(1);
        break;
      case KeyKind::Left:
      case KeyKind::Backspace:
        state.go_parent();
        break;
      case KeyKind::WheelUp:
      case KeyKind::WheelDown: {
        // 滚轮按栏路由：左栏滚左栏、右栏滚右栏、其余（含中栏）移动光标
        int delta = ev.kind == KeyKind::WheelUp ? -1 : 1;
        Renderer::ColumnsRegion rg = renderer.columns_region(last_size);
        if (ev.x >= rg.left.col1 && ev.x <= rg.left.col2 && ev.y >= rg.left.row1 &&
            ev.y <= rg.left.row2) {
          state.scroll_left(delta);
        } else if (ev.x >= rg.right.col1 && ev.x <= rg.right.col2 &&
                   ev.y >= rg.right.row1 && ev.y <= rg.right.row2) {
          state.scroll_right(delta);
        } else {
          state.move_cursor(delta);
        }
        break;
      }
      case KeyKind::MouseLeft: {
        // 鼠标左键：左栏跳到祖先、右栏进入子目录、中栏定位光标（均定位到点击行）
        Renderer::ColumnsRegion rg = renderer.columns_region(last_size);
        if (ev.y < rg.left.row1 || ev.y > rg.left.row2)
          break; // 标题/状态栏等非条目区点击忽略
        if (ev.x >= rg.left.col1 && ev.x <= rg.left.col2) {
          // 左栏：若当前目录还有祖先则跳转，偏移定位到点击行
          if (state.parent_listing().dir != state.current_dir()) {
            size_t target =
                state.parent_listing().scroll +
                static_cast<size_t>(ev.y - rg.left.row1);
            state.go_parent();
            state.jump_cursor(target);
          }
        } else if (ev.x >= rg.middle.col1 && ev.x <= rg.middle.col2) {
          size_t target =
              state.current_listing().scroll +
              static_cast<size_t>(ev.y - rg.middle.row1);
          state.jump_cursor(target);
        } else if (ev.x >= rg.right.col1 && ev.x <= rg.right.col2) {
          // 右栏：若中栏光标所指是文件夹则进入子目录，偏移定位到点击行
          size_t target = state.right_listing().scroll +
                          static_cast<size_t>(ev.y - rg.right.row1);
          if (state.enter_cursor())
            state.jump_cursor(target);
        }
        break;
      }
      case KeyKind::Esc: // 扩展：Esc 等价于取消
      case KeyKind::CtrlC:
        done = true;
        result.ok = false;
        break;
      default:
        break; // Home/End/PgUp/PgDn/Unknown：忽略
      }

      if (done)
        break;
      build_status(msg_pending, status_left, status_right);
      last_size = term.size();
      render_now(last_size);
    }
  } catch (const std::exception &e) {
    term.restore();
    std::fprintf(stderr, "file_picker: internal error: %s\n", e.what());
    return {false, {}};
  } catch (...) {
    term.restore();
    return {false, {}};
  }

  term.restore();
  if (!result.ok)
    result.paths.clear(); // 保证 ok=false 时 paths 为空
  return result;
}
