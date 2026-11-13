#include "common/ui_json.h"

#include <charconv>
#include <cmath>
#include <cstring>
#include <system_error>

#include "platform/input.h"  // InputEventList::utf8_sequence_length

namespace scraps_apps {

// ------------------------------------------------------------------ writer

void JsonWriter::put(char c) {
  if (!m_ok) return;
  if (m_size >= m_capacity) {
    m_ok = false;
    return;
  }
  m_out[m_size++] = c;
}

void JsonWriter::put(const char* text, std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) put(text[i]);
}

void JsonWriter::separate() {
  if (m_after_key) {
    m_after_key = false;
    return;
  }
  if (m_depth > 0 && m_depth <= kMaxDepth) {
    if (m_has_items[m_depth - 1]) put(',');
    m_has_items[m_depth - 1] = true;
  }
}

void JsonWriter::begin_object() {
  separate();
  put('{');
  if (m_depth >= kMaxDepth) m_ok = false;
  ++m_depth;
  if (m_depth <= kMaxDepth) m_has_items[m_depth - 1] = false;
}

void JsonWriter::end_object() { end_object_or_array('}'); }

void JsonWriter::begin_array() {
  separate();
  put('[');
  if (m_depth >= kMaxDepth) m_ok = false;
  ++m_depth;
  if (m_depth <= kMaxDepth) m_has_items[m_depth - 1] = false;
}

void JsonWriter::end_array() { end_object_or_array(']'); }

void JsonWriter::key(const char* name) {
  separate();
  string_body(name);
  put(':');
  m_after_key = true;
}

void JsonWriter::string(const char* text) {
  separate();
  string_body(text);
}

void JsonWriter::string_body(const char* text) {
  static const char kHex[] = "0123456789abcdef";
  put('"');
  const std::size_t size = text != nullptr ? std::strlen(text) : 0;
  std::size_t i = 0;
  while (i < size) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c >= 0x80u) {
      const std::size_t length = scraps::InputEventList::utf8_sequence_length(text + i, size - i);
      if (length == 0) {
        put('?');
        ++i;
      } else {
        put(text + i, length);
        i += length;
      }
      continue;
    }
    if (c == '"' || c == '\\') {
      put('\\');
      put(static_cast<char>(c));
    } else if (c == '\n') {
      put("\\n", 2);
    } else if (c == '\r') {
      put("\\r", 2);
    } else if (c == '\t') {
      put("\\t", 2);
    } else if (c < 0x20u || c == 0x7Fu) {
      const char escaped[6] = {'\\', 'u', '0', '0', kHex[c >> 4], kHex[c & 0xFu]};
      put(escaped, sizeof(escaped));
    } else {
      put(static_cast<char>(c));
    }
    ++i;
  }
  put('"');
}

void JsonWriter::number(float value) {
  separate();
  if (!std::isfinite(value)) value = 0.0f;
  char digits[32];
  const std::to_chars_result result = std::to_chars(digits, digits + sizeof(digits), value);
  if (result.ec != std::errc()) {
    m_ok = false;
    return;
  }
  put(digits, static_cast<std::size_t>(result.ptr - digits));
}

void JsonWriter::boolean(bool value) {
  separate();
  if (value) {
    put("true", 4);
  } else {
    put("false", 5);
  }
}

void JsonWriter::end_object_or_array(char close) {
  put(close);
  if (m_depth == 0) {
    m_ok = false;
    return;
  }
  --m_depth;
}

// ------------------------------------------------------------------ reader

namespace {

class Parser {
 public:
  Parser(const char* data, std::size_t size) : m_data(data), m_size(data != nullptr ? size : 0) {}

  void skip_space() {
    while (m_pos < m_size) {
      const char c = m_data[m_pos];
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return;
      ++m_pos;
    }
  }
  bool at_end() const { return m_pos >= m_size; }
  bool take(char c) {
    if (m_pos < m_size && m_data[m_pos] == c) {
      ++m_pos;
      return true;
    }
    return false;
  }
  char peek() const { return m_pos < m_size ? m_data[m_pos] : '\0'; }
  bool take_word(const char* word) {
    const std::size_t length = std::strlen(word);
    if (m_size - m_pos < length || std::memcmp(m_data + m_pos, word, length) != 0) return false;
    m_pos += length;
    return true;
  }

  // A JSON string into `out` (`capacity` bytes, room for the '\0' included).
  bool string(char* out, std::size_t capacity, std::size_t& size) {
    size = 0;
    if (!take('"')) return false;
    while (true) {
      if (m_pos >= m_size) return false;
      const unsigned char c = static_cast<unsigned char>(m_data[m_pos]);
      if (c == '"') {
        ++m_pos;
        out[size] = '\0';
        return true;
      }
      if (c < 0x20u) return false;  // raw control characters are not allowed
      if (c == '\\') {
        ++m_pos;
        if (m_pos >= m_size) return false;
        const char escape = m_data[m_pos++];
        char simple = '\0';
        switch (escape) {
          case '"': simple = '"'; break;
          case '\\': simple = '\\'; break;
          case '/': simple = '/'; break;
          case 'b': simple = '\b'; break;
          case 'f': simple = '\f'; break;
          case 'n': simple = '\n'; break;
          case 'r': simple = '\r'; break;
          case 't': simple = '\t'; break;
          case 'u': {
            std::uint32_t code = 0;
            if (!hex4(code)) return false;
            if (code >= 0xD800u && code <= 0xDBFFu) {
              // A high surrogate must be followed by a low one.
              std::uint32_t low = 0;
              if (!take('\\') || !take('u') || !hex4(low) || low < 0xDC00u || low > 0xDFFFu) return false;
              code = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
            } else if (code >= 0xDC00u && code <= 0xDFFFu) {
              return false;  // a lone low surrogate
            }
            if (code == 0) return false;  // strings here hold no '\0'
            if (!append_utf8(code, out, capacity, size)) return false;
            continue;
          }
          default: return false;
        }
        if (size + 1 >= capacity) return false;
        out[size++] = simple;
        continue;
      }
      if (c >= 0x80u) {
        const std::size_t length = scraps::InputEventList::utf8_sequence_length(m_data + m_pos, m_size - m_pos);
        if (length == 0 || size + length >= capacity) return false;
        for (std::size_t k = 0; k < length; ++k) out[size++] = m_data[m_pos + k];
        m_pos += length;
        continue;
      }
      if (size + 1 >= capacity) return false;
      out[size++] = static_cast<char>(c);
      ++m_pos;
    }
  }

