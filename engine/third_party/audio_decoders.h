#pragma once

// A small C++ face on the vendored audio decoders (dr_wav, dr_flac, dr_mp3,
// stb_vorbis). Only engine/core/sound.cpp uses it. The decoders themselves
// are compiled in audio_decoders.cpp, in the third-party target, without our
// warnings-as-errors flags (engine/third_party/README.md).
//
// Decoding streams through a Sink a chunk at a time, so the caller decides
// how much to keep: it can mix channels down as they arrive and stop as soon
// as a sound is too long, instead of holding a whole file's channels first.

#include <cstddef>
#include <cstdint>

namespace scraps::decoders {

struct StreamInfo {
  unsigned channels = 0;          // 1..8 (more is refused before any sample)
  unsigned sample_rate = 0;       // 1..384000
  std::uint64_t total_frames = 0; // 0 when the format does not say
};

// Receives a stream. Every function returns false to stop decoding.
struct Sink {
  void* user = nullptr;
  bool (*begin)(void* user, const StreamInfo& info) = nullptr;
  // Interleaved float frames in -1..1, channels in WAV order
  // (FL FR FC LFE BL BR SL SR) whatever the file's own order.
  bool (*frames)(void* user, const float* interleaved, std::size_t count) = nullptr;
};

enum class Result : std::uint8_t {
  kOk,         // decoded to the end
  kStopped,    // the sink said stop
  kMalformed,  // not decodable
};

Result decode_wav(const std::uint8_t* data, std::size_t size, const Sink& sink);  // also AIFF
Result decode_flac(const std::uint8_t* data, std::size_t size, const Sink& sink);  // also Ogg FLAC
Result decode_mp3(const std::uint8_t* data, std::size_t size, const Sink& sink);
Result decode_vorbis(const std::uint8_t* data, std::size_t size, const Sink& sink);

}  // namespace scraps::decoders
