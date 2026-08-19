#ifndef FILE_PICKER_LISTING_HPP
#define FILE_PICKER_LISTING_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace fp {

struct Entry {
  std::filesystem::path path;
  std::string name;
  bool is_dir = false;
  bool is_link = false;
};

struct Listing {
  std::filesystem::path dir;
  bool ok = true;
  std::string error;
  std::vector<Entry> entries;
  size_t scroll = 0;
  size_t cursor = 0;
  int highlight = -1;
};

Listing scan_directory(const std::filesystem::path &dir, bool show_hidden);

} // namespace fp

#endif // FILE_PICKER_LISTING_HPP