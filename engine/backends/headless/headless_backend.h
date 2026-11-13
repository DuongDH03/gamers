#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "platform/audio.h"
#include "platform/input.h"
#include "platform/renderer2d.h"
#include "platform/services.h"
#include "platform/window.h"

// The headless backend: no window, no drawing, no sound. It runs the full
// engine for the dedicated server and for automated tests, and it keeps the
// platform interfaces honest (Rule 3): anything that only works with SDL is a
// leak in the abstraction.
namespace scraps::headless {

// Keyboard, mouse and gamepad state driven by code. press()/release(),
// move_pointer(), scroll(), set_axis() and connect_gamepad() take effect at
// the next Window::pump_events(), exactly like real events: a press, a
// release or a scroll is seen for one frame.
class HeadlessInput final : public Input {
 public:
  bool is_down(Key key) const override;
  bool was_pressed(Key key) const override;

  PointerPosition pointer() const override { return m_pointer; }
  bool pointer_inside() const override { return m_inside; }
  bool is_down(MouseButton button) const override;
  bool was_pressed(MouseButton button) const override;
  bool was_released(MouseButton button) const override;
  float wheel() const override { return m_wheel; }

  bool gamepad_connected() const override { return m_pad_connected; }
  bool is_down(GamepadButton button) const override;
  bool was_pressed(GamepadButton button) const override;
  bool was_released(GamepadButton button) const override;
  float axis(GamepadAxis axis) const override;

  void press(Key key);
  void release(Key key);
  // In renderer output pixels. Also puts the pointer inside the window.
  void move_pointer(float x, float y);
  void press(MouseButton button);
  void release(MouseButton button);
  void scroll(float notches);
  // Plugs a gamepad in (true) or pulls it out (false). Pulling it out lets go
  // of its buttons and centres its axes. While no pad is connected, gamepad
  // presses and axis moves are ignored, as they would be on a real machine.
  void connect_gamepad(bool connected);
  void press(GamepadButton button);
  void release(GamepadButton button);
  // Clamped to the axis's range (-1..1, triggers 0..1); not finite counts as 0.
  void set_axis(GamepadAxis axis, float value);
  void begin_frame();  // called by HeadlessWindow::pump_events()

 private:
  static constexpr std::size_t kButtons = static_cast<std::size_t>(MouseButton::kCount);
  static constexpr std::size_t kPadButtons = static_cast<std::size_t>(GamepadButton::kCount);
  static constexpr std::size_t kPadAxes = static_cast<std::size_t>(GamepadAxis::kCount);
  static std::size_t slot(Key key) { return static_cast<std::size_t>(key); }
  static std::size_t slot(MouseButton button) { return static_cast<std::size_t>(button); }
  static std::size_t slot(GamepadButton button) { return static_cast<std::size_t>(button); }
  static std::size_t slot(GamepadAxis axis) { return static_cast<std::size_t>(axis); }

  std::array<bool, static_cast<std::size_t>(Key::kCount)> m_down{};
  std::array<bool, static_cast<std::size_t>(Key::kCount)> m_pending_pressed{};
  std::array<bool, static_cast<std::size_t>(Key::kCount)> m_pressed{};

  PointerPosition m_pending_pointer;
  PointerPosition m_pointer;
  bool m_pending_inside = false;
  bool m_inside = false;
  std::array<bool, kButtons> m_button_down{};
  std::array<bool, kButtons> m_pending_button_pressed{};
  std::array<bool, kButtons> m_pending_button_released{};
  std::array<bool, kButtons> m_button_pressed{};
  std::array<bool, kButtons> m_button_released{};
  float m_pending_wheel = 0.0f;
  float m_wheel = 0.0f;

  // Gamepad: the pending_ values are what code set since the last frame; the
  // others are what this frame reports.
  bool m_pending_pad_connected = false;
  bool m_pad_connected = false;
  std::array<bool, kPadButtons> m_pending_pad_down{};
  std::array<bool, kPadButtons> m_pending_pad_pressed{};
  std::array<bool, kPadButtons> m_pending_pad_released{};
  std::array<bool, kPadButtons> m_pad_down{};
  std::array<bool, kPadButtons> m_pad_pressed{};
  std::array<bool, kPadButtons> m_pad_released{};
  std::array<float, kPadAxes> m_pending_axes{};
  std::array<float, kPadAxes> m_axes{};
};

class HeadlessWindow final : public Window {
 public:
  explicit HeadlessWindow(HeadlessInput& input) : m_input(input) {}

