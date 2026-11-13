#pragma once

#include <cstddef>
#include <cstdint>

namespace scraps {

// Turning decoded audio into the engine's one shape (48 kHz stereo float,
// core/sound.h), one source frame at a time. Sound effects (decoded whole,
// core/sound.cpp) and music (decoded while it plays, core/music.cpp) share
// this code, so a file sounds the same either way, sample for sample, and
// the music tests can compare a stream with a full decode exactly.

// How much of each input channel goes to the left and right output, by
// channel count, for the usual WAV/FLAC channel order (the decoders hand
// every format over in that order):
//   3: FL FR FC   4: FL FR BL BR   5: FL FR FC BL BR   6: FL FR FC LFE BL BR
//   7: FL FR FC LFE BC SL SR       8: FL FR FC LFE BL BR SL SR
// Centre and surround channels go to both sides at -3 dB, LFE is left out,
// and each side is scaled so that the same signal in every channel keeps its
// level (and nothing can clip).
struct Downmix {
  float left[8] = {};
  float right[8] = {};
};

Downmix downmix_for(std::size_t channels);

// One source frame of `channels` (1..8) samples, mixed down to stereo.
inline void downmix_frame(const Downmix& mix, const float* frame, unsigned channels, float& left, float& right) {
  left = 0.0f;
  right = 0.0f;
  for (unsigned c = 0; c < channels; ++c) {
    left += frame[c] * mix.left[c];
    right += frame[c] * mix.right[c];
  }
}

// Full scale is -1..1. NaN becomes -1.
inline float clamp_sample(float v) {
  if (!(v >= -1.0f)) return -1.0f;
  return v > 1.0f ? 1.0f : v;
}

// Linear-interpolation resampler from any rate to 48 kHz (good enough for
// games). Source frames go in with push(); output frames come out with
// pop() while ready(). It never holds more than two source frames, so it
// works the same whether the source arrives all at once or in chunks, and
// it runs on forever for a looping stream: after the last frame of a loop
// it interpolates straight into the first frame of the next, with no gap.
class StereoResampler {
 public:
  void reset(unsigned source_rate);

  void push(float left, float right) {
    if (m_seen >= kRebaseFrames) rebase();
    if (m_seen == 0) {
      m_prev[0] = left;
      m_prev[1] = right;
    } else {
      m_prev[0] = m_cur[0];
      m_prev[1] = m_cur[1];
    }
    m_cur[0] = left;
    m_cur[1] = right;
    ++m_seen;
  }

  // An output frame lies at or before the newest source frame.
  bool ready() const { return m_seen > 0 && m_next <= static_cast<double>(m_seen - 1); }

  // The next output frame: between the two newest source frames.
  void pop(float& left, float& right) {
    const double here = static_cast<double>(m_seen - 1);
    const float t = m_seen == 1 ? 0.0f : static_cast<float>(m_next - (here - 1.0));
    left = m_prev[0] + (m_cur[0] - m_prev[0]) * t;
    right = m_prev[1] + (m_cur[1] - m_prev[1]) * t;
    m_next += m_step;
  }

  // At the very end of a stream that does not loop: the output frames that
  // fall after the last source frame hold its value.
  bool tail_ready() const { return m_seen > 0 && m_next < static_cast<double>(m_seen); }
  void pop_tail(float& left, float& right) {
    left = m_cur[0];
    right = m_cur[1];
    m_next += m_step;
  }

 private:
  // Positions are doubles counted from the start. A stream that loops for
  // hours would slowly lose precision, so every few minutes both counters
  // move back by the same whole number of frames (exact in a double). The
  // first 2^24 frames (5.8 minutes at 48 kHz) are never rebased, so they
  // match a whole-file decode exactly.
  static constexpr std::uint64_t kRebaseFrames = std::uint64_t{1} << 24;
  void rebase() {
    const std::uint64_t shift = m_seen - 2;  // keeps m_seen >= 2: "first frame" stays special only once
    m_seen -= shift;
    m_next -= static_cast<double>(shift);
  }

  double m_step = 1.0;  // source frames per output frame
  double m_next = 0.0;  // source position of the next output frame
  std::uint64_t m_seen = 0;
  float m_prev[2] = {0.0f, 0.0f};
  float m_cur[2] = {0.0f, 0.0f};
};

}  // namespace scraps
