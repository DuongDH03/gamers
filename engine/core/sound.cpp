#include "core/sound.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

#include "core/audio_convert.h"
#include "third_party/audio_decoders.h"

namespace scraps {
namespace {

bool starts_with(const std::uint8_t* data, std::size_t size, std::size_t at, const char* text) {
  const std::size_t length = std::strlen(text);
  return size >= at + length && std::memcmp(data + at, text, length) == 0;
}

// True if `text` appears within the first `limit` bytes.
bool contains(const std::uint8_t* data, std::size_t size, std::size_t limit, const char* text, std::size_t length) {
  const std::size_t end = size < limit ? size : limit;
  for (std::size_t i = 0; i + length <= end; ++i) {
    if (std::memcmp(data + i, text, length) == 0) return true;
  }
  return false;
}

// An ID3v2 tag (common at the start of MP3s, sometimes FLACs): its size, so
// detection can look at what follows it.
std::size_t id3_size(const std::uint8_t* data, std::size_t size) {
  if (!starts_with(data, size, 0, "ID3") || size < 10) return 0;
  const std::size_t body = (static_cast<std::size_t>(data[6] & 0x7F) << 21) |
                           (static_cast<std::size_t>(data[7] & 0x7F) << 14) |
                           (static_cast<std::size_t>(data[8] & 0x7F) << 7) | static_cast<std::size_t>(data[9] & 0x7F);
  const bool footer = (data[5] & 0x10) != 0;
  return 10 + body + (footer ? 10 : 0);
}

// An MPEG audio frame header: 11 sync bits. Layer bits 00 mean AAC (ADTS).
bool mpeg_sync(const std::uint8_t* data, std::size_t size, std::size_t at, bool& is_aac) {
  if (size < at + 2 || data[at] != 0xFF || (data[at + 1] & 0xE0) != 0xE0) return false;
  is_aac = (data[at + 1] & 0x06) == 0 && (data[at + 1] & 0xF0) == 0xF0;
  return true;
}

// Turns a decoder's stream into a SoundData as it arrives: each frame is
// mixed down to stereo and resampled to 48 kHz on the fly (core/audio_convert.h,
// the same conversion music streams use). Only the finished samples are
// kept, and decoding stops the moment a sound passes 30 seconds, so memory
// never depends on what a file claims or contains: at most 30 s x 48 kHz x
// 2 floats (11.5 MB).
class Builder {
 public:
  explicit Builder(SoundData& out) : m_out(out) {}

  decoders::Sink sink() {
    decoders::Sink sink;
    sink.user = this;
    sink.begin = [](void* user, const decoders::StreamInfo& info) { return static_cast<Builder*>(user)->begin(info); };
    sink.frames = [](void* user, const float* interleaved, std::size_t count) {
      return static_cast<Builder*>(user)->frames(interleaved, count);
    };
    return sink;
  }

  // Emits the output frames between the last source frame and the end.
  void finish() {
    float left = 0.0f;
    float right = 0.0f;
    while (m_resampler.tail_ready()) {
      m_resampler.pop_tail(left, right);
      emit(left, right);
    }
  }

  bool too_long() const { return m_too_long; }

 private:
  bool begin(const decoders::StreamInfo& info) {
    m_channels = info.channels;
    m_mix = downmix_for(info.channels);
    m_resampler.reset(info.sample_rate);
    m_max_source_frames = static_cast<std::uint64_t>(std::ceil(kMaxSoundSeconds * info.sample_rate));
    if (info.total_frames > m_max_source_frames) {
      m_too_long = true;
      return false;
    }
    const double step = static_cast<double>(info.sample_rate) / static_cast<double>(kSoundSampleRate);
    const std::uint64_t expected = info.total_frames > 0 ? info.total_frames : static_cast<std::uint64_t>(info.sample_rate);
    m_out.samples.reserve(static_cast<std::size_t>(static_cast<double>(expected) / step + 2.0) * kSoundChannels);
    return true;
  }

  bool frames(const float* interleaved, std::size_t count) {
    if (m_seen + count > m_max_source_frames) {
      m_too_long = true;
      return false;
    }
    for (std::size_t f = 0; f < count; ++f) {
      float left = 0.0f;
      float right = 0.0f;
      downmix_frame(m_mix, interleaved + f * m_channels, m_channels, left, right);
      m_resampler.push(left, right);
      while (m_resampler.ready()) {
        m_resampler.pop(left, right);
        emit(left, right);
      }
    }
    m_seen += count;
    return true;
  }

  void emit(float left, float right) {
    m_out.samples.push_back(clamp_sample(left));
    m_out.samples.push_back(clamp_sample(right));
  }

