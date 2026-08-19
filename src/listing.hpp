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

// 条目排序比较器：目录（含指向目录的链接）总在文件（含指向文件的链接）前
// 同类内按名称字典序。scan_directory 排序与 PickerState 定位条目共用
bool entry_sorted_before(const Entry &a, const Entry &b);

} // namespace fp

#endif // FILE_PICKER_LISTING_HPP