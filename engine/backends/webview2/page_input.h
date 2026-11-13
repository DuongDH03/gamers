#pragma once

// Keyboard, mouse, wheel and text from the page (UI layout A). In WebView2
// the page gets the keys and the mouse first, so the backend's script
// (renderer.js) forwards DOM input to native as short text messages, and
// only while the game has the input (the view's screen is "playing"; P also
// in "paused"). Pointer positions are canvas output pixels: CSS pixels times
// devicePixelRatio, the space of Renderer2D::output_width and
// View::world_to_screen.
//
//   kd <code> <repeat 0|1>   key down; <code> is the DOM KeyboardEvent.code ("KeyW")
//   ku <code>                key up
//   tx <utf-8>               typed text (kept only while text input is on)
//   pm <x> <y>               pointer moved over the canvas
//   pd <button> <x> <y>      button down (DOM numbering: 0 left, 1 middle, 2 right)
//   pu <button> <x> <y>      button up
//   wh <notches>             wheel; positive = away from the user
//   pe <0|1>                 pointer entered (1) or left (0) the canvas
//   bl                       the page lost focus: let go of every key and button
//
// Esc is the page's (it opens and closes the menus), so it has no code here:
// "kd Escape 0" is refused like any unknown key, and the native Esc path of
// the client app never fires in scraps_web.
//
// The gamepad does not come from the page: the app pairs this input with a
// gamepad source (the sdl3 devices service) through set_gamepad().
//
// Messages are hostile data: anything malformed is refused and counted, and
// numbers must be finite and within bounds.

#include <array>
#include <cstddef>
#include <cstdint>

#include "platform/input.h"

namespace scraps::webview2 {

// The text messages the backend's page script sends (the UI's actions are
// JSON objects instead and go to the UiHost).
enum class PageMessage : std::uint8_t {
  kInput,       // one of the input messages above
  kFrame,       // "rf <generation> <acked seq> <width> <height>": the page drew and wants a frame
  kHello,       // "hi": the page's WebGL is ready; send it the shared buffer
  kProbe,       // "pr ...": self-test result (canvas pixels)
  kScreenshot,  // "sc data:image/png;base64,...": a capture of the canvas
  kUnknown,
};

PageMessage classify_page_message(const char* text, std::size_t size);

struct FrameRequest {
  std::uint32_t generation = 0;  // the shared buffer the page holds (0: none yet)
  std::uint32_t acked = 0;       // the last frame it drew (0: none in this generation)
  int width = 0;                 // its canvas, in pixels
  int height = 0;
};
// "rf <generation> <acked> <width> <height>"; width and height 1..16384.
bool parse_frame_request(const char* text, std::size_t size, FrameRequest& out);

struct ProbeResult {
  std::uint32_t views = 0;          // view messages the page received
  std::uint32_t clicked = 0;        // 1: the page clicked Play itself; 2: posted "play" without a button
  int width = 0;
  int height = 0;
  std::uint32_t colors = 0;         // distinct colours among sampled pixels
  std::uint32_t not_clear = 0;      // per mille of sampled pixels that differ from the clear colour
  std::uint32_t leaked = 0;         // backend messages a later page listener saw (must be 0: the React
                                    // bridge never sees the "" frame messages)
};
// "pr <views> <clicked> <width> <height> <colors> <not clear> <leaked>"
bool parse_probe(const char* text, std::size_t size, ProbeResult& out);

// The DOM KeyboardEvent.code for each key the engine knows, or kUnknown.
// "Escape" is deliberately not one of them (see the top).
Key key_from_dom_code(const char* code, std::size_t size);

class PageInput final : public Input {
 public:
  static constexpr std::size_t kMaxPending = 512;
  static constexpr std::size_t kMaxTextBytes = 256;

  // Parses one input message. False (counted in rejected()) if it is not a
  // valid one; nothing changes then. Applied, in order, at the next pump().
  bool queue(const char* text, std::size_t size);

  // A new frame: forgets last frame's presses, applies what was queued, and
  // takes the gamepad from the source while `focused` (the window is in the
  // foreground; a pad held in another window must not drive the game).
  void pump(bool focused);

  // Where gamepad state comes from (nullptr: none). Not owned.
  void set_gamepad(const Input* pad) { m_pad = pad; }

  std::size_t rejected() const { return m_rejected; }
  // Messages that were valid but did not fit before the next pump().
  std::size_t overflowed() const { return m_overflowed; }

  // Input
  bool is_down(Key key) const override;
  bool was_pressed(Key key) const override;
  PointerPosition pointer() const override { return m_pointer; }
  bool pointer_inside() const override { return m_inside; }
  bool is_down(MouseButton button) const override;
  bool was_pressed(MouseButton button) const override;
  bool was_released(MouseButton button) const override;
  float wheel() const override { return m_wheel; }
  bool gamepad_connected() const override { return pad() != nullptr && pad()->gamepad_connected(); }
  bool is_down(GamepadButton button) const override { return pad() != nullptr && pad()->is_down(button); }
  bool was_pressed(GamepadButton button) const override { return pad() != nullptr && pad()->was_pressed(button); }
  bool was_released(GamepadButton button) const override { return pad() != nullptr && pad()->was_released(button); }
  float axis(GamepadAxis axis) const override { return pad() != nullptr ? pad()->axis(axis) : 0.0f; }
  const InputEventList& events() const override { return m_events; }
  void start_text_input() override { m_text_input = true; }
  void stop_text_input() override { m_text_input = false; }
  bool text_input_active() const override { return m_text_input; }

 private:
  enum class Kind : std::uint8_t { kEvent, kInside, kOutside, kBlur };
  struct Pending {
    Kind kind = Kind::kEvent;
    InputEvent event;
  };

  const Input* pad() const { return m_pad_active ? m_pad : nullptr; }
  bool push_pending(const Pending& pending);
  void apply(const Pending& pending);

  std::array<Pending, kMaxPending> m_pending{};
  std::size_t m_pending_count = 0;
  std::size_t m_rejected = 0;
  std::size_t m_overflowed = 0;

  static constexpr std::size_t kKeys = static_cast<std::size_t>(Key::kCount);
  static constexpr std::size_t kButtons = static_cast<std::size_t>(MouseButton::kCount);
  std::array<bool, kKeys> m_down{};
  std::array<bool, kKeys> m_pressed{};
  std::array<bool, kButtons> m_button_down{};
  std::array<bool, kButtons> m_button_pressed{};
  std::array<bool, kButtons> m_button_released{};
  PointerPosition m_pointer;
  bool m_inside = false;
  float m_wheel = 0.0f;
  bool m_text_input = false;
  InputEventList m_events;

  const Input* m_pad = nullptr;
  bool m_pad_active = false;
};

}  // namespace scraps::webview2
