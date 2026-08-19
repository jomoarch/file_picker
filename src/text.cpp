#include "text.hpp"
#include "wcwidth.hpp"

namespace fp {

bool utf8_decode(const std::string &s, size_t pos, char32_t &cp,
                 size_t &consumed) {
  if (pos >= s.size())
    return false;
  unsigned char b0 = static_cast<unsigned char>(s[pos]);
  size_t n;
  char32_t v;
  if (b0 < 0x80) {
    n = 1;
    v = b0;
  } else if ((b0 & 0xE0) == 0xC0) {
    n = 2;
    v = b0 & 0x1F;
  } else if ((b0 & 0xF0) == 0xE0) {
    n = 3;
    v = b0 & 0x0F;
  } else if ((b0 & 0xF8) == 0xF0) {
    n = 4;
    v = b0 & 0x07;
  } else {
    return false;
  }
  if (pos + n > s.size())
    return false;
  for (size_t i = 1; i < n; ++i) {
    unsigned char b = static_cast<unsigned char>(s[pos + i]);
    if ((b & 0xC0) != 0x80)
      return false;
    v = (v << 6) | (b & 0x3F);
  }
  if ((n == 2 && v < 0x80) || (n == 3 && v < 0x800) || (n == 4 && v < 0x10000))
    return false;
  if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
    return false;
  cp = v;
  consumed = n;
  return true;
}

int utf8_seq_len(unsigned char lead) {
  if (lead < 0x80)
    return 1;
  if ((lead & 0xE0) == 0xC0)
    return 2;
  if ((lead & 0xF0) == 0xE0)
    return 3;
  if ((lead & 0xF8) == 0xF0)
    return 4;
  return 0;
}

void utf8_append(std::string &out, char32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

int display_width(char32_t cp) {
  int w = mk_wcwidth(static_cast<uint32_t>(cp));
  return w < 0 ? 0 : w;
}

int string_display_width(const std::string &s) {
  int w = 0;
  size_t pos = 0;
  while (pos < s.size()) {
    char32_t cp;
    size_t consumed;
    if (!utf8_decode(s, pos, cp, consumed)) {
      ++w;
      pos += 1;
      continue;
    }
    w += display_width(cp);
    pos += consumed;
  }
  return w;
}

std::string truncate_to_width(const std::string &s, int width) {
  if (width <= 0)
    return std::string();
  if (string_display_width(s) <= width)
    return s;
  int budget = width >= 4 ? width - 3 : width;
  std::string out;
  int w = 0;
  size_t pos = 0;
  while (pos < s.size()) {
    char32_t cp;
    size_t consumed;
    if (!utf8_decode(s, pos, cp, consumed)) {
      if (w + 1 > budget)
        break;
      out.push_back(s[pos]);
      ++w;
      pos += 1;
      continue;
    }
    int cw = display_width(cp);
    if (w + cw > budget)
      break;
    w += cw;
    out.append(s, pos, consumed);
    pos += consumed;
  }
  if (width >= 4) {
    out += "...";
  } else {
    int dots = width - w;
    if (dots > 0)
      out.append(static_cast<size_t>(dots), '.');
  }
  return out;
}

std::string truncate_keep_tail(const std::string &utf8, int width) {
  if (width <= 3)
    return std::string(static_cast<size_t>(width > 0 ? width : 0), '.');
  if (string_display_width(utf8) <= width)
    return utf8;
  std::string tail;
  int w = 0;
  size_t pos = utf8.size();
  while (pos > 0) {
    size_t start = pos;
    while (start > 0 &&
           (static_cast<unsigned char>(utf8[start - 1]) & 0xC0) == 0x80)
      --start;
    char32_t cp;
    size_t consumed;
    if (!utf8_decode(utf8, start, cp, consumed)) {
      if (start == 0)
        break;
      pos = start - 1;
      continue;
    }
    int cw = display_width(cp);
    if (w + cw > width - 3)
      break;
    w += cw;
    tail.insert(0, utf8, start, consumed);
    pos = start;
  }
  return "..." + tail;
}

std::string path_to_utf8(const std::filesystem::path &p) {
#if defined(__cpp_lib_char8_t)
  auto s = p.generic_u8string();
  return std::string(reinterpret_cast<const char *>(s.data()), s.size());
#else
  return p.generic_u8string();
#endif
}

std::filesystem::path path_from_utf8(const std::string &utf8) {
#if defined(__cpp_lib_char8_t)
  return std::filesystem::path(std::u8string(
      reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
#else
  return std::filesystem::u8path(utf8);
#endif
}

} // namespace fp