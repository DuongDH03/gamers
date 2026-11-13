#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace scraps {

// The keys the engine knows about. Backends translate their own key codes.
enum class Key : std::uint8_t {
  kUnknown = 0,
  kW, kA, kS, kD, kE, kQ, kR, kV, kX, kZ,
  kUp, kDown, kLeft, kRight,
  kSpace, kEnter, kEscape, kTab,
  kMinus, kEquals,
  k1, k2, k3,
  kF1, kF2, kF3,
  kP, kF5, kF9,
  kBackspace, kDelete, kHome, kEnd,  // for text fields (key events in Input::events())
  kCount
};

// Mouse buttons the engine knows about.
enum class MouseButton : std::uint8_t { kLeft, kRight, kMiddle, kCount };

// Gamepad buttons, named by position, not by label: kSouth is the bottom face
// button (A on Xbox, Cross on PlayStation, B on Nintendo), kEast the right
// one, and so on. A game binds "south" and every pad agrees where that is.
enum class GamepadButton : std::uint8_t {
  kSouth, kEast, kWest, kNorth,
  kDpadUp, kDpadDown, kDpadLeft, kDpadRight,
  kStart, kBack,
  kLeftShoulder, kRightShoulder,
  kLeftStick, kRightStick,  // pressing a stick in
  kCount
};

// Gamepad axes. Sticks are -1..1 with screen conventions: x right is +1, y
// down is +1 (like SDL and like screen pixels). Triggers are 0..1. Values are
// raw: no dead zone, because what counts as "resting" is a gameplay choice
// (Controls applies one).
enum class GamepadAxis : std::uint8_t {
  kLeftX, kLeftY, kRightX, kRightY,
  kLeftTrigger, kRightTrigger,
  kCount
};

// A point on screen in renderer output pixels. The platform layer has no Vec2
// (that lives in core), so it has its own small type.
struct PointerPosition {
  float x = 0.0f;
  float y = 0.0f;
};

// One thing that happened during a frame, in the order it happened. Games
// mostly poll Input (is_down, was_pressed); the event list is for what
// polling loses: the order of presses inside one frame, two clicks in one
// frame, and typed text. A UI layer reads or forwards these.
enum class InputEventType : std::uint8_t {
  kKeyDown,      // `key` went down (`repeat`: the OS repeating a held key)
  kKeyUp,        // `key` went up
  kText,         // `text`: UTF-8 the user typed, only while text input is on
  kPointerMove,  // the pointer is now at `x`, `y` (renderer output pixels)
  kPointerDown,  // `button` went down at `x`, `y`
  kPointerUp,    // `button` went up at `x`, `y`
  kWheel,        // `wheel` notches; positive = away from the user
  kPadDown,      // gamepad button `pad` went down
  kPadUp,        // gamepad button `pad` went up
};

// Plain data of a fixed size, so an event list never allocates. Only the
// fields its type names mean anything; the others keep their defaults.
struct InputEvent {
  static constexpr std::size_t kTextCapacity = 32;  // 31 bytes of UTF-8 and the '\0'

  InputEventType type = InputEventType::kKeyDown;
  bool repeat = false;
  Key key = Key::kUnknown;
  MouseButton button = MouseButton::kCount;
  GamepadButton pad = GamepadButton::kCount;
  float x = 0.0f;
  float y = 0.0f;
  float wheel = 0.0f;
  // kText: '\0'-terminated UTF-8, whole code points only. Longer text
  // arrives as several kText events in a row.
  std::array<char, kTextCapacity> text{};
};

// The events of one frame, in order, in fixed storage (no allocation).
// Events that do not fit are dropped and counted. Pointer moves in a row are
// merged into one (the last position), so a fast mouse cannot fill the list.
class InputEventList {
 public:
  static constexpr std::size_t kCapacity = 256;

  // Empties the list and its dropped count.
  void clear() {
    m_count = 0;
    m_dropped = 0;
  }

  // Appends `event`; a pointer move right after another one replaces it.
  // Returns false, and counts it as dropped, when the list is full.
  bool push(const InputEvent& event) {
    if (event.type == InputEventType::kPointerMove && m_count > 0 &&
        m_events[m_count - 1].type == InputEventType::kPointerMove) {
      m_events[m_count - 1].x = event.x;
      m_events[m_count - 1].y = event.y;
      return true;
    }
    if (m_count >= kCapacity) {
      ++m_dropped;
      return false;
    }
    m_events[m_count++] = event;
    return true;
  }

  // Appends `size` bytes of UTF-8 as kText events, split between code points
  // so each holds at most kTextCapacity - 1 bytes. Bytes that are not valid
  // UTF-8 (or are '\0') are skipped: text comes from outside, from an IME or
  // a WebView. Returns false if some of it did not fit (counted in dropped()).
  bool push_text(const char* utf8, std::size_t size) {
    if (utf8 == nullptr) return true;
    InputEvent event;
    event.type = InputEventType::kText;
    std::size_t used = 0;
    bool all = true;
    std::size_t i = 0;
    while (i < size) {
      const std::size_t length = utf8_sequence_length(utf8 + i, size - i);
      if (length == 0) {
        ++i;
        continue;
      }
      if (used + length > InputEvent::kTextCapacity - 1) {
        event.text[used] = '\0';
        all = push(event) && all;
        used = 0;
      }
      for (std::size_t k = 0; k < length; ++k) event.text[used++] = utf8[i + k];
      i += length;
    }
    if (used > 0) {
      event.text[used] = '\0';
      all = push(event) && all;
    }
    return all;
  }

