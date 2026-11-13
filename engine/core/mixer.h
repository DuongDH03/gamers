#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/music.h"
#include "core/sound.h"

namespace scraps {

// Index of a sound held by a Mixer. kNoSound plays nothing.
using SoundId = std::uint32_t;
constexpr SoundId kNoSound = 0xFFFFFFFFu;

// Mixes sound effects and music into one stereo stream (48 kHz, float).
// Pure code: the backend only receives the finished samples (Audio::submit,
// Rule 3), and tests can check every sample.
//
// Effects: it owns the decoded sounds, so a voice can never outlive its
// sound. Up to kVoices play at once; when all are busy, a new sound
// replaces the oldest.
//
// Music: one track plays beside the effects, streamed from a MusicStream
// the Mixer owns. Starting another crossfades: the old one fades out while
// the new one fades in, so for a moment two tracks are decoded.
//
// mix() allocates nothing, music included.
class Mixer {
 public:
  static constexpr std::size_t kVoices = 32;
  // Music is pulled from its stream this many frames at a time.
  static constexpr std::size_t kMusicBlockFrames = 512;
  // Even an instant stop or switch fades the old track over this long
  // (10 ms): cutting a wave mid-swing clicks.
  static constexpr double kMinMusicFadeSeconds = 0.01;
  // Gain changes (set_music_gain, set_music_track_volume) glide at this
  // speed: from silence to full in 50 ms, so a pause duck never clicks.
  static constexpr double kGainGlideSeconds = 0.05;
  // Longest fade accepted; longer ones are shortened to this.
  static constexpr double kMaxMusicFadeSeconds = 600.0;

  // ---------------------------------------------------------------- effects

  // Takes a decoded sound; returns its id. (Load time: may allocate.)
  SoundId add(SoundData sound);
  // Forgets every sound and stops every voice (a hot reload loads new
  // ones). Music is not touched: it has its own calls below.
  void clear();
  std::size_t sound_count() const { return m_sounds.size(); }

  // Starts a sound at `volume` (1 = as recorded; clamped to 0..4).
  void play(SoundId sound, float volume = 1.0f);
  // Stops every effect voice (not the music).
  void stop_all();
  std::size_t playing() const;

  // ---------------------------------------------------------------- music

  // Plays `stream` (open, and looping unless told otherwise) at `volume`
  // (0..4), fading in over `fade_in_seconds` (0: at full volume at once).
  // A track already playing fades out over the same time (at least
  // kMinMusicFadeSeconds), so a new track crossfades with the old. A stream
  // that is not open is ignored and whatever plays keeps playing.
  //
  // The fade curve is equal power: fading in, a track's level is
  // sin(p * 90°), fading out cos(p * 90°), as p goes from 0 to 1. Two
  // unrelated tracks crossfaded this way keep a steady loudness, where a
  // straight line would dip in the middle.
  //
  //   level 1 ┤╲ out          ╱ in
  //           │   ╲        ╱
  //           │     ╲    ╱
  //           │       ╳          (both at 0.71 halfway)
  //         0 ┼──────────────── p
  //
  // Moving the stream in may free the one it replaces (load time, not mix).
  void play_music(MusicStream stream, float volume = 1.0f, double fade_in_seconds = 0.0);
  // Fades the playing track out over `fade_out_seconds` (at least
  // kMinMusicFadeSeconds), then it is silent.
  void stop_music(double fade_out_seconds = 0.0);
  // The playing track's own volume (0..4), e.g. after a scene edit changed
  // it. Glides; no restart.
  void set_music_track_volume(float volume);
  // A track is playing and not on its way out.
  bool music_playing() const;
  // Tracks being mixed: 0, 1, or 2 during a crossfade.
  std::size_t music_tracks() const;
  // The playing track's stream (for its loop count, format...), or null.
  const MusicStream* current_music() const;

  // ---------------------------------------------------------------- volumes

  // Music and effects have separate volumes (0..4, 1 = as mixed). The
  // music gain is meant for the player's music volume times any duck (a
  // pause plays music at 40 %); it glides (kGainGlideSeconds). The effects
  // gain applies at once.
  void set_music_gain(float gain);
  float music_gain() const { return m_music_gain_target; }
  void set_effects_gain(float gain);
  float effects_gain() const { return m_effects_gain; }

  // Overwrites `stereo` (frames * 2 floats) with the next `frames` frames.
  void mix(float* stereo, std::size_t frames);

 private:
  struct Voice {
    SoundId sound = kNoSound;
    std::size_t position = 0;  // next frame
    float volume = 0.0f;
    std::uint64_t started = 0;  // for "replace the oldest"
  };

  struct MusicTrack {
    MusicStream stream;
    bool active = false;
    float volume = 1.0f;  // glides toward volume_target
    float volume_target = 1.0f;
    float fade_from = 1.0f;  // the fade's level at its start and at its end
    float fade_to = 1.0f;
    std::uint32_t fade_frames = 0;  // the fade's length; 0 = no fade, at fade_to
    std::uint32_t fade_done = 0;
    float level() const;
  };

  static void start_fade(MusicTrack& track, float to, std::uint32_t frames);
  void mix_track(MusicTrack& track, float* stereo, std::size_t frames);

  std::vector<SoundData> m_sounds;
  std::array<Voice, kVoices> m_voices{};
  std::uint64_t m_next_start = 1;

  std::array<MusicTrack, 2> m_music{};
  std::size_t m_current = 0;  // m_music[m_current] plays; the other may be fading out
  std::array<float, kMusicBlockFrames * kSoundChannels> m_music_block{};
  std::array<float, kMusicBlockFrames> m_gain_block{};  // music gain per frame of the block
  float m_music_gain = 1.0f;  // glides toward m_music_gain_target
  float m_music_gain_target = 1.0f;
  float m_effects_gain = 1.0f;
};

}  // namespace scraps