  void pump_events() override;
  bool close_requested() const override { return m_close_requested; }
  void set_title(const char* title) override { m_title = title != nullptr ? title : ""; }

  void request_close() { m_close_requested = true; }
  // Requests close automatically after this many pump_events() calls (0 = never).
  void set_frame_limit(long frames) { m_frame_limit = frames; }
  long frames() const { return m_frames; }
  const std::string& title() const { return m_title; }

 private:
  HeadlessInput& m_input;
  bool m_close_requested = false;
  long m_frame_limit = 0;
  long m_frames = 0;
  std::string m_title;
};

// Counts what would have been drawn. Optionally keeps the triangles of the
// current frame so tests can check draw order.
class HeadlessRenderer final : public Renderer2D {
 public:
  struct Stats {
    long frames = 0;
    std::size_t triangles = 0;  // in the last frame
    std::size_t rects = 0;      // in the last frame
    std::size_t lines = 0;      // in the last frame
    std::size_t texts = 0;      // in the last frame
    std::size_t batches = 0;    // fill_triangles calls in the last frame
  };

  HeadlessRenderer(int width, int height) : m_width(width), m_height(height) {}

  // Textures are remembered by size only; nothing is drawn.
  TextureId create_texture(int width, int height, const std::uint8_t* rgba) override;
  void destroy_texture(TextureId texture) override;
  void begin_frame(Color clear_color) override;
  void fill_rect(const RectF& rect, Color color) override;
  void draw_line(float x0, float y0, float x1, float y1, Color color) override;
  void fill_triangles(const Vertex2D* vertices, std::size_t count, TextureId texture) override;
  void draw_debug_text(float x, float y, const char* text, Color color) override;
  void end_frame() override;
  int output_width() const override { return m_width; }
  int output_height() const override { return m_height; }

  const Stats& stats() const { return m_stats; }
  void set_capture(bool capture) { m_capture = capture; }
  const std::vector<Vertex2D>& captured_vertices() const { return m_captured; }
  // The texture of each fill_triangles call this frame, in order (when capturing).
  const std::vector<TextureId>& captured_batch_textures() const { return m_captured_textures; }
  const std::string& last_text() const { return m_last_text; }
  int live_textures() const;
  bool texture_size(TextureId texture, int& width, int& height) const;

 private:
  struct TextureInfo {
    bool alive = false;
    int width = 0;
    int height = 0;
  };

  int m_width;
  int m_height;
  Stats m_stats;
  bool m_capture = false;
  std::vector<Vertex2D> m_captured;
  std::vector<TextureId> m_captured_textures;
  std::vector<TextureInfo> m_textures;  // index = TextureId - 1
  std::string m_last_text;
};

// No device: counts what it is given, plays nothing, and reports nothing
// queued (so a caller keeps mixing as it would with a real device).
class HeadlessAudio final : public Audio {
 public:
  void submit(const float* stereo, std::size_t frames) override;
  std::size_t queued_frames() const override { return 0; }
  std::size_t frames_submitted() const { return m_frames; }
  // Largest absolute sample seen: lets tests hear that something played.
  float peak() const { return m_peak; }

 private:
  std::size_t m_frames = 0;
  float m_peak = 0.0f;
};

// Owns one of each and hands them out as PlatformServices.
class HeadlessBackend {
 public:
  explicit HeadlessBackend(int width = 1280, int height = 720);

  PlatformServices services();
  HeadlessInput& input() { return m_input; }
  HeadlessWindow& window() { return m_window; }
  HeadlessRenderer& renderer() { return m_renderer; }
  HeadlessAudio& audio() { return m_audio; }

 private:
  HeadlessInput m_input;
  HeadlessWindow m_window;  // declared after m_input, which it refers to
  HeadlessRenderer m_renderer;
  HeadlessAudio m_audio;
};

}  // namespace scraps::headless
