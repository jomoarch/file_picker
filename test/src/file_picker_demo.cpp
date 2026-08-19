#include "file_picker.hpp"

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv) {
  SelectionOptions opts;
  if (argc > 1)
    opts.initial_path = argv[1];
  if (argc > 2) {
    int m = std::atoi(argv[2]);
    if (m >= 0 && m <= 3)
      opts.mode = static_cast<SelectionMode>(m);
  }

  SelectionResult result = pick_files(opts);
  std::printf("RESULT ok=%d paths=%zu\n", result.ok ? 1 : 0,
              result.paths.size());
  for (const auto &p : result.paths) {
    std::printf("  %s\n", p.string().c_str());
  }
  return result.ok ? 0 : 1;
}
