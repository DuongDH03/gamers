#include "backends/webview2/page_input.h"

#include <charconv>
#include <cmath>
#include <cstring>

namespace scraps::webview2 {
namespace {

// Positions beyond this are not a canvas pixel on any screen.
constexpr float kMaxCoordinate = 1.0e6f;
// More wheel notches than this in one event is not a wheel.
constexpr float kMaxWheel = 100.0f;

struct KeyCode {
  const char* code;
  Key key;
};

// Physical keys (KeyboardEvent.code), as the sdl3 backend uses scancodes:
// "KeyW" is the key where W is on a US layout, whatever the layout says.
constexpr KeyCode kKeyCodes[] = {
    {"KeyW", Key::kW},        {"KeyA", Key::kA},          {"KeyS", Key::kS},          {"KeyD", Key::kD},
    {"KeyE", Key::kE},        {"KeyQ", Key::kQ},          {"KeyR", Key::kR},          {"KeyV", Key::kV},
    {"KeyX", Key::kX},        {"KeyZ", Key::kZ},          {"ArrowUp", Key::kUp},      {"ArrowDown", Key::kDown},
    {"ArrowLeft", Key::kLeft}, {"ArrowRight", Key::kRight}, {"Space", Key::kSpace},     {"Enter", Key::kEnter},
    {"NumpadEnter", Key::kEnter}, {"Tab", Key::kTab},     {"Minus", Key::kMinus},     {"Equal", Key::kEquals},
    {"Digit1", Key::k1},      {"Digit2", Key::k2},        {"Digit3", Key::k3},        {"F1", Key::kF1},
    {"F2", Key::kF2},         {"F3", Key::kF3},           {"KeyP", Key::kP},          {"F5", Key::kF5},
    {"F9", Key::kF9},         {"Backspace", Key::kBackspace}, {"Delete", Key::kDelete}, {"Home", Key::kHome},
    {"End", Key::kEnd},
    // No "Escape": the page owns Esc (see page_input.h).
};

// Splits `text` at single spaces into at most `max` tokens; false if there
// are more, or empty ones (two spaces in a row, a space at either end).
struct Tokens {
  static constexpr std::size_t kMax = 8;
  const char* begin[kMax] = {};
  std::size_t size[kMax] = {};
  std::size_t count = 0;
};

bool split(const char* text, std::size_t size, std::size_t max, Tokens& out) {
  out.count = 0;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= size; ++i) {
    if (i < size && text[i] != ' ') continue;
    if (i == start || out.count >= max || out.count >= Tokens::kMax) return false;
    out.begin[out.count] = text + start;
    out.size[out.count] = i - start;
    ++out.count;
    start = i + 1;
  }
  return out.count > 0;
}

bool is(const Tokens& tokens, std::size_t i, const char* word) {
  const std::size_t length = std::strlen(word);
  return i < tokens.count && tokens.size[i] == length && std::memcmp(tokens.begin[i], word, length) == 0;
}

bool parse_float(const Tokens& tokens, std::size_t i, float limit, float& out) {
  if (i >= tokens.count) return false;
  float value = 0.0f;
  const char* end = tokens.begin[i] + tokens.size[i];
  const std::from_chars_result result = std::from_chars(tokens.begin[i], end, value);
  if (result.ec != std::errc() || result.ptr != end || !std::isfinite(value) || std::fabs(value) > limit) return false;
  out = value;
  return true;
}

bool parse_u32(const Tokens& tokens, std::size_t i, std::uint32_t& out) {
  if (i >= tokens.count) return false;
  const char* end = tokens.begin[i] + tokens.size[i];
  std::uint32_t value = 0;
  const std::from_chars_result result = std::from_chars(tokens.begin[i], end, value);
  if (result.ec != std::errc() || result.ptr != end) return false;
  out = value;
  return true;
}

bool parse_flag(const Tokens& tokens, std::size_t i, bool& out) {
  if (is(tokens, i, "0")) {
    out = false;
    return true;
  }
  if (is(tokens, i, "1")) {
    out = true;
    return true;
  }
  return false;
}

bool parse_button(const Tokens& tokens, std::size_t i, MouseButton& out) {
  if (is(tokens, i, "0")) {
    out = MouseButton::kLeft;
  } else if (is(tokens, i, "1")) {
    out = MouseButton::kMiddle;
  } else if (is(tokens, i, "2")) {
    out = MouseButton::kRight;
  } else {
    return false;
  }
  return true;
}

bool starts_with(const char* text, std::size_t size, const char* prefix) {
  const std::size_t length = std::strlen(prefix);
  return size >= length && std::memcmp(text, prefix, length) == 0;
}

}  // namespace

