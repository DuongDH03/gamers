#include "core/file_path.h"

#include <cstdint>
#include <utility>

namespace scraps {
namespace {

// Decodes the UTF-8 sequence starting at `i` into `code_point` and advances
// `i` past it. Refuses overlong forms, surrogates, values past U+10FFFF and
// NUL, so every accepted string means exactly one path.
bool next_code_point(std::string_view text, std::size_t& i, std::uint32_t& code_point) {
  const std::uint32_t lead = static_cast<unsigned char>(text[i]);
  std::size_t extra = 0;
  std::uint32_t minimum = 0;
  if (lead == 0) {
    return false;
  } else if (lead < 0x80u) {
    code_point = lead;
  } else if (lead >= 0xC2u && lead <= 0xDFu) {
    extra = 1;
    code_point = lead & 0x1Fu;
    minimum = 0x80u;
  } else if (lead >= 0xE0u && lead <= 0xEFu) {
    extra = 2;
    code_point = lead & 0x0Fu;
    minimum = 0x800u;
  } else if (lead >= 0xF0u && lead <= 0xF4u) {
    extra = 3;
    code_point = lead & 0x07u;
    minimum = 0x10000u;
  } else {
    return false;
  }
  if (extra > text.size() - i - 1) return false;
  for (std::size_t k = 1; k <= extra; ++k) {
    const std::uint32_t byte = static_cast<unsigned char>(text[i + k]);
    if ((byte & 0xC0u) != 0x80u) return false;
    code_point = (code_point << 6) | (byte & 0x3Fu);
  }
  if (extra > 0 && code_point < minimum) return false;
  if (code_point > 0x10FFFFu || (code_point >= 0xD800u && code_point <= 0xDFFFu)) return false;
  i += extra + 1;
  return true;
}

}  // namespace

bool utf8_to_path(std::string_view utf8, std::filesystem::path& out) {
  out.clear();
#ifdef _WIN32
  // Windows paths are UTF-16: convert by hand, so nothing can throw and no
  // code page is involved.
  std::wstring wide;
  wide.reserve(utf8.size());
  std::size_t i = 0;
  while (i < utf8.size()) {
    std::uint32_t code_point = 0;
    if (!next_code_point(utf8, i, code_point)) return false;
    if (code_point >= 0x10000u) {
      code_point -= 0x10000u;
      wide.push_back(static_cast<wchar_t>(0xD800u + (code_point >> 10)));
      wide.push_back(static_cast<wchar_t>(0xDC00u + (code_point & 0x3FFu)));
    } else {
      wide.push_back(static_cast<wchar_t>(code_point));
    }
  }
  out = std::filesystem::path(std::move(wide));
#else
  // Elsewhere paths are bytes, and the engine's are UTF-8 already; still
  // refuse what is not, like on Windows.
  std::size_t i = 0;
  while (i < utf8.size()) {
    std::uint32_t code_point = 0;
    if (!next_code_point(utf8, i, code_point)) return false;
  }
  out = std::filesystem::path(std::string(utf8));
#endif
  return true;
}

bool open_file_for_reading(std::string_view utf8_path, std::ifstream& file) {
  std::filesystem::path path;
  if (utf8_path.empty() || !utf8_to_path(utf8_path, path)) return false;
  file.open(path, std::ios::binary);
  return static_cast<bool>(file);
}

}  // namespace scraps
