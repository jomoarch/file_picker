#ifndef FILE_PICKER_TEXT_HPP
#define FILE_PICKER_TEXT_HPP

#include <filesystem>
#include <string>
#include <utility>

namespace fp {

// ---- UTF-8 ----

// 从 s 的 pos 位置解码一个 Unicode 码点
// 成功则返回 true 并输出 cp 与 consumde
// 失败返回 false
bool utf8_decode(const std::string &s, size_t pos, char32_t &cp,
                 size_t &consumed);

// 判断 UTF-8 首字母的序列长度
// 1-4=完整长度
// 0=非法首字节
int utf8_seq_len(unsigned char lead);

// 把码点编码为 UTF-8 追加到 out
void utf8_append(std::string &out, char32_t cp);

// ---- 终端显示宽度 ----

// 单个码点的终端列宽
int display_width(char32_t cp);

// UTF-8 字符串的终端显示宽度
int string_display_width(const std::string &utf8);

// 截断字符串，超出部分以 "..." 结尾
std::string truncate_to_width(const std::string &utf8, int width);

// 截断字符串，但保留后缀
std::string truncate_keep_tail(const std::string &utf8, int width);

// ---- Path <-> UTF-8 ----

// 把 path 转为 UTF-8
std::string path_to_utf8(const std::filesystem::path &p);

// 用 UTF-8 构造 path
std::filesystem::path path_from_utf8(const std::string &utf8);

// 路径的规范化 UTF-8 键：先去 . .. 冗余再转 UTF-8，
// 供选中集合 / 目录缓存等哈希查找直接使用（避免每次查找重复构造）
std::string path_key_string(const std::filesystem::path &p);

} // namespace fp

#endif // FILE_PICKER_TEXT_HPP