#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "platform/audio.h"
#include "platform/input.h"
#include "platform/renderer2d.h"
#include "platform/services.h"
#include "platform/window.h"

// SDL types are only forward-declared here, so nothing that includes this
// header sees SDL. Only sdl3_backend.cpp includes SDL headers (Rule 3).
struct SDL_Window;
struct SDL_Renderer;
struct SDL_AudioStream;
struct SDL_Texture;
struct SDL_Vertex;

namespace scraps::sdl3 {

// The real backend for players: one SDL window with an SDL_Renderer, the
// keyboard, and an audio stream. Implements all four platform interfaces.
class Sdl3Platform final : public Window, public Input, public Renderer2D, public Audio {
  struct Passkey {
    explicit Passkey() = default;
  };

 public:
  // Only create() can make a Passkey, so only create() can construct.
  explicit Sdl3Platform(Passkey);

  // Returns nullptr and sets `error` if SDL or the window cannot start.
  static std::unique_ptr<Sdl3Platform> create(const WindowDesc& desc, std::string& error);
  ~Sdl3Platform() override;

  Sdl3Platform(const Sdl3Platform&) = delete;
  Sdl3Platform& operator=(const Sdl3Platform&) = delete;

  PlatformServices services();

  // Window
  void pump_events() override;
  bool close_requested() const override { return m_close_requested; }
  void set_title(const char* title) override;

  // Input
  bool is_down(Key key) const override;
  bool was_pressed(Key key) const override;
  PointerPosition pointer() const override { return m_pointer; }
  bool pointer_inside() const override { return m_pointer_inside; }
  bool is_down(MouseButton button) const override;
  bool was_pressed(MouseButton button) const override;
  bool was_released(MouseButton button) const override;
  float wheel() const override { return m_wheel; }

  // Renderer2D
  TextureId create_texture(int width, int height, const std::uint8_t* rgba) override;
  void destroy_texture(TextureId texture) override;
  void begin_frame(Color clear_color) override;
  void fill_rect(const RectF& rect, Color color) override;
  void draw_line(float x0, float y0, float x1, float y1, Color color) override;
  void fill_triangles(const Vertex2D* vertices, std::size_t count, TextureId texture) override;
  void draw_debug_text(float x, float y, const char* text, Color color) override;
  void end_frame() override;
  int output_width() const override;
  int output_height() const override;

  // Audio
  void submit(const float* stereo, std::size_t frames) override;
  std::size_t queued_frames() const override;

 private:
  void set_draw_color(Color color);
  // Window points (from an event) to renderer output pixels.
  void set_pointer(float window_x, float window_y);

  SDL_Window* m_window = nullptr;
  SDL_Renderer* m_renderer = nullptr;
  SDL_AudioStream* m_audio = nullptr;
  bool m_close_requested = false;
  std::array<bool, static_cast<std::size_t>(Key::kCount)> m_pressed{};
  static constexpr std::size_t kButtons = static_cast<std::size_t>(MouseButton::kCount);
  PointerPosition m_pointer;
  bool m_pointer_inside = false;
  std::array<bool, kButtons> m_button_down{};
  std::array<bool, kButtons> m_button_pressed{};
  std::array<bool, kButtons> m_button_released{};
  float m_wheel = 0.0f;
  std::vector<SDL_Vertex> m_vertices;  // reused conversion buffer
  std::vector<SDL_Texture*> m_textures;  // index = TextureId - 1; nullptr once destroyed
};

}  // namespace scraps::sdl3
