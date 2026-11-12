#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace scraps {

// Every sound is kept in one shape, whatever file it came from: 48 kHz,
// stereo, float samples in -1..1, interleaved (left, right, left, right…).
// Converting once at load time makes playing a sound cost nothing.
constexpr int kSoundSampleRate = 48000;
constexpr int kSoundChannels = 2;
constexpr std::size_t kMaxSoundFileBytes = 32u * 1024u * 1024u;
constexpr double kMaxSoundSeconds = 30.0;

struct SoundData {
  std::vector<float> samples;  // frames * 2
  std::size_t frames() const { return samples.size() / kSoundChannels; }
};

// What a file holds, judged by its first bytes, never by its name.
enum class AudioFormat : std::uint8_t {
  kUnknown,
  kWav,         // RIFF/RIFX/RF64 WAVE, Sony Wave64
  kAiff,        // AIFF, AIFF-C
  kFlac,        // native FLAC, also Ogg FLAC
  kMp3,
  kOggVorbis,
  kOpus,        // recognised, not decoded (needs libopus)
  kAac,         // MP4/M4A or ADTS: recognised, not decoded
  kWma,         // ASF: recognised, not decoded
};

AudioFormat detect_audio_format(const std::uint8_t* data, std::size_t size);
const char* audio_format_name(AudioFormat format);

// Decodes WAV, AIFF, FLAC, MP3 or OGG Vorbis, at any sample rate and with any
// number of channels (1 to 8), into a SoundData. Mono is spread to both
// sides; more than two channels are mixed down. Returns false and sets
// `error` (naming the format when it is known but unsupported) on failure.
bool decode_sound(const std::uint8_t* data, std::size_t size, SoundData& out, std::string& error);

// Reads a file and decodes it.
bool load_sound(const std::string& path, SoundData& out, std::string& error);

}  // namespace scraps
