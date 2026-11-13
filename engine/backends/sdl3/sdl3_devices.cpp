#include "backends/sdl3/sdl3_devices.h"

#include <SDL3/SDL.h>

namespace scraps::sdl3 {
namespace {

constexpr int kAudioSampleRate = 48000;

}  // namespace

std::unique_ptr<Sdl3Devices> Sdl3Devices::create(std::string& notes) {
  notes.clear();
  // Ctrl+C in a console stops the program as usual (SDL would turn it into
  // an event nobody here reads). With no SDL window, SDL cannot tell whether
  // the game is in front, so pad events always arrive; the app filters them.
  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  auto devices = std::make_unique<Sdl3Devices>(Passkey());
  if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;  // interleaved stereo, as the mixer produces (core/sound.h)
    spec.freq = kAudioSampleRate;
    devices->m_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (devices->m_audio != nullptr) {
      SDL_ResumeAudioStreamDevice(devices->m_audio);
    } else {
      notes += std::string("no audio device: ") + SDL_GetError() + "; ";
    }
  } else {
    notes += std::string("no audio: ") + SDL_GetError() + "; ";
  }
  if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
    devices->m_pad.open_any();
  } else {
    notes += std::string("no gamepad support: ") + SDL_GetError() + "; ";
  }
  return devices;
}

Sdl3Devices::~Sdl3Devices() {
  m_pad.close();  // before SDL_Quit
  if (m_audio != nullptr) SDL_DestroyAudioStream(m_audio);
  SDL_Quit();
}

void Sdl3Devices::pump() {
  m_pad.begin_frame();
  m_events.clear();
  SDL_Event event;
  while (SDL_PollEvent(&event)) m_pad.handle(event, m_events);  // anything else is not ours
}

void Sdl3Devices::submit(const float* stereo, std::size_t frames) {
  if (m_audio == nullptr || stereo == nullptr || frames == 0) return;
  SDL_PutAudioStreamData(m_audio, stereo, static_cast<int>(frames * 2 * sizeof(float)));
}

std::size_t Sdl3Devices::queued_frames() const {
  if (m_audio == nullptr) return 0;
  const int bytes = SDL_GetAudioStreamQueued(m_audio);
  return bytes > 0 ? static_cast<std::size_t>(bytes) / (2 * sizeof(float)) : 0;
}

}  // namespace scraps::sdl3
