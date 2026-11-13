#include "backends/headless/headless_backend.h"

#include <cmath>
#include <cstring>

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
  if (!m_down[i]) {
    m_pending_pressed[i] = true;
    InputEvent event;
    event.type = InputEventType::kKeyDown;
    event.key = key;
    m_pending_events.push(event);
  }
  m_down[i] = true;
}

void HeadlessInput::release(Key key) {
  const std::size_t i = slot(key);
  if (i >= m_down.size()) return;
  if (m_down[i]) {
    InputEvent event;
    event.type = InputEventType::kKeyUp;
    event.key = key;
    m_pending_events.push(event);
  }
  m_down[i] = false;
}

bool HeadlessInput::type_text(const char* utf8) {
  if (!m_text_input || utf8 == nullptr) return false;
  return m_pending_events.push_text(utf8, std::strlen(utf8));
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
  InputEvent event;
  event.type = InputEventType::kPointerMove;
  event.x = x;
  event.y = y;
  m_pending_events.push(event);
}

void HeadlessInput::press(MouseButton button) {
  const std::size_t i = slot(button);
  if (i >= m_button_down.size()) return;
  if (!m_button_down[i]) {
    m_pending_button_pressed[i] = true;
    InputEvent event;
    event.type = InputEventType::kPointerDown;
    event.button = button;
    event.x = m_pending_pointer.x;
    event.y = m_pending_pointer.y;
    m_pending_events.push(event);
  }
  m_button_down[i] = true;
}

void HeadlessInput::release(MouseButton button) {
  const std::size_t i = slot(button);
  if (i >= m_button_down.size()) return;
  if (m_button_down[i]) {
    m_pending_button_released[i] = true;
    InputEvent event;
    event.type = InputEventType::kPointerUp;
    event.button = button;
    event.x = m_pending_pointer.x;
    event.y = m_pending_pointer.y;
    m_pending_events.push(event);
  }
  m_button_down[i] = false;
}

void HeadlessInput::scroll(float notches) {
  m_pending_wheel += notches;
  InputEvent event;
  event.type = InputEventType::kWheel;
  event.wheel = notches;
  m_pending_events.push(event);
}

bool HeadlessInput::is_down(GamepadButton button) const {
  const std::size_t i = slot(button);
  return i < m_pad_down.size() && m_pad_down[i];
}

bool HeadlessInput::was_pressed(GamepadButton button) const {
  const std::size_t i = slot(button);
  return i < m_pad_pressed.size() && m_pad_pressed[i];
}

bool HeadlessInput::was_released(GamepadButton button) const {
  const std::size_t i = slot(button);
  return i < m_pad_released.size() && m_pad_released[i];
}

float HeadlessInput::axis(GamepadAxis axis) const {
  const std::size_t i = slot(axis);
  return i < m_axes.size() ? m_axes[i] : 0.0f;
}

void HeadlessInput::connect_gamepad(bool connected) {
  if (!connected) {
    // Like a real pad pulled out: whatever was held is let go, silently (no
    // "released" either, there is no pad left to have released it).
    m_pending_pad_down.fill(false);
    m_pending_pad_pressed.fill(false);
    m_pending_pad_released.fill(false);
    m_pending_axes.fill(0.0f);
  }
  m_pending_pad_connected = connected;
}

void HeadlessInput::press(GamepadButton button) {
  const std::size_t i = slot(button);
  if (!m_pending_pad_connected || i >= m_pending_pad_down.size()) return;
  if (!m_pending_pad_down[i]) {
    m_pending_pad_pressed[i] = true;
    InputEvent event;
    event.type = InputEventType::kPadDown;
    event.pad = button;
    m_pending_events.push(event);
  }
  m_pending_pad_down[i] = true;
}

void HeadlessInput::release(GamepadButton button) {
  const std::size_t i = slot(button);
  if (!m_pending_pad_connected || i >= m_pending_pad_down.size()) return;
  if (m_pending_pad_down[i]) {
    m_pending_pad_released[i] = true;
    InputEvent event;
    event.type = InputEventType::kPadUp;
    event.pad = button;
    m_pending_events.push(event);
  }
  m_pending_pad_down[i] = false;
}

void HeadlessInput::set_axis(GamepadAxis axis, float value) {
  const std::size_t i = slot(axis);
  if (!m_pending_pad_connected || i >= m_pending_axes.size()) return;
  const bool trigger = axis == GamepadAxis::kLeftTrigger || axis == GamepadAxis::kRightTrigger;
  const float low = trigger ? 0.0f : -1.0f;
  if (!std::isfinite(value)) value = 0.0f;
  m_pending_axes[i] = value < low ? low : (value > 1.0f ? 1.0f : value);
}

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
  // The gamepad, the same way: a tap inside one frame is both pressed and
  // released in it. Held buttons and axes carry over.
  m_pad_connected = m_pending_pad_connected;
  m_pad_down = m_pending_pad_down;
  m_pad_pressed = m_pending_pad_pressed;
  m_pad_released = m_pending_pad_released;
  m_pending_pad_pressed.fill(false);
  m_pending_pad_released.fill(false);
  m_axes = m_pending_axes;
  // The events of what happened since the last frame, in order.
  m_events.clear();
  m_events.append(m_pending_events);
  m_pending_events.clear();
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

bool HeadlessUi::post(const char* json, std::size_t size) {
  if (!m_present || (json == nullptr && size > 0)) return false;
  m_posts.emplace_back(json != nullptr ? json : "", size);
  return true;
}

void HeadlessUi::receive(UiMessageList& out) {
  out.clear();
  out.append(m_inbox);
  m_inbox.clear();
}

bool HeadlessUi::inject(const char* json, std::size_t size) { return m_inbox.push(json, size); }

bool HeadlessUi::inject(const char* json) { return json != nullptr && inject(json, std::strlen(json)); }

const std::string& HeadlessUi::last_post() const { return m_posts.empty() ? m_none : m_posts.back(); }

HeadlessBackend::HeadlessBackend(int width, int height) : m_window(m_input), m_renderer(width, height) {}

PlatformServices HeadlessBackend::services() {
  PlatformServices services;
  services.window = &m_window;
  services.input = &m_input;
  services.renderer = &m_renderer;
  services.audio = &m_audio;
  services.ui = &m_ui;
  return services;
}

}  // namespace scraps::headless
