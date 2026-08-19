#ifndef FILE_PICKER_LISTING_HPP
#define FILE_PICKER_LISTING_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace fp {

struct Entry {
  std::filesystem::path path;
  std::string name;
  std::string key; // 预计算的规范化 UTF-8 键（path_key_string）
                   // 供选中集合 / 目录缓存哈希直接使用，避免每帧重复构造
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