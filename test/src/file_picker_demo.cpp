#include "file_picker.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char **argv) {
  SelectionOptions opts;
  opts.filter = select_file_only;
  if (argc > 1)
    opts.initial_path = argv[1];
  if (argc > 2) {
    int m = std::atoi(argv[2]);
    if (m == 0)
      opts.mode = SelectionMode::SINGLE;
    else if (m == 1)
      opts.mode = SelectionMode::MULTI;
  }
  if (argc > 3) {
    std::string f = argv[3];
    if (f == "file")
      opts.filter = select_file_only;
    else if (f == "dir")
      opts.filter = select_directory_only;
  }

  SelectionResult result = pick_files(opts);
  std::printf("RESULT ok=%d paths=%zu\n", result.ok ? 1 : 0,
              result.paths.size());
  for (const auto &p : result.paths) {
    // -1=未设 filter；0/1=是否通过当前 filter（对空 std::function
    // 调用会抛异常）
    std::printf("  %s\n", p.string().c_str());
  }
  return result.ok ? 0 : 1;
}
