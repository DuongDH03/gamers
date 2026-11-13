#include "backends/sdl3/sdl3_backend.h"

#include <SDL3/SDL.h>

#include <climits>
#include <cmath>
#include <cstring>

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
    case Key::kBackspace: return SDL_SCANCODE_BACKSPACE;
    case Key::kDelete: return SDL_SCANCODE_DELETE;
    case Key::kHome: return SDL_SCANCODE_HOME;
    case Key::kEnd: return SDL_SCANCODE_END;
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

// SDL's gamepad buttons are positional too (SOUTH is the bottom face
// button), so the names map one to one. Buttons the engine does not know
// (guide, paddles, touchpad, misc) are ignored.
bool to_pad_button(Uint8 sdl_button, std::size_t& slot) {
  GamepadButton button = GamepadButton::kCount;
  switch (static_cast<SDL_GamepadButton>(sdl_button)) {
    case SDL_GAMEPAD_BUTTON_SOUTH: button = GamepadButton::kSouth; break;
    case SDL_GAMEPAD_BUTTON_EAST: button = GamepadButton::kEast; break;
    case SDL_GAMEPAD_BUTTON_WEST: button = GamepadButton::kWest; break;
    case SDL_GAMEPAD_BUTTON_NORTH: button = GamepadButton::kNorth; break;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: button = GamepadButton::kDpadUp; break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: button = GamepadButton::kDpadDown; break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: button = GamepadButton::kDpadLeft; break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: button = GamepadButton::kDpadRight; break;
    case SDL_GAMEPAD_BUTTON_START: button = GamepadButton::kStart; break;
    case SDL_GAMEPAD_BUTTON_BACK: button = GamepadButton::kBack; break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: button = GamepadButton::kLeftShoulder; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: button = GamepadButton::kRightShoulder; break;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: button = GamepadButton::kLeftStick; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: button = GamepadButton::kRightStick; break;
    default: return false;
  }
  slot = static_cast<std::size_t>(button);
  return true;
}

SDL_GamepadButton to_sdl_pad_button(GamepadButton button) {
  switch (button) {
    case GamepadButton::kSouth: return SDL_GAMEPAD_BUTTON_SOUTH;
    case GamepadButton::kEast: return SDL_GAMEPAD_BUTTON_EAST;
    case GamepadButton::kWest: return SDL_GAMEPAD_BUTTON_WEST;
    case GamepadButton::kNorth: return SDL_GAMEPAD_BUTTON_NORTH;
    case GamepadButton::kDpadUp: return SDL_GAMEPAD_BUTTON_DPAD_UP;
    case GamepadButton::kDpadDown: return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    case GamepadButton::kDpadLeft: return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    case GamepadButton::kDpadRight: return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    case GamepadButton::kStart: return SDL_GAMEPAD_BUTTON_START;
    case GamepadButton::kBack: return SDL_GAMEPAD_BUTTON_BACK;
    case GamepadButton::kLeftShoulder: return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    case GamepadButton::kRightShoulder: return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    case GamepadButton::kLeftStick: return SDL_GAMEPAD_BUTTON_LEFT_STICK;
    case GamepadButton::kRightStick: return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    default: return SDL_GAMEPAD_BUTTON_INVALID;
  }
}

// GamepadAxis is in SDL's order (left x/y, right x/y, left/right trigger);
// anything SDL may add later is ignored.
bool to_pad_axis(Uint8 sdl_axis, std::size_t& slot) {
  if (sdl_axis >= static_cast<Uint8>(GamepadAxis::kCount)) return false;
  slot = sdl_axis;
  return true;
}

// Sticks are -32768..32767, triggers 0..32767: both become -1..1 (0..1).
float axis_value(Sint16 value) {
  const float v = static_cast<float>(value) / 32767.0f;
  return v < -1.0f ? -1.0f : v;
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
  // Gamepads are optional too. A pad already plugged in is opened now; one
  // plugged in later arrives as SDL_EVENT_GAMEPAD_ADDED.
  if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) platform->open_any_gamepad();
  return platform;
}

