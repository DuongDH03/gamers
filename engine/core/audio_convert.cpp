#include "core/audio_convert.h"

#include "core/sound.h"

namespace scraps {

Downmix downmix_for(std::size_t channels) {
  constexpr float c = 0.70710678f;
  Downmix d;
  auto set = [&d](std::size_t ch, float l, float r) {
    d.left[ch] = l;
    d.right[ch] = r;
  };
  switch (channels) {
    case 1: set(0, 1.0f, 1.0f); break;
    case 2: set(0, 1.0f, 0.0f); set(1, 0.0f, 1.0f); break;
    case 3: set(0, 1.0f, 0.0f); set(1, 0.0f, 1.0f); set(2, c, c); break;
    case 4: set(0, 1.0f, 0.0f); set(1, 0.0f, 1.0f); set(2, c, 0.0f); set(3, 0.0f, c); break;
    case 5: set(0, 1.0f, 0.0f); set(1, 0.0f, 1.0f); set(2, c, c); set(3, c, 0.0f); set(4, 0.0f, c); break;
    case 6: set(0, 1.0f, 0.0f); set(1, 0.0f, 1.0f); set(2, c, c); set(4, c, 0.0f); set(5, 0.0f, c); break;
    case 7:
      set(0, 1.0f, 0.0f); set(1, 0.0f, 1.0f); set(2, c, c); set(4, 0.5f, 0.5f); set(5, c, 0.0f); set(6, 0.0f, c);
      break;
    default:  // 8
      set(0, 1.0f, 0.0f); set(1, 0.0f, 1.0f); set(2, c, c); set(4, c, 0.0f); set(5, 0.0f, c);
      set(6, c, 0.0f); set(7, 0.0f, c);
      break;
  }
  float sum_left = 0.0f;
  float sum_right = 0.0f;
  for (std::size_t ch = 0; ch < channels && ch < 8; ++ch) {
    sum_left += d.left[ch];
    sum_right += d.right[ch];
  }
  for (std::size_t ch = 0; ch < 8; ++ch) {
    if (sum_left > 0.0f) d.left[ch] /= sum_left;
    if (sum_right > 0.0f) d.right[ch] /= sum_right;
  }
  return d;
}

void StereoResampler::reset(unsigned source_rate) {
  m_step = static_cast<double>(source_rate) / static_cast<double>(kSoundSampleRate);
  m_next = 0.0;
  m_seen = 0;
  m_prev[0] = m_prev[1] = 0.0f;
  m_cur[0] = m_cur[1] = 0.0f;
}

}  // namespace scraps
