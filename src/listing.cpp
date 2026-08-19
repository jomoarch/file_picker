#include "listing.hpp"
#include "text.hpp"

#include <algorithm>
#include <system_error>

namespace fp {

namespace {

std::string sanitize_name(std::string name) {
  for (char &c : name) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 0x20 || uc == 0x7F)
      c = '?';
  }
  return name;
}

bool is_hidden_name(const std::string &name) {
  return !name.empty() && name[0] == '.';
}

} // namespace

Listing scan_directory(const std::filesystem::path &dir, bool show_hidden) {
  Listing out;
  out.dir = dir;

  std::error_code ec;
  std::filesystem::directory_iterator it(dir, ec);
  if (ec) {
    out.ok = false;
    out.error = ec.message();
    return out;
  }
  const std::filesystem::directory_iterator end;

  for (; it != end; it.increment(ec)) {
    if (ec) {
      ec.clear();
      continue;
    }
    const auto &de = *it;
    std::string name = sanitize_name(path_to_utf8(de.path().filename()));
    if (!show_hidden && is_hidden_name(name))
      continue;

    Entry e;
    e.path = de.path();
    e.name = name;

    std::error_code ec2;
    std::filesystem::file_status ss = de.symlink_status(ec2);
    if (ec2) {
      ec2.clear();
      e.is_link = false;
    } else {
      e.is_link = std::filesystem::is_symlink(ss);
    }

    if (e.is_link) {
      std::error_code ec3;
      e.is_dir = std::filesystem::is_directory(e.path, ec3);
      if (ec3)
        e.is_dir = false;
    } else {
      e.is_dir = std::filesystem::is_directory(ss);
    }
    out.entries.push_back(std::move(e));
  }

  std::sort(out.entries.begin(), out.entries.end(),
            [](const Entry &a, const Entry &b) { return a.name < b.name; });
  return out;
}

} // namespace fp