Sdl3Platform::~Sdl3Platform() {
  for (SDL_Texture* texture : m_textures) {
    if (texture != nullptr) SDL_DestroyTexture(texture);
  }
  close_gamepad();
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
  m_pad_pressed.fill(false);
  m_pad_released.fill(false);
  m_events.clear();
  SDL_Event event;
  std::size_t button = 0;
  InputEvent item;  // the event list's entry for the SDL event at hand
  while (SDL_PollEvent(&event)) {
    item = InputEvent();
    switch (event.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        m_close_requested = true;
        break;
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP: {
        const Key key = from_scancode(event.key.scancode);
        if (key == Key::kUnknown) break;
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) m_pressed[static_cast<std::size_t>(key)] = true;
        item.type = event.type == SDL_EVENT_KEY_DOWN ? InputEventType::kKeyDown : InputEventType::kKeyUp;
        item.key = key;
        item.repeat = event.type == SDL_EVENT_KEY_DOWN && event.key.repeat;
        m_events.push(item);
        break;
      }
      case SDL_EVENT_TEXT_INPUT:
        // Only arrives between SDL_StartTextInput and SDL_StopTextInput.
        if (event.text.text != nullptr) m_events.push_text(event.text.text, std::strlen(event.text.text));
        break;
      case SDL_EVENT_MOUSE_MOTION:
        set_pointer(event.motion.x, event.motion.y);
        item.type = InputEventType::kPointerMove;
        item.x = m_pointer.x;
        item.y = m_pointer.y;
        m_events.push(item);
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        set_pointer(event.button.x, event.button.y);
        if (to_mouse_button(event.button.button, button)) {
          m_button_pressed[button] = true;
          m_button_down[button] = true;
          item.type = InputEventType::kPointerDown;
          item.button = static_cast<MouseButton>(button);
          item.x = m_pointer.x;
          item.y = m_pointer.y;
          m_events.push(item);
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        set_pointer(event.button.x, event.button.y);
        if (to_mouse_button(event.button.button, button)) {
          m_button_released[button] = true;
          m_button_down[button] = false;
          item.type = InputEventType::kPointerUp;
          item.button = static_cast<MouseButton>(button);
          item.x = m_pointer.x;
          item.y = m_pointer.y;
          m_events.push(item);
        }
        break;
      case SDL_EVENT_MOUSE_WHEEL: {
        // "Natural" scrolling reports flipped values; undo it so positive
        // always means away from the user.
        const float notches = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
        if (std::isfinite(notches)) {
          m_wheel += notches;
          item.type = InputEventType::kWheel;
          item.wheel = notches;
          m_events.push(item);
        }
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
        // its release here; do not leave it stuck down. The same for the
        // gamepad: SDL sends no pad events to a window in the background.
        m_button_down.fill(false);
        clear_gamepad_state();
        break;
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
        // Whatever is held now (a stick pushed while switching back) counts,
        // without a "pressed": that press happened elsewhere.
        read_gamepad_state();
        break;
      case SDL_EVENT_GAMEPAD_ADDED:
        if (m_pad == nullptr) open_any_gamepad();
        break;
      case SDL_EVENT_GAMEPAD_REMOVED:
        if (m_pad != nullptr && event.gdevice.which == m_pad_id) {
          close_gamepad();
          open_any_gamepad();  // another pad that is still plugged in takes over
        }
        break;
      case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (m_pad != nullptr && event.gbutton.which == m_pad_id && to_pad_button(event.gbutton.button, button)) {
          if (event.gbutton.down) {
            if (!m_pad_down[button]) {
              m_pad_pressed[button] = true;
              item.type = InputEventType::kPadDown;
              item.pad = static_cast<GamepadButton>(button);
              m_events.push(item);
            }
          } else if (m_pad_down[button]) {
            m_pad_released[button] = true;
            item.type = InputEventType::kPadUp;
            item.pad = static_cast<GamepadButton>(button);
            m_events.push(item);
          }
          m_pad_down[button] = event.gbutton.down;
        }
        break;
      case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (m_pad != nullptr && event.gaxis.which == m_pad_id && to_pad_axis(event.gaxis.axis, button)) {
          m_axes[button] = axis_value(event.gaxis.value);
        }
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

bool Sdl3Platform::open_any_gamepad() {
  if (m_pad != nullptr) return true;
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);  // allocates; only when a pad comes or goes
  if (ids == nullptr) return false;
  for (int i = 0; i < count && m_pad == nullptr; ++i) {
    m_pad = SDL_OpenGamepad(ids[i]);
    if (m_pad != nullptr) m_pad_id = ids[i];
  }
  SDL_free(ids);
  clear_gamepad_state();
  read_gamepad_state();
  return m_pad != nullptr;
}

void Sdl3Platform::read_gamepad_state() {
  if (m_pad == nullptr) return;
  for (std::size_t i = 0; i < kPadButtons; ++i) {
    m_pad_down[i] = SDL_GetGamepadButton(m_pad, to_sdl_pad_button(static_cast<GamepadButton>(i)));
  }
  for (std::size_t i = 0; i < kPadAxes; ++i) {
    m_axes[i] = axis_value(SDL_GetGamepadAxis(m_pad, static_cast<SDL_GamepadAxis>(i)));
  }
}

void Sdl3Platform::close_gamepad() {
  if (m_pad != nullptr) SDL_CloseGamepad(m_pad);
  m_pad = nullptr;
  m_pad_id = 0;
  clear_gamepad_state();
}

void Sdl3Platform::clear_gamepad_state() {
  m_pad_down.fill(false);
  m_pad_pressed.fill(false);
  m_pad_released.fill(false);
  m_axes.fill(0.0f);
}

void Sdl3Platform::set_title(const char* title) { SDL_SetWindowTitle(m_window, title); }

void Sdl3Platform::start_text_input() { SDL_StartTextInput(m_window); }

void Sdl3Platform::stop_text_input() { SDL_StopTextInput(m_window); }

bool Sdl3Platform::text_input_active() const { return SDL_TextInputActive(m_window); }

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

bool Sdl3Platform::is_down(GamepadButton button) const {
  const std::size_t i = static_cast<std::size_t>(button);
  return i < m_pad_down.size() && m_pad_down[i];
}

bool Sdl3Platform::was_pressed(GamepadButton button) const {
  const std::size_t i = static_cast<std::size_t>(button);
  return i < m_pad_pressed.size() && m_pad_pressed[i];
}

bool Sdl3Platform::was_released(GamepadButton button) const {
  const std::size_t i = static_cast<std::size_t>(button);
  return i < m_pad_released.size() && m_pad_released[i];
}

float Sdl3Platform::axis(GamepadAxis axis) const {
  const std::size_t i = static_cast<std::size_t>(axis);
  return i < m_axes.size() ? m_axes[i] : 0.0f;
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