PageMessage classify_page_message(const char* text, std::size_t size) {
  if (text == nullptr) return PageMessage::kUnknown;
  if (size == 2 && std::memcmp(text, "hi", 2) == 0) return PageMessage::kHello;
  if (size == 2 && std::memcmp(text, "bl", 2) == 0) return PageMessage::kInput;
  if (size < 3 || text[2] != ' ') return PageMessage::kUnknown;
  if (starts_with(text, size, "rf ")) return PageMessage::kFrame;
  if (starts_with(text, size, "pr ")) return PageMessage::kProbe;
  if (starts_with(text, size, "sc ")) return PageMessage::kScreenshot;
  static const char* const kInputTags[] = {"kd ", "ku ", "tx ", "pm ", "pd ", "pu ", "wh ", "pe "};
  for (const char* tag : kInputTags) {
    if (starts_with(text, size, tag)) return PageMessage::kInput;
  }
  return PageMessage::kUnknown;
}

bool parse_frame_request(const char* text, std::size_t size, FrameRequest& out) {
  Tokens tokens;
  if (text == nullptr || size > 64 || !split(text, size, 5, tokens) || tokens.count != 5 || !is(tokens, 0, "rf")) {
    return false;
  }
  FrameRequest request;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  if (!parse_u32(tokens, 1, request.generation) || !parse_u32(tokens, 2, request.acked) ||
      !parse_u32(tokens, 3, width) || !parse_u32(tokens, 4, height)) {
    return false;
  }
  if (width == 0 || height == 0 || width > 16384 || height > 16384) return false;
  request.width = static_cast<int>(width);
  request.height = static_cast<int>(height);
  out = request;
  return true;
}

bool parse_probe(const char* text, std::size_t size, ProbeResult& out) {
  Tokens tokens;
  if (text == nullptr || size > 96 || !split(text, size, 8, tokens) || tokens.count != 8 || !is(tokens, 0, "pr")) {
    return false;
  }
  ProbeResult probe;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  if (!parse_u32(tokens, 1, probe.views) || !parse_u32(tokens, 2, probe.clicked) || !parse_u32(tokens, 3, width) ||
      !parse_u32(tokens, 4, height) || !parse_u32(tokens, 5, probe.colors) || !parse_u32(tokens, 6, probe.not_clear) ||
      !parse_u32(tokens, 7, probe.leaked)) {
    return false;
  }
  if (width > 16384 || height > 16384 || probe.not_clear > 1000) return false;
  probe.width = static_cast<int>(width);
  probe.height = static_cast<int>(height);
  out = probe;
  return true;
}

Key key_from_dom_code(const char* code, std::size_t size) {
  if (code == nullptr) return Key::kUnknown;
  for (const KeyCode& entry : kKeyCodes) {
    if (std::strlen(entry.code) == size && std::memcmp(entry.code, code, size) == 0) return entry.key;
  }
  return Key::kUnknown;
}

bool PageInput::push_pending(const Pending& pending) {
  if (m_pending_count >= kMaxPending) {
    ++m_overflowed;
    return false;
  }
  m_pending[m_pending_count++] = pending;
  return true;
}

bool PageInput::queue(const char* text, std::size_t size) {
  if (text == nullptr || classify_page_message(text, size) != PageMessage::kInput) {
    ++m_rejected;
    return false;
  }
  Pending pending;
  InputEvent& event = pending.event;
  if (starts_with(text, size, "tx ")) {
    // Typed text: the rest of the message. Split into events of whole code
    // points; bytes that are not UTF-8 make the whole message invalid.
    const char* utf8 = text + 3;
    const std::size_t length = size - 3;
    if (length == 0 || length > kMaxTextBytes) {
      ++m_rejected;
      return false;
    }
    for (std::size_t i = 0; i < length;) {
      const std::size_t n = InputEventList::utf8_sequence_length(utf8 + i, length - i);
      if (n == 0) {
        ++m_rejected;
        return false;
      }
      i += n;
    }
    event.type = InputEventType::kText;
    std::size_t used = 0;
    bool all = true;
    for (std::size_t i = 0; i < length;) {
      const std::size_t n = InputEventList::utf8_sequence_length(utf8 + i, length - i);
      if (used + n > InputEvent::kTextCapacity - 1) {
        event.text[used] = '\0';
        all = push_pending(pending) && all;
        used = 0;
      }
      for (std::size_t k = 0; k < n; ++k) event.text[used++] = utf8[i + k];
      i += n;
    }
    event.text[used] = '\0';
    return push_pending(pending) && all;
  }

  Tokens tokens;
  if (size > 96 || !split(text, size, 4, tokens)) {
    ++m_rejected;
    return false;
  }
  bool ok = false;
  if (is(tokens, 0, "kd") && tokens.count == 3) {
    event.type = InputEventType::kKeyDown;
    event.key = key_from_dom_code(tokens.begin[1], tokens.size[1]);
    ok = event.key != Key::kUnknown && parse_flag(tokens, 2, event.repeat);
  } else if (is(tokens, 0, "ku") && tokens.count == 2) {
    event.type = InputEventType::kKeyUp;
    event.key = key_from_dom_code(tokens.begin[1], tokens.size[1]);
    ok = event.key != Key::kUnknown;
  } else if (is(tokens, 0, "pm") && tokens.count == 3) {
    event.type = InputEventType::kPointerMove;
    ok = parse_float(tokens, 1, kMaxCoordinate, event.x) && parse_float(tokens, 2, kMaxCoordinate, event.y);
  } else if ((is(tokens, 0, "pd") || is(tokens, 0, "pu")) && tokens.count == 4) {
    event.type = is(tokens, 0, "pd") ? InputEventType::kPointerDown : InputEventType::kPointerUp;
    ok = parse_button(tokens, 1, event.button) && parse_float(tokens, 2, kMaxCoordinate, event.x) &&
         parse_float(tokens, 3, kMaxCoordinate, event.y);
  } else if (is(tokens, 0, "wh") && tokens.count == 2) {
    event.type = InputEventType::kWheel;
    ok = parse_float(tokens, 1, kMaxWheel, event.wheel);
  } else if (is(tokens, 0, "pe") && tokens.count == 2) {
    bool inside = false;
    ok = parse_flag(tokens, 1, inside);
    pending.kind = inside ? Kind::kInside : Kind::kOutside;
  } else if (is(tokens, 0, "bl") && tokens.count == 1) {
    pending.kind = Kind::kBlur;
    ok = true;
  }
  if (!ok) {
    ++m_rejected;
    return false;
  }
  return push_pending(pending);
}

