#pragma once

// A tiny JSON writer and reader for the UI bridge (docs/specs/2026-11-14-
// offline-essentials-and-ui.md, part 6a). The messages are small and flat,
// so this is not a general JSON library, and it never allocates: the writer
// fills a buffer the caller owns, the reader parses into fixed arrays.
//
// The reader takes one flat object: string, number, true/false/null values
// only. Anything else (nested objects or arrays, comments, trailing commas,
// duplicate keys, invalid UTF-8, lone surrogates, numbers that overflow,
// strings longer than kMaxText bytes, more than kMaxFields fields, text after
// the object) makes the whole message malformed. The UI page is untrusted, so
// "malformed" is the answer to anything unusual, never a guess.

#include <array>
#include <cstddef>
#include <cstdint>

namespace scraps_apps {

// Writes JSON into `out` (`capacity` bytes). If it runs out of room it stops
// writing and ok() turns false; the text is then not usable.
class JsonWriter {
 public:
  JsonWriter(char* out, std::size_t capacity) : m_out(out), m_capacity(out != nullptr ? capacity : 0) {}

  void begin_object();
  void end_object();
  void begin_array();
  void end_array();
  // An object key; the value comes next.
  void key(const char* name);
  // A string value. Bytes that are not valid UTF-8 become '?', so the text
  // is always valid JSON even when a C string was cut in the middle of a
  // character.
  void string(const char* text);
  // A finite number, in the shortest form that reads back as the same
  // float; not finite is written as 0 (JSON has no NaN).
  void number(float value);
  void boolean(bool value);

  bool ok() const { return m_ok; }
  // Bytes written (no '\0' is added).
  std::size_t size() const { return m_size; }

 private:
  static constexpr std::size_t kMaxDepth = 8;

  void put(char c);
  void put(const char* text, std::size_t size);
  // Writes a ',' if a value already came before this one at this depth.
  void separate();
  // The quoted, escaped string, without a separator.
  void string_body(const char* text);
  void end_object_or_array(char close);

  char* m_out;
  std::size_t m_capacity;
  std::size_t m_size = 0;
  bool m_ok = true;
  std::size_t m_depth = 0;
  std::array<bool, kMaxDepth> m_has_items{};
  bool m_after_key = false;
};

enum class JsonType : std::uint8_t { kString, kNumber, kBool, kNull };

// One field of a flat object. String values are unescaped UTF-8 with a '\0'
// after them; a "\u0000" escape is refused, so strings hold no '\0' bytes.
struct JsonField {
  static constexpr std::size_t kMaxKey = 31;
  static constexpr std::size_t kMaxText = 255;

  std::array<char, kMaxKey + 1> key{};
  JsonType type = JsonType::kNull;
  std::array<char, kMaxText + 1> text{};  // kString
  std::size_t text_size = 0;
  double number = 0.0;  // kNumber, always finite
  bool boolean = false;  // kBool
};

// A parsed flat JSON object.
class JsonObject {
 public:
  static constexpr std::size_t kMaxFields = 16;

  // Parses `size` bytes. False if they are not exactly one flat JSON object
  // within the limits above; the object is then empty.
  bool parse(const char* data, std::size_t size);

  std::size_t size() const { return m_count; }
  const JsonField& field(std::size_t i) const { return m_fields[i < m_count ? i : 0]; }
  // The field named `key`, or nullptr.
  const JsonField* find(const char* key) const;
  // The string value of `key`, or nullptr if it is missing or not a string.
  const char* string(const char* key) const;

 private:
  std::array<JsonField, kMaxFields> m_fields{};
  std::size_t m_count = 0;
};

}  // namespace scraps_apps
