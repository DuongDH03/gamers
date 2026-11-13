#include "backends/sdl3/sdl3_backend.h"

#include <SDL3/SDL.h>

#include <climits>
#include <cmath>

namespace scraps::sdl3 {
namespace {

constexpr int kAudioSampleRate = 48000;

SDL_Scancode to_scancode(Key key) {
  switch (key) {
    case Key::kW: return SDL_SCANCODE_W;
    case Key::kA: return SDL_SCANCODE_A;
    case Key::kS: return SDL_SCANCODE_S;
    case Key::kD: return SDL_SCANCODE_D;
    case Key::kE: return SDL_SCANCODE_E;
    case Key::kQ: return SDL_SCANCODE_Q;
    case Key::kR: return SDL_SCANCODE_R;
    case Key::kV: return SDL_SCANCODE_V;
    case Key::kX: return SDL_SCANCODE_X;
    case Key::kZ: return SDL_SCANCODE_Z;
    case Key::kUp: return SDL_SCANCODE_UP;
    case Key::kDown: return SDL_SCANCODE_DOWN;
    case Key::kLeft: return SDL_SCANCODE_LEFT;
    case Key::kRight: return SDL_SCANCODE_RIGHT;
    case Key::kSpace: return SDL_SCANCODE_SPACE;
    case Key::kEnter: return SDL_SCANCODE_RETURN;
    case Key::kEscape: return SDL_SCANCODE_ESCAPE;
    case Key::kTab: return SDL_SCANCODE_TAB;
    case Key::kMinus: return SDL_SCANCODE_MINUS;
    case Key::kEquals: return SDL_SCANCODE_EQUALS;
    case Key::k1: return SDL_SCANCODE_1;
    case Key::k2: return SDL_SCANCODE_2;
    case Key::k3: return SDL_SCANCODE_3;
    case Key::kF1: return SDL_SCANCODE_F1;
    case Key::kF2: return SDL_SCANCODE_F2;
    case Key::kF3: return SDL_SCANCODE_F3;
    case Key::kP: return SDL_SCANCODE_P;
    case Key::kF5: return SDL_SCANCODE_F5;
    case Key::kF9: return SDL_SCANCODE_F9;
    default: return SDL_SCANCODE_UNKNOWN;
  }
}

Key from_scancode(SDL_Scancode scancode) {
  for (std::size_t i = 1; i < static_cast<std::size_t>(Key::kCount); ++i) {
    const Key key = static_cast<Key>(i);
    if (to_scancode(key) == scancode) return key;
  }
  return Key::kUnknown;
}

// SDL numbers buttons 1 (left), 2 (middle), 3 (right); others are ignored.
bool to_mouse_button(Uint8 sdl_button, std::size_t& slot) {
  switch (sdl_button) {
    case SDL_BUTTON_LEFT: slot = static_cast<std::size_t>(MouseButton::kLeft); return true;
    case SDL_BUTTON_RIGHT: slot = static_cast<std::size_t>(MouseButton::kRight); return true;
    case SDL_BUTTON_MIDDLE: slot = static_cast<std::size_t>(MouseButton::kMiddle); return true;
    default: return false;
  }
}

SDL_FColor to_fcolor(Color color) {
  SDL_FColor result;
  result.r = static_cast<float>(color.r) / 255.0f;
  result.g = static_cast<float>(color.g) / 255.0f;
  result.b = static_cast<float>(color.b) / 255.0f;
  result.a = static_cast<float>(color.a) / 255.0f;
  return result;
}

}  // namespace

// Defined here, where SDL_Vertex is a complete type.
Sdl3Platform::Sdl3Platform(Passkey) {}

std::unique_ptr<Sdl3Platform> Sdl3Platform::create(const WindowDesc& desc, std::string& error) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    error = std::string("SDL_Init failed: ") + SDL_GetError();
    return nullptr;
  }

  auto platform = std::make_unique<Sdl3Platform>(Passkey());
  if (!SDL_CreateWindowAndRenderer(desc.title, desc.width, desc.height, SDL_WINDOW_RESIZABLE, &platform->m_window,
                                   &platform->m_renderer)) {
    error = std::string("SDL_CreateWindowAndRenderer failed: ") + SDL_GetError();
    return nullptr;  // the destructor shuts SDL down
  }
  SDL_SetRenderVSync(platform->m_renderer, desc.vsync ? 1 : 0);
  SDL_SetRenderDrawBlendMode(platform->m_renderer, SDL_BLENDMODE_BLEND);
  // A window that opens under the cursor gets no "enter" event until it moves.
  platform->m_pointer_inside = SDL_GetMouseFocus() == platform->m_window;

  // Audio is optional: no device must not stop the game.
  if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;  // interleaved stereo, as the mixer produces (core/sound.h)
    spec.freq = kAudioSampleRate;
    platform->m_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (platform->m_audio != nullptr) SDL_ResumeAudioStreamDevice(platform->m_audio);
  }
  return platform;
}