  SoundData& m_out;
  unsigned m_channels = 0;
  Downmix m_mix;
  StereoResampler m_resampler;
  std::uint64_t m_seen = 0;
  std::uint64_t m_max_source_frames = 0;
  bool m_too_long = false;
};

}  // namespace

AudioFormat detect_audio_format(const std::uint8_t* data, std::size_t size) {
  if (data == nullptr || size < 4) return AudioFormat::kUnknown;
  if ((starts_with(data, size, 0, "RIFF") || starts_with(data, size, 0, "RIFX") || starts_with(data, size, 0, "RF64")) &&
      starts_with(data, size, 8, "WAVE")) {
    return AudioFormat::kWav;
  }
  if (starts_with(data, size, 0, "riff") && size >= 16 && data[4] == 0x2E && data[5] == 0x91) return AudioFormat::kWav;
  if (starts_with(data, size, 0, "FORM") && (starts_with(data, size, 8, "AIFF") || starts_with(data, size, 8, "AIFC"))) {
    return AudioFormat::kAiff;
  }
  if (starts_with(data, size, 0, "OggS")) {
    // The codec is named in the first page's packet.
    if (contains(data, size, 128, "\x01vorbis", 7)) return AudioFormat::kOggVorbis;
    if (contains(data, size, 128, "OpusHead", 8)) return AudioFormat::kOpus;
    if (contains(data, size, 128, "\x7F" "FLAC", 5)) return AudioFormat::kFlac;
    return AudioFormat::kUnknown;
  }
  if (starts_with(data, size, 4, "ftyp")) return AudioFormat::kAac;  // MP4 / M4A
  static const std::uint8_t kAsf[4] = {0x30, 0x26, 0xB2, 0x75};
  if (std::memcmp(data, kAsf, 4) == 0) return AudioFormat::kWma;

  const std::size_t skip = id3_size(data, size);
  if (starts_with(data, size, skip, "fLaC")) return AudioFormat::kFlac;
  bool is_aac = false;
  if (mpeg_sync(data, size, skip, is_aac)) return is_aac ? AudioFormat::kAac : AudioFormat::kMp3;
  if (skip > 0) return AudioFormat::kMp3;  // a tag with no frame right after it: let the MP3 decoder decide
  return AudioFormat::kUnknown;
}

const char* audio_format_name(AudioFormat format) {
  switch (format) {
    case AudioFormat::kWav: return "WAV";
    case AudioFormat::kAiff: return "AIFF";
    case AudioFormat::kFlac: return "FLAC";
    case AudioFormat::kMp3: return "MP3";
    case AudioFormat::kOggVorbis: return "OGG Vorbis";
    case AudioFormat::kOpus: return "Opus";
    case AudioFormat::kAac: return "AAC/M4A";
    case AudioFormat::kWma: return "WMA";
    default: return "unknown";
  }
}

bool decode_sound(const std::uint8_t* data, std::size_t size, SoundData& out, std::string& error) {
  out.samples.clear();
  if (size > kMaxSoundFileBytes) {
    error = "sound file is larger than 32 MB";
    return false;
  }
  const AudioFormat format = detect_audio_format(data, size);
  Builder builder(out);
  const decoders::Sink sink = builder.sink();
  decoders::Result result = decoders::Result::kMalformed;
  switch (format) {
    case AudioFormat::kWav:
    case AudioFormat::kAiff: result = decoders::decode_wav(data, size, sink); break;
    case AudioFormat::kFlac: result = decoders::decode_flac(data, size, sink); break;
    case AudioFormat::kMp3: result = decoders::decode_mp3(data, size, sink); break;
    case AudioFormat::kOggVorbis: result = decoders::decode_vorbis(data, size, sink); break;
    case AudioFormat::kOpus:
    case AudioFormat::kAac:
    case AudioFormat::kWma:
      error = std::string("this is ") + audio_format_name(format) +
              " audio, which Scraps cannot decode yet; use WAV, AIFF, FLAC, MP3 or OGG Vorbis";
      return false;
    default:
      error = "not a sound file Scraps recognises (WAV, AIFF, FLAC, MP3 or OGG Vorbis)";
      return false;
  }
  if (builder.too_long()) {
    out.samples.clear();
    error = "sound is longer than 30 seconds (sound effects only; play longer audio with a scene's music line)";
    return false;
  }
  if (result != decoders::Result::kOk) {
    out.samples.clear();
    error = std::string("cannot decode this ") + audio_format_name(format) + " file (damaged or unusual)";
    return false;
  }
  builder.finish();
  if (out.samples.empty()) {
    error = std::string("this ") + audio_format_name(format) + " file holds no sound";
    return false;
  }
  return true;
}

bool load_sound(const std::string& path, SoundData& out, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "cannot open '" + path + "'";
    return false;
  }
  file.seekg(0, std::ios::end);
  const std::streamoff length = file.tellg();
  if (length < 0 || static_cast<std::uint64_t>(length) > kMaxSoundFileBytes) {
    error = "'" + path + "' is larger than 32 MB";
    return false;
  }
  file.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
  if (length > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), length)) {
    error = "cannot read '" + path + "'";
    return false;
  }
  std::string reason;
  if (!decode_sound(bytes.data(), bytes.size(), out, reason)) {
    error = "'" + path + "': " + reason;
    return false;
  }
  return true;
}

}  // namespace scraps