  // A number in exactly JSON's grammar, finite as a double.
  bool number(double& out) {
    const std::size_t start = m_pos;
    take('-');
    if (take('0')) {
      // no leading zeros
    } else if (digit()) {
      while (digit()) {
      }
    } else {
      return false;
    }
    if (take('.')) {
      if (!digit()) return false;
      while (digit()) {
      }
    }
    if (take('e') || take('E')) {
      if (!take('+')) take('-');
      if (!digit()) return false;
      while (digit()) {
      }
    }
    const std::from_chars_result result = std::from_chars(m_data + start, m_data + m_pos, out);
    return result.ec == std::errc() && result.ptr == m_data + m_pos && std::isfinite(out);
  }

 private:
  bool digit() {
    if (m_pos < m_size && m_data[m_pos] >= '0' && m_data[m_pos] <= '9') {
      ++m_pos;
      return true;
    }
    return false;
  }
  bool hex4(std::uint32_t& out) {
    if (m_size - m_pos < 4) return false;
    out = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = m_data[m_pos++];
      std::uint32_t value = 0;
      if (c >= '0' && c <= '9') {
        value = static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value = static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value = static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        return false;
      }
      out = (out << 4) | value;
    }
    return true;
  }
  static bool append_utf8(std::uint32_t code, char* out, std::size_t capacity, std::size_t& size) {
    char bytes[4];
    std::size_t length = 0;
    if (code < 0x80u) {
      bytes[length++] = static_cast<char>(code);
    } else if (code < 0x800u) {
      bytes[length++] = static_cast<char>(0xC0u | (code >> 6));
      bytes[length++] = static_cast<char>(0x80u | (code & 0x3Fu));
    } else if (code < 0x10000u) {
      bytes[length++] = static_cast<char>(0xE0u | (code >> 12));
      bytes[length++] = static_cast<char>(0x80u | ((code >> 6) & 0x3Fu));
      bytes[length++] = static_cast<char>(0x80u | (code & 0x3Fu));
    } else {
      bytes[length++] = static_cast<char>(0xF0u | (code >> 18));
      bytes[length++] = static_cast<char>(0x80u | ((code >> 12) & 0x3Fu));
      bytes[length++] = static_cast<char>(0x80u | ((code >> 6) & 0x3Fu));
      bytes[length++] = static_cast<char>(0x80u | (code & 0x3Fu));
    }
    if (size + length >= capacity) return false;
    for (std::size_t k = 0; k < length; ++k) out[size++] = bytes[k];
    return true;
  }

  const char* m_data;
  std::size_t m_size;
  std::size_t m_pos = 0;
};

}  // namespace

bool JsonObject::parse(const char* data, std::size_t size) {
  m_count = 0;
  Parser parser(data, size);
  parser.skip_space();
  if (!parser.take('{')) return false;
  parser.skip_space();
  bool ok = true;
  if (!parser.take('}')) {
    while (true) {
      if (m_count >= kMaxFields) {
        ok = false;
        break;
      }
      JsonField& field = m_fields[m_count];
      std::size_t key_size = 0;
      parser.skip_space();
      if (!parser.string(field.key.data(), field.key.size(), key_size)) {
        ok = false;
        break;
      }
      for (std::size_t i = 0; i < m_count && ok; ++i) {
        ok = std::strcmp(m_fields[i].key.data(), field.key.data()) != 0;  // no duplicate keys
      }
      parser.skip_space();
      if (!ok || !parser.take(':')) {
        ok = false;
        break;
      }
      parser.skip_space();
      field.text[0] = '\0';
      field.text_size = 0;
      field.number = 0.0;
      field.boolean = false;
      const char c = parser.peek();
      if (c == '"') {
        field.type = JsonType::kString;
        ok = parser.string(field.text.data(), field.text.size(), field.text_size);
      } else if (c == 't' || c == 'f') {
        field.type = JsonType::kBool;
        field.boolean = c == 't';
        ok = parser.take_word(c == 't' ? "true" : "false");
      } else if (c == 'n') {
        field.type = JsonType::kNull;
        ok = parser.take_word("null");
      } else {
        field.type = JsonType::kNumber;
        ok = parser.number(field.number);
      }
      if (!ok) break;
      ++m_count;
      parser.skip_space();
      if (parser.take('}')) break;
      if (!parser.take(',')) {
        ok = false;
        break;
      }
    }
  }
  if (ok) {
    parser.skip_space();
    ok = parser.at_end();
  }
  if (!ok) m_count = 0;
  return ok;
}

const JsonField* JsonObject::find(const char* key) const {
  if (key == nullptr) return nullptr;
  for (std::size_t i = 0; i < m_count; ++i) {
    if (std::strcmp(m_fields[i].key.data(), key) == 0) return &m_fields[i];
  }
  return nullptr;
}

const char* JsonObject::string(const char* key) const {
  const JsonField* field = find(key);
  return field != nullptr && field->type == JsonType::kString ? field->text.data() : nullptr;
}

}  // namespace scraps_apps