Sdl3Platform::~Sdl3Platform() {
  for (SDL_Texture* texture : m_textures) {
    if (texture != nullptr) SDL_DestroyTexture(texture);
  }
  if (m_audio != nullptr) SDL_DestroyAudioStream(m_audio);
  if (m_renderer != nullptr) SDL_DestroyRenderer(m_renderer);
  if (m_window != nullptr) SDL_DestroyWindow(m_window);
  SDL_Quit();
}

PlatformServices Sdl3Platform::services() {
  PlatformServices services;
  services.window = this;
  services.input = this;
  services.renderer = this;
  services.audio = this;
  return services;
}

// ---------------------------------------------------------------- Window

void Sdl3Platform::pump_events() {
  m_pressed.fill(false);
  m_button_pressed.fill(false);
  m_button_released.fill(false);
  m_wheel = 0.0f;
  SDL_Event event;
  std::size_t button = 0;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        m_close_requested = true;
        break;
      case SDL_EVENT_KEY_DOWN:
        if (!event.key.repeat) {
          const Key key = from_scancode(event.key.scancode);
          if (key != Key::kUnknown) m_pressed[static_cast<std::size_t>(key)] = true;
        }
        break;
      case SDL_EVENT_MOUSE_MOTION:
        set_pointer(event.motion.x, event.motion.y);
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        set_pointer(event.button.x, event.button.y);
        if (to_mouse_button(event.button.button, button)) {
          m_button_pressed[button] = true;
          m_button_down[button] = true;
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        set_pointer(event.button.x, event.button.y);
        if (to_mouse_button(event.button.button, button)) {
          m_button_released[button] = true;
          m_button_down[button] = false;
        }
        break;
      case SDL_EVENT_MOUSE_WHEEL: {
        // "Natural" scrolling reports flipped values; undo it so positive
        // always means away from the user.
        const float notches = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
        if (std::isfinite(notches)) m_wheel += notches;
        break;
      }
      case SDL_EVENT_WINDOW_MOUSE_ENTER:
        m_pointer_inside = true;
        break;
      case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        m_pointer_inside = false;
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        // A button released while another window has focus never reports
        // its release here; do not leave it stuck down.
        m_button_down.fill(false);
        break;
      default:
        break;
    }
  }
}

void Sdl3Platform::set_pointer(float window_x, float window_y) {
  float x = 0.0f;
  float y = 0.0f;
  if (!SDL_RenderCoordinatesFromWindow(m_renderer, window_x, window_y, &x, &y)) return;
  if (!std::isfinite(x) || !std::isfinite(y)) return;
  m_pointer.x = x;
  m_pointer.y = y;
}

void Sdl3Platform::set_title(const char* title) { SDL_SetWindowTitle(m_window, title); }

// ---------------------------------------------------------------- Input

bool Sdl3Platform::is_down(Key key) const {
  const SDL_Scancode scancode = to_scancode(key);
  if (scancode == SDL_SCANCODE_UNKNOWN) return false;
  int count = 0;
  const bool* state = SDL_GetKeyboardState(&count);
  return state != nullptr && static_cast<int>(scancode) < count && state[scancode];
}

bool Sdl3Platform::was_pressed(Key key) const {
  const std::size_t i = static_cast<std::size_t>(key);
  return i < m_pressed.size() && m_pressed[i];
}

bool Sdl3Platform::is_down(MouseButton button) const {
  const std::size_t i = static_cast<std::size_t>(button);
  return i < m_button_down.size() && m_button_down[i];
}

bool Sdl3Platform::was_pressed(MouseButton button) const {
  const std::size_t i = static_cast<std::size_t>(button);
  return i < m_button_pressed.size() && m_button_pressed[i];
}

