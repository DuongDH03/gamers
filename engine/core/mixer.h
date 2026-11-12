#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/sound.h"

namespace scraps {

// Index of a sound held by a Mixer. kNoSound plays nothing.
using SoundId = std::uint32_t;
constexpr SoundId kNoSound = 0xFFFFFFFFu;

// Mixes sound effects into one stereo stream (48 kHz, float). Pure code: the
// backend only receives the finished samples (Audio::submit, Rule 3), and
// tests can check every sample.
//
// It owns the decoded sounds, so a voice can never outlive its sound.
// Up to kVoices play at once; when all are busy, a new sound replaces the
// oldest. mix() allocates nothing.
class Mixer {
 public:
  static constexpr std::size_t kVoices = 32;

  // Takes a decoded sound; returns its id. (Load time: may allocate.)
  SoundId add(SoundData sound);
  // Forgets every sound and stops every voice (a hot reload loads new ones).
  void clear();
  std::size_t sound_count() const { return m_sounds.size(); }

  // Starts a sound at `volume` (1 = as recorded; clamped to 0..4).
  void play(SoundId sound, float volume = 1.0f);
  void stop_all();

  // Overwrites `stereo` (frames * 2 floats) with the next `frames` frames.
  void mix(float* stereo, std::size_t frames);
  std::size_t playing() const;

 private:
  struct Voice {
    SoundId sound = kNoSound;
    std::size_t position = 0;  // next frame
    float volume = 0.0f;
    std::uint64_t started = 0;  // for "replace the oldest"
  };

  std::vector<SoundData> m_sounds;
  std::array<Voice, kVoices> m_voices{};
  std::uint64_t m_next_start = 1;
};

}  // namespace scraps
