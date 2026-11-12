#pragma once

#include <cstddef>

namespace scraps {

// Plays a stream of samples: 48 kHz, stereo, interleaved float in -1..1
// (the format of core/sound.h). The engine mixes sound effects itself
// (core/mixer.h) and hands the finished samples over here, a few
// milliseconds ahead, from the main thread: no audio-thread callbacks.
class Audio {
 public:
  virtual ~Audio() = default;

  // Queues `frames` frames to play after what is already queued. Never
  // blocks; without an audio device it does nothing (a game must keep
  // running with no sound).
  virtual void submit(const float* stereo, std::size_t frames) = 0;
  // Frames queued and not played yet.
  virtual std::size_t queued_frames() const = 0;
};

}  // namespace scraps
