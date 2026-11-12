#include "core/mixer.h"

#include <utility>

namespace scraps {

SoundId Mixer::add(SoundData sound) {
  m_sounds.push_back(std::move(sound));
  return static_cast<SoundId>(m_sounds.size() - 1);
}

void Mixer::clear() {
  stop_all();
  m_sounds.clear();
}

void Mixer::stop_all() {
  for (Voice& voice : m_voices) voice = Voice();
}

void Mixer::play(SoundId sound, float volume) {
  if (sound >= m_sounds.size() || m_sounds[sound].frames() == 0) return;
  if (!(volume > 0.0f)) return;  // also refuses NaN
  if (volume > 4.0f) volume = 4.0f;
  Voice* slot = &m_voices[0];
  for (Voice& voice : m_voices) {
    if (voice.sound == kNoSound) {
      slot = &voice;
      break;
    }
    if (voice.started < slot->started) slot = &voice;  // all busy: the oldest goes
  }
  slot->sound = sound;
  slot->position = 0;
  slot->volume = volume;
  slot->started = m_next_start++;
}

void Mixer::mix(float* stereo, std::size_t frames) {
  for (std::size_t i = 0; i < frames * kSoundChannels; ++i) stereo[i] = 0.0f;
  for (Voice& voice : m_voices) {
    if (voice.sound == kNoSound) continue;
    const SoundData& sound = m_sounds[voice.sound];
    const std::size_t left = sound.frames() - voice.position;
    const std::size_t count = left < frames ? left : frames;
    const float* from = sound.samples.data() + voice.position * kSoundChannels;
    for (std::size_t i = 0; i < count * kSoundChannels; ++i) stereo[i] += from[i] * voice.volume;
    voice.position += count;
    if (voice.position >= sound.frames()) voice = Voice();
  }
  // Several loud sounds can add up past full scale: clamp instead of wrapping.
  for (std::size_t i = 0; i < frames * kSoundChannels; ++i) {
    if (stereo[i] > 1.0f) stereo[i] = 1.0f;
    if (stereo[i] < -1.0f) stereo[i] = -1.0f;
  }
}

std::size_t Mixer::playing() const {
  std::size_t count = 0;
  for (const Voice& voice : m_voices) {
    if (voice.sound != kNoSound) ++count;
  }
  return count;
}

}  // namespace scraps
