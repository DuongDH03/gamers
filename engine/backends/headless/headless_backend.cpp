#include "backends/headless/headless_backend.h"

namespace scraps::headless {

bool HeadlessInput::is_down(Key key) const {
  const std::size_t i = slot(key);
  return i < m_down.size() && m_down[i];
}

bool HeadlessInput::was_pressed(Key key) const {
  const std::size_t i = slot(key);
  return i < m_pressed.size() && m_pressed[i];
}

void HeadlessInput::press(Key key) {
  const std::size_t i = slot(key);
  if (i >= m_down.size()) return;
  if (!m_down[i]) m_pending_pressed[i] = true;
  m_down[i] = true;
}

void HeadlessInput::release(Key key) {
  const std::size_t i = slot(key);
  if (i >= m_down.size()) return;
  m_down[i] = false;
}

bool HeadlessInput::is_down(MouseButton button) const {
  const std::size_t i = slot(button);
  return i < m_button_down.size() && m_button_down[i];
}

bool HeadlessInput::was_pressed(MouseButton button) const {
  const std::size_t i = slot(button);
  return i < m_button_pressed.size() && m_button_pressed[i];
}

bool HeadlessInput::was_released(MouseButton button) const {
  const std::size_t i = slot(button);
  return i < m_button_released.size() && m_button_released[i];
}

void HeadlessInput::move_pointer(float x, float y) {
  m_pending_pointer.x = x;
  m_pending_pointer.y = y;
  m_pending_inside = true;
}

void HeadlessInput::press(MouseButton button) {
  const std::size_t i = slot(button);
  if (i >= m_button_down.size()) return;
  if (!m_button_down[i]) m_pending_button_pressed[i] = true;
  m_button_down[i] = true;
}

void HeadlessInput::release(MouseButton button) {
  const std::size_t i = slot(button);
  if (i >= m_button_down.size()) return;
  if (m_button_down[i]) m_pending_button_released[i] = true;
  m_button_down[i] = false;
}

void HeadlessInput::scroll(float notches) { m_pending_wheel += notches; }

void HeadlessInput::begin_frame() {
  m_pressed = m_pending_pressed;
  m_pending_pressed.fill(false);
  m_pointer = m_pending_pointer;
  m_inside = m_pending_inside;
  // A press and a release before one frame are both seen in it, so a click
  // shorter than a frame is never lost.
  m_button_pressed = m_pending_button_pressed;
  m_button_released = m_pending_button_released;
  m_pending_button_pressed.fill(false);
  m_pending_button_released.fill(false);
  m_wheel = m_pending_wheel;
  m_pending_wheel = 0.0f;
}

void HeadlessWindow::pump_events() {
  m_input.begin_frame();
  ++m_frames;
  if (m_frame_limit > 0 && m_frames >= m_frame_limit) m_close_requested = true;
}

TextureId HeadlessRenderer::create_texture(int width, int height, const std::uint8_t* rgba) {
  if (width <= 0 || height <= 0 || rgba == nullptr) return kNoTexture;
  TextureInfo info;
  info.alive = true;
  info.width = width;
  info.height = height;
  m_textures.push_back(info);
  return static_cast<TextureId>(m_textures.size());
}

void HeadlessRenderer::destroy_texture(TextureId texture) {
  if (texture == kNoTexture || texture > m_textures.size()) return;
  m_textures[texture - 1].alive = false;
}

int HeadlessRenderer::live_textures() const {
  int count = 0;
  for (const TextureInfo& info : m_textures) {
    if (info.alive) ++count;
  }
  return count;
}

bool HeadlessRenderer::texture_size(TextureId texture, int& width, int& height) const {
  if (texture == kNoTexture || texture > m_textures.size() || !m_textures[texture - 1].alive) return false;
  width = m_textures[texture - 1].width;
  height = m_textures[texture - 1].height;
  return true;
}

void HeadlessRenderer::begin_frame(Color) {
  const long frames = m_stats.frames;
  m_stats = Stats();
  m_stats.frames = frames;
  m_captured.clear();
  m_captured_textures.clear();
}

void HeadlessRenderer::fill_rect(const RectF&, Color) { ++m_stats.rects; }

void HeadlessRenderer::draw_line(float, float, float, float, Color) { ++m_stats.lines; }

void HeadlessRenderer::fill_triangles(const Vertex2D* vertices, std::size_t count, TextureId texture) {
  m_stats.triangles += count / 3;
  ++m_stats.batches;
  if (m_capture && vertices != nullptr) {
    m_captured.insert(m_captured.end(), vertices, vertices + count);
    m_captured_textures.push_back(texture);
  }
}

void HeadlessRenderer::draw_debug_text(float, float, const char* text, Color) {
  ++m_stats.texts;
  if (m_capture) m_last_text = text != nullptr ? text : "";
}

void HeadlessRenderer::end_frame() { ++m_stats.frames; }

void HeadlessAudio::submit(const float* stereo, std::size_t frames) {
  if (stereo == nullptr) return;
  m_frames += frames;
  for (std::size_t i = 0; i < frames * 2; ++i) {
    const float magnitude = stereo[i] < 0.0f ? -stereo[i] : stereo[i];
    if (magnitude > m_peak) m_peak = magnitude;
  }
}

HeadlessBackend::HeadlessBackend(int width, int height) : m_window(m_input), m_renderer(width, height) {}

PlatformServices HeadlessBackend::services() {
  PlatformServices services;
  services.window = &m_window;
  services.input = &m_input;
  services.renderer = &m_renderer;
  services.audio = &m_audio;
  return services;
}

}  // namespace scraps::headless
