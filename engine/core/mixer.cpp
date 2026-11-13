#include "core/mixer.h"

#include <cmath>
#include <utility>

namespace scraps {
namespace {

constexpr float kHalfPi = 1.57079632679f;
constexpr float kGlideStep = static_cast<float>(1.0 / (Mixer::kGainGlideSeconds * kSoundSampleRate));

// Volumes and gains are 0..4; NaN counts as 0.
float clamp_gain(float gain) {
  if (!(gain > 0.0f)) return 0.0f;
  return gain > 4.0f ? 4.0f : gain;
}

std::uint32_t fade_frames_for(double seconds) {
  if (!(seconds > 0.0)) return 0;  // also NaN
  if (seconds > Mixer::kMaxMusicFadeSeconds) seconds = Mixer::kMaxMusicFadeSeconds;
  return static_cast<std::uint32_t>(seconds * kSoundSampleRate + 0.5);
}

constexpr std::uint32_t kMinFadeFrames =
    static_cast<std::uint32_t>(Mixer::kMinMusicFadeSeconds * kSoundSampleRate + 0.5);

// One step of a glide from `value` toward `target`.
float glide(float value, float target) {
  if (value < target) return value + kGlideStep < target ? value + kGlideStep : target;
  if (value > target) return value - kGlideStep > target ? value - kGlideStep : target;
  return value;
}

}  // namespace

// ---------------------------------------------------------------- effects

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

std::size_t Mixer::playing() const {
  std::size_t count = 0;
  for (const Voice& voice : m_voices) {
    if (voice.sound != kNoSound) ++count;
  }
  return count;
}

// ---------------------------------------------------------------- music

float Mixer::MusicTrack::level() const {
  if (fade_done >= fade_frames) return fade_to;
  const float p = static_cast<float>(static_cast<double>(fade_done) / static_cast<double>(fade_frames));
  // Equal power: rising follows sin, falling follows cos (see mixer.h).
  const float shape = fade_to >= fade_from ? std::sin(p * kHalfPi) : 1.0f - std::cos(p * kHalfPi);
  return fade_from + (fade_to - fade_from) * shape;
}

void Mixer::start_fade(MusicTrack& track, float to, std::uint32_t frames) {
  track.fade_from = track.level();
  track.fade_to = to;
  track.fade_frames = frames;
  track.fade_done = 0;
}

void Mixer::play_music(MusicStream stream, float volume, double fade_in_seconds) {
  if (!stream.is_open()) return;
  const std::uint32_t fade = fade_frames_for(fade_in_seconds);
  MusicTrack& playing = m_music[m_current];
  if (playing.active) {
    // The playing track moves to the fading-out place (ending whatever was
    // still fading out there) and the new one takes its place.
    start_fade(playing, 0.0f, fade > kMinFadeFrames ? fade : kMinFadeFrames);
    m_current = 1 - m_current;
  }
  MusicTrack& track = m_music[m_current];
  track.stream = std::move(stream);
  track.active = true;
  track.volume = clamp_gain(volume);
  track.volume_target = track.volume;
  track.fade_from = fade > 0 ? 0.0f : 1.0f;
  track.fade_to = 1.0f;
  track.fade_frames = fade;
  track.fade_done = 0;
}

void Mixer::stop_music(double fade_out_seconds) {
  MusicTrack& track = m_music[m_current];
  if (!track.active) return;
  const std::uint32_t fade = fade_frames_for(fade_out_seconds);
  start_fade(track, 0.0f, fade > kMinFadeFrames ? fade : kMinFadeFrames);
}

void Mixer::set_music_track_volume(float volume) { m_music[m_current].volume_target = clamp_gain(volume); }

bool Mixer::music_playing() const {
  const MusicTrack& track = m_music[m_current];
  return track.active && track.fade_to > 0.0f;
}

std::size_t Mixer::music_tracks() const {
  return (m_music[0].active ? 1u : 0u) + (m_music[1].active ? 1u : 0u);
}

const MusicStream* Mixer::current_music() const {
  return music_playing() ? &m_music[m_current].stream : nullptr;
}

void Mixer::set_music_gain(float gain) { m_music_gain_target = clamp_gain(gain); }

void Mixer::set_effects_gain(float gain) { m_effects_gain = clamp_gain(gain); }

void Mixer::mix_track(MusicTrack& track, float* stereo, std::size_t frames) {
  const std::size_t got = track.stream.read(m_music_block.data(), frames);
  const float* from = m_music_block.data();
  for (std::size_t f = 0; f < got; ++f) {
    track.volume = glide(track.volume, track.volume_target);
    const float gain = m_gain_block[f] * track.volume * track.level();
    if (track.fade_done < track.fade_frames) ++track.fade_done;
    stereo[f * kSoundChannels] += from[f * kSoundChannels] * gain;
    stereo[f * kSoundChannels + 1] += from[f * kSoundChannels + 1] * gain;
  }
  // A stream that ended (it does not loop, or its data broke off) is done;
  // so is a track that has faded all the way out. The stream itself stays
  // until a new track replaces it: freeing it is not for the mixing path.
  if (got < frames || (track.fade_to <= 0.0f && track.fade_done >= track.fade_frames)) track.active = false;
}

// ---------------------------------------------------------------- mixing

void Mixer::mix(float* stereo, std::size_t frames) {
  for (std::size_t i = 0; i < frames * kSoundChannels; ++i) stereo[i] = 0.0f;
  for (Voice& voice : m_voices) {
    if (voice.sound == kNoSound) continue;
    const SoundData& sound = m_sounds[voice.sound];
    const std::size_t left = sound.frames() - voice.position;
    const std::size_t count = left < frames ? left : frames;
    const float* from = sound.samples.data() + voice.position * kSoundChannels;
    const float gain = voice.volume * m_effects_gain;
    for (std::size_t i = 0; i < count * kSoundChannels; ++i) stereo[i] += from[i] * gain;
    voice.position += count;
    if (voice.position >= sound.frames()) voice = Voice();
  }

  if (music_tracks() == 0) {
    m_music_gain = m_music_gain_target;  // nothing to glide for
  } else {
    for (std::size_t done = 0; done < frames;) {
      const std::size_t block = frames - done < kMusicBlockFrames ? frames - done : kMusicBlockFrames;
      for (std::size_t f = 0; f < block; ++f) {
        m_music_gain = glide(m_music_gain, m_music_gain_target);
        m_gain_block[f] = m_music_gain;
      }
      // The track on its way out first, then the current one.
      for (std::size_t k = 1; k <= 2; ++k) {
        MusicTrack& track = m_music[(m_current + k) % 2];
        if (track.active) mix_track(track, stereo + done * kSoundChannels, block);
      }
      done += block;
    }
  }

  // Several loud sounds can add up past full scale: clamp instead of wrapping.
  for (std::size_t i = 0; i < frames * kSoundChannels; ++i) {
    if (stereo[i] > 1.0f) stereo[i] = 1.0f;
    if (stereo[i] < -1.0f) stereo[i] = -1.0f;
  }
}

}  // namespace scraps
