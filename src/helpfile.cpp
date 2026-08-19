#include "helpfile.hpp"
#include "text.hpp"

#include <fstream>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fp {

namespace fs = std::filesystem;

std::filesystem::path exe_directory() {
#if defined(_WIN32)
  wchar_t buf[MAX_PATH];
  DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH)
    return {};
  return fs::path(buf).parent_path();
#elif defined(__linux__)
  char buf[4096];
  ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0)
    return {};
  buf[n] = '\0';
  return fs::path(buf).parent_path();
#else
  std::error_code ec;
  return fs::current_path(ec);
#endif
}

std::filesystem::path
find_help_file(const std::filesystem::path &explicit_path) {
  if (!explicit_path.empty()) {
    return fs::exists(explicit_path) ? explicit_path : fs::path();
  }
  std::vector<fs::path> base_dirs;
  fs::path exe = exe_directory();
  if (!exe.empty()) {
    base_dirs.push_back(exe);
    base_dirs.push_back(exe / "docs");
  }
  std::error_code ec;
  fs::path cwd = fs::current_path(ec);
  if (!ec) {
    base_dirs.push_back(cwd);
    base_dirs.push_back(cwd / "docs");
  }
  for (const auto &d : base_dirs) {
    fs::path cand = d / "file_picker_help.txt";
    if (fs::exists(cand))
      return cand;
  }
  return {};
}

bool load_help_lines(const std::filesystem::path &path,
                     std::vector<std::string> &lines, std::string &error) {
  lines.clear();
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    error = "cannot open file: " + path_to_utf8(path);
    return false;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  std::string data = ss.str();

  size_t start = 0;
  for (size_t i = 0; i <= data.size(); ++i) {
    if (i == data.size() || data[i] == '\n') {
      if (start < i || i < data.size()) {
        size_t end = i;
        if (end > start && data[end - 1] == '\r')
          --end;
        lines.emplace_back(data, start, end - start);
      }
      start = i + 1;
    }
  }
  return true;
}

} // namespace fp