  // Appends every event of `other` in order (merging as push() does) and
  // adds its dropped count.
  void append(const InputEventList& other) {
    for (std::size_t i = 0; i < other.m_count; ++i) push(other.m_events[i]);
    m_dropped += other.m_dropped;
  }

  std::size_t size() const { return m_count; }
  bool empty() const { return m_count == 0; }
  // Events that happened this frame but did not fit.
  std::size_t dropped() const { return m_dropped; }
  // `i` must be below size(); out of range gives the first slot, never memory outside.
  const InputEvent& operator[](std::size_t i) const { return m_events[i < m_count ? i : 0]; }
  const InputEvent* begin() const { return m_events.data(); }
  const InputEvent* end() const { return m_events.data() + m_count; }

  // Bytes in the UTF-8 sequence at `text` (1 to 4), or 0 if it is not a
  // valid one: overlong forms, surrogates, values past U+10FFFF, '\0' and
  // sequences cut short by `available` are all 0.
  static std::size_t utf8_sequence_length(const char* text, std::size_t available) {
    if (available == 0) return 0;
    const unsigned char lead = static_cast<unsigned char>(text[0]);
    if (lead < 0x80u) return lead == 0 ? 0 : 1;
    std::size_t length = 0;
    unsigned char low = 0x80u;
    unsigned char high = 0xBFu;
    if (lead >= 0xC2u && lead <= 0xDFu) {
      length = 2;
    } else if (lead >= 0xE0u && lead <= 0xEFu) {
      length = 3;
      if (lead == 0xE0u) low = 0xA0u;   // no overlong forms
      if (lead == 0xEDu) high = 0x9Fu;  // no surrogates
    } else if (lead >= 0xF0u && lead <= 0xF4u) {
      length = 4;
      if (lead == 0xF0u) low = 0x90u;
      if (lead == 0xF4u) high = 0x8Fu;  // nothing past U+10FFFF
    } else {
      return 0;
    }
    if (available < length) return 0;
    const unsigned char second = static_cast<unsigned char>(text[1]);
    if (second < low || second > high) return 0;
    for (std::size_t k = 2; k < length; ++k) {
      const unsigned char next = static_cast<unsigned char>(text[k]);
      if (next < 0x80u || next > 0xBFu) return 0;
    }
    return length;
  }

 private:
  std::array<InputEvent, kCapacity> m_events{};
  std::size_t m_count = 0;
  std::size_t m_dropped = 0;
};

// Keyboard, mouse and gamepad state for the current rendered frame. Updated
// by Window::pump_events(). Mostly polled: two clicks inside one frame count
// as one, which is fine for a game. events() has the same frame as a list in
// order, for a UI, and typed text.
//
// Gamepad: only the first connected pad is read (local multiplayer with
// several pads is out of scope). While none is connected, every button is up
// and every axis is 0.
class Input {
 public:
  virtual ~Input() = default;

  // True while the key is held.
  virtual bool is_down(Key key) const = 0;
  // True only in the frame in which the key went down.
  virtual bool was_pressed(Key key) const = 0;

  // Pointer position in renderer output pixels (the same space as
  // Renderer2D::output_width/height and View::world_to_screen), top-left
  // origin. Not window points: on a high-DPI screen those differ.
  virtual PointerPosition pointer() const = 0;
  // True while the pointer is over the window.
  virtual bool pointer_inside() const = 0;
  // True while the button is held.
  virtual bool is_down(MouseButton button) const = 0;
  // True only in the frame in which the button went down.
  virtual bool was_pressed(MouseButton button) const = 0;
  // True only in the frame in which the button went up.
  virtual bool was_released(MouseButton button) const = 0;
  // Wheel notches this frame; positive = away from the user.
  virtual float wheel() const = 0;

  // True while a gamepad is connected and being read.
  virtual bool gamepad_connected() const = 0;
  // True while the button is held.
  virtual bool is_down(GamepadButton button) const = 0;
  // True only in the frame in which the button went down. A tap shorter than
  // a frame is seen as pressed and released in the same frame.
  virtual bool was_pressed(GamepadButton button) const = 0;
  // True only in the frame in which the button went up.
  virtual bool was_released(GamepadButton button) const = 0;
  // The axis now, raw (see GamepadAxis): -1..1 for sticks, 0..1 for triggers.
  virtual float axis(GamepadAxis axis) const = 0;

  // Everything that happened this frame, in order (see InputEventType):
  // keys, text, pointer, wheel and gamepad buttons. Filled by
  // Window::pump_events(), like the polled state, and valid until the next
  // pump. Stick and trigger moves are not events (read axis()), and losing
  // focus lets go of buttons without "up" events, as the polled state does.
  virtual const InputEventList& events() const = 0;

  // Typed text (kText events) arrives only between start_text_input() and
  // stop_text_input(): a text field calls them when it gains and loses
  // focus. Off at start. On some systems starting it opens an on-screen
  // keyboard or an IME window. Key events keep coming meanwhile, so whoever
  // owns the text field also keeps those keys away from the game controls.
  virtual void start_text_input() = 0;
  virtual void stop_text_input() = 0;
  virtual bool text_input_active() const = 0;
};

}  // namespace scraps
