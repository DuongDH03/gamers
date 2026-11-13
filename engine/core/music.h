#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/audio_convert.h"
#include "core/sound.h"

namespace scraps {

namespace decoders {
class PullDecoder;
}

// Music files are kept in memory still encoded (a 3-minute OGG is about
// 3 MB) and decoded while they play. This caps that copy; a long WAV is
// better stored as OGG.
constexpr std::size_t kMaxMusicFileBytes = 64u * 1024u * 1024u;

// A loop shorter than this (in the file's own frames, about 20 ms at
// 48 kHz) is refused when it first wraps: looping it would mean seeking back
// to the start every few output frames.
constexpr std::uint64_t kMinMusicLoopFrames = 1024;

// Background music, decoded a chunk at a time while it plays instead of all
// at once like a sound effect (a 3-minute song would be about 70 MB of
// floats). It reads OGG Vorbis, MP3, FLAC, WAV and AIFF, recognised by
// content (core/sound.h), and turns them into 48 kHz stereo float with the
// same conversion as sound effects (core/audio_convert.h).
//
// Every buffer is sized in open(): read() allocates nothing, so the Mixer
// can pull from it while mixing (core/mixer.h). Presentation only: music
// never touches the World (Rule 1).
//
// A stream loops by default: after its last frame comes its first, with
// nothing added or dropped in between (the resampler interpolates across
// the seam). How cleanly a track loops is then up to the file: WAV, FLAC
// and OGG Vorbis are sample-exact; MP3 encoders pad the ends a little.
class MusicStream {
 public:
  MusicStream();
  ~MusicStream();
  MusicStream(MusicStream&& other) noexcept;
  MusicStream& operator=(MusicStream&& other) noexcept;
  MusicStream(const MusicStream&) = delete;
  MusicStream& operator=(const MusicStream&) = delete;

  // Takes the encoded file. False, with `error` set and the stream closed,
  // when it is not a format Scraps can decode or the first chunk does not
  // decode. Load time: allocates.
  bool open(std::vector<std::uint8_t> bytes, std::string& error);
  void close();
  bool is_open() const { return m_decoder != nullptr; }

  void set_looping(bool looping) { m_looping = looping; }
  bool looping() const { return m_looping; }

  // Writes up to `frames` frames (48 kHz, stereo, interleaved) into
  // `stereo` and returns how many. Fewer only when the stream has ended:
  // a stream that does not loop reached its end, or decoding failed
  // (failed()). Allocates nothing.
  std::size_t read(float* stereo, std::size_t frames);

  bool ended() const { return m_ended; }
  bool failed() const { return m_failed; }
  // How many times it wrapped from its end back to its start.
  std::uint32_t loops() const { return m_loops; }

  AudioFormat format() const { return m_format; }
  unsigned source_rate() const { return m_source_rate; }
  unsigned source_channels() const { return m_source_channels; }

 private:
  bool refill();

  std::vector<std::uint8_t> m_bytes;  // the encoded file; its heap block never moves while open
  std::unique_ptr<decoders::PullDecoder> m_decoder;
  std::vector<float> m_chunk;  // decoded source frames, interleaved, source channel count
  std::size_t m_chunk_frames = 0;
  std::size_t m_chunk_position = 0;
  Downmix m_mix;
  StereoResampler m_resampler;
  AudioFormat m_format = AudioFormat::kUnknown;
  unsigned m_source_rate = 0;
  unsigned m_source_channels = 0;
  std::uint64_t m_loop_frames = 0;  // source frames since the last wrap
  std::uint32_t m_loops = 0;
  bool m_looping = true;
  bool m_ended = false;
  bool m_failed = false;
};

// Reads a file and opens it. Errors name the file.
bool open_music(const std::string& path, MusicStream& out, std::string& error);

}  // namespace scraps
