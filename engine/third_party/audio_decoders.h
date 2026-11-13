#pragma once

// A small C++ face on the vendored audio decoders (dr_wav, dr_flac, dr_mp3,
// stb_vorbis). Only engine/core/sound.cpp and engine/core/music.cpp use it. The decoders themselves
// are compiled in audio_decoders.cpp, in the third-party target, without our
// warnings-as-errors flags (engine/third_party/README.md).
//
// Decoding streams through a Sink a chunk at a time, so the caller decides
// how much to keep: it can mix channels down as they arrive and stop as soon
// as a sound is too long, instead of holding a whole file's channels first.

#include <cstddef>
#include <cstdint>
#include <memory>

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

enum class Codec : std::uint8_t { kWav, kFlac, kMp3, kVorbis };  // kWav also reads AIFF, kFlac Ogg FLAC

// Decodes when asked instead of all at once: music plays for minutes, so it
// is decoded a chunk at a time while it plays (engine/core/music.cpp). The
// encoded bytes stay with the caller and must not move or change while the
// decoder is open.
//
// Everything a decoder needs is allocated in open(), so read() and rewind()
// allocate nothing. dr_libs allocate through callbacks that use operator new,
// so the engine's allocation test (tests/alloc_test.cpp) would see it if they
// did; stb_vorbis mallocs its setup in open() and decodes with stack scratch.
class PullDecoder {
 public:
  PullDecoder();
  ~PullDecoder();
  PullDecoder(const PullDecoder&) = delete;
  PullDecoder& operator=(const PullDecoder&) = delete;

  // False when the bytes are not that codec, or the stream has 0 or more
  // than 8 channels, or a sample rate of 0 or above 384 kHz.
  bool open(Codec codec, const std::uint8_t* data, std::size_t size, StreamInfo& info);
  void close();
  bool is_open() const { return m_state != nullptr; }

  // Up to `frames` interleaved float frames in -1..1, channels in WAV order
  // like Sink::frames. 0 at the end of the stream or when the data is damaged.
  std::size_t read(float* interleaved, std::size_t frames);
  // Back to the first frame. False when the decoder cannot seek.
  bool rewind();

 private:
  struct State;
  std::unique_ptr<State> m_state;
};

}  // namespace scraps::decoders
