#pragma once

#include <memory>
#include <string>

#include "backends/sdl3/sdl3_gamepad.h"
#include "platform/audio.h"
#include "platform/input.h"

struct SDL_AudioStream;

namespace scraps::sdl3 {

// SDL's audio and gamepads without an SDL window: what an app pairs with a
// window from another backend (the WebView2 one), so that backend needs no
// SDL of its own. Implements Audio, and Input for the gamepad only (no keys,
// no mouse: those come from the window). Both parts are optional: without an
// audio device submit() does nothing, without a pad nothing is pressed.
//
// The app calls pump() once per frame, before the window's pump_events(),
// and decides itself whether the pad counts (only while its window is in
// front: SDL cannot tell, as it owns no window here).
class Sdl3Devices final : public Input, public Audio {
  struct Passkey {
    explicit Passkey() = default;
  };

 public:
  explicit Sdl3Devices(Passkey) {}
  // Never fails: a missing audio device or gamepad support is reported in
  // `notes` (empty when everything started).
  static std::unique_ptr<Sdl3Devices> create(std::string& notes);
  ~Sdl3Devices() override;
  Sdl3Devices(const Sdl3Devices&) = delete;
  Sdl3Devices& operator=(const Sdl3Devices&) = delete;

  // Reads SDL's events: gamepads plugged in and out, buttons, axes.
  void pump();

  // Input: the gamepad only.
  bool is_down(Key) const override { return false; }
  bool was_pressed(Key) const override { return false; }
  PointerPosition pointer() const override { return PointerPosition(); }
  bool pointer_inside() const override { return false; }
  bool is_down(MouseButton) const override { return false; }
  bool was_pressed(MouseButton) const override { return false; }
  bool was_released(MouseButton) const override { return false; }
  float wheel() const override { return 0.0f; }
  bool gamepad_connected() const override { return m_pad.connected(); }
  bool is_down(GamepadButton button) const override { return m_pad.is_down(button); }
  bool was_pressed(GamepadButton button) const override { return m_pad.was_pressed(button); }
  bool was_released(GamepadButton button) const override { return m_pad.was_released(button); }
  float axis(GamepadAxis axis) const override { return m_pad.axis(axis); }
  const InputEventList& events() const override { return m_events; }
  void start_text_input() override {}
  void stop_text_input() override {}
  bool text_input_active() const override { return false; }

  // Audio
  void submit(const float* stereo, std::size_t frames) override;
  std::size_t queued_frames() const override;

 private:
  SDL_AudioStream* m_audio = nullptr;
  GamepadReader m_pad;
  InputEventList m_events;  // this frame's gamepad button events
};

}  // namespace scraps::sdl3