bool Sdl3Platform::was_released(MouseButton button) const {
  const std::size_t i = static_cast<std::size_t>(button);
  return i < m_button_released.size() && m_button_released[i];
}

// ---------------------------------------------------------------- Renderer2D

void Sdl3Platform::set_draw_color(Color color) {
  SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
}

void Sdl3Platform::begin_frame(Color clear_color) {
  set_draw_color(clear_color);
  SDL_RenderClear(m_renderer);
}

void Sdl3Platform::fill_rect(const RectF& rect, Color color) {
  set_draw_color(color);
  SDL_FRect r;
  r.x = rect.x;
  r.y = rect.y;
  r.w = rect.w;
  r.h = rect.h;
  SDL_RenderFillRect(m_renderer, &r);
}

void Sdl3Platform::draw_line(float x0, float y0, float x1, float y1, Color color) {
  set_draw_color(color);
  SDL_RenderLine(m_renderer, x0, y0, x1, y1);
}

TextureId Sdl3Platform::create_texture(int width, int height, const std::uint8_t* rgba) {
  if (width <= 0 || height <= 0 || rgba == nullptr) return kNoTexture;
  // RGBA32 means the bytes in memory are R, G, B, A, which is what stb_image gives.
  SDL_Texture* texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
  if (texture == nullptr) return kNoTexture;
  if (!SDL_UpdateTexture(texture, nullptr, rgba, width * 4)) {
    SDL_DestroyTexture(texture);
    return kNoTexture;
  }
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);  // crisp pixel art when scaled up
  m_textures.push_back(texture);
  return static_cast<TextureId>(m_textures.size());
}

void Sdl3Platform::destroy_texture(TextureId texture) {
  if (texture == kNoTexture || texture > m_textures.size()) return;
  SDL_Texture*& slot = m_textures[texture - 1];
  if (slot != nullptr) SDL_DestroyTexture(slot);
  slot = nullptr;
}

void Sdl3Platform::fill_triangles(const Vertex2D* vertices, std::size_t count, TextureId texture) {
  count -= count % 3;
  if (vertices == nullptr || count == 0 || count > static_cast<std::size_t>(INT_MAX)) return;
  SDL_Texture* sdl_texture = nullptr;
  if (texture != kNoTexture && texture <= m_textures.size()) sdl_texture = m_textures[texture - 1];
  m_vertices.resize(count);  // grows to the largest frame once, then reused
  for (std::size_t i = 0; i < count; ++i) {
    SDL_Vertex& v = m_vertices[i];
    v.position.x = vertices[i].x;
    v.position.y = vertices[i].y;
    v.color = to_fcolor(vertices[i].color);
    v.tex_coord.x = vertices[i].u;
    v.tex_coord.y = vertices[i].v;
  }
  SDL_RenderGeometry(m_renderer, sdl_texture, m_vertices.data(), static_cast<int>(count), nullptr, 0);
}

void Sdl3Platform::draw_debug_text(float x, float y, const char* text, Color color) {
  if (text == nullptr) return;
  set_draw_color(color);
  SDL_RenderDebugText(m_renderer, x, y, text);
}

void Sdl3Platform::end_frame() { SDL_RenderPresent(m_renderer); }

int Sdl3Platform::output_width() const {
  int w = 0;
  int h = 0;
  SDL_GetRenderOutputSize(m_renderer, &w, &h);
  return w;
}

int Sdl3Platform::output_height() const {
  int w = 0;
  int h = 0;
  SDL_GetRenderOutputSize(m_renderer, &w, &h);
  return h;
}

// ---------------------------------------------------------------- Audio

void Sdl3Platform::submit(const float* stereo, std::size_t frames) {
  if (m_audio == nullptr || stereo == nullptr || frames == 0) return;
  SDL_PutAudioStreamData(m_audio, stereo, static_cast<int>(frames * 2 * sizeof(float)));
}

std::size_t Sdl3Platform::queued_frames() const {
  if (m_audio == nullptr) return 0;
  const int bytes = SDL_GetAudioStreamQueued(m_audio);
  return bytes > 0 ? static_cast<std::size_t>(bytes) / (2 * sizeof(float)) : 0;
}

std::string preferences_path(const char* organization, const char* application) {
  char* path = SDL_GetPrefPath(organization, application);
  if (path == nullptr) return std::string();
  std::string result(path);
  SDL_free(path);
  return result;
}

}  // namespace scraps::sdl3