void PageInput::apply(const Pending& pending) {
  switch (pending.kind) {
    case Kind::kInside:
      m_inside = true;
      return;
    case Kind::kOutside:
      m_inside = false;
      return;
    case Kind::kBlur:
      // As when an SDL window loses focus: buttons and keys are let go of
      // without "up" events (the presses went to another window).
      m_down.fill(false);
      m_button_down.fill(false);
      m_inside = false;
      return;
    case Kind::kEvent:
      break;
  }
  const InputEvent& event = pending.event;
  const std::size_t key = static_cast<std::size_t>(event.key);
  const std::size_t button = static_cast<std::size_t>(event.button);
  switch (event.type) {
    case InputEventType::kKeyDown:
      if (key >= kKeys) return;
      if (!event.repeat) m_pressed[key] = true;
      m_down[key] = true;
      break;
    case InputEventType::kKeyUp:
      if (key >= kKeys) return;
      m_down[key] = false;
      break;
    case InputEventType::kText:
      if (!m_text_input) return;  // as SDL: text only while a text field asked for it
      break;
    case InputEventType::kPointerMove:
      m_pointer.x = event.x;
      m_pointer.y = event.y;
      m_inside = true;
      break;
    case InputEventType::kPointerDown:
    case InputEventType::kPointerUp:
      if (button >= kButtons) return;
      m_pointer.x = event.x;
      m_pointer.y = event.y;
      if (event.type == InputEventType::kPointerDown) {
        m_inside = true;  // the page sends presses on the canvas only
        m_button_pressed[button] = true;
        m_button_down[button] = true;
      } else {
        m_button_released[button] = true;
        m_button_down[button] = false;
      }
      break;
    case InputEventType::kWheel:
      m_wheel += event.wheel;
      break;
    case InputEventType::kPadDown:
    case InputEventType::kPadUp:
      return;  // never from the page
  }
  m_events.push(event);
}

void PageInput::pump(bool focused) {
  m_pressed.fill(false);
  m_button_pressed.fill(false);
  m_button_released.fill(false);
  m_wheel = 0.0f;
  m_events.clear();
  for (std::size_t i = 0; i < m_pending_count; ++i) apply(m_pending[i]);
  m_pending_count = 0;

  // The gamepad, from its source, only while this window is in front. Its
  // button events follow the page's events of this frame.
  m_pad_active = focused && m_pad != nullptr;
  if (m_pad_active) {
    for (const InputEvent& event : m_pad->events()) {
      if (event.type == InputEventType::kPadDown || event.type == InputEventType::kPadUp) m_events.push(event);
    }
  }
}

bool PageInput::is_down(Key key) const {
  const std::size_t i = static_cast<std::size_t>(key);
  return i < kKeys && m_down[i];
}

bool PageInput::was_pressed(Key key) const {
  const std::size_t i = static_cast<std::size_t>(key);
  return i < kKeys && m_pressed[i];
}

bool PageInput::is_down(MouseButton button) const {
  const std::size_t i = static_cast<std::size_t>(button);
  return i < kButtons && m_button_down[i];
}

bool PageInput::was_pressed(MouseButton button) const {
  const std::size_t i = static_cast<std::size_t>(button);
  return i < kButtons && m_button_pressed[i];
}

bool PageInput::was_released(MouseButton button) const {
  const std::size_t i = static_cast<std::size_t>(button);
  return i < kButtons && m_button_released[i];
}

}  // namespace scraps::webview2
