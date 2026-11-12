#include "core/sound.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

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

// Turns a decoder's stream into a SoundData as it arrives: each frame is
// mixed down to stereo (downmix_for) and resampled to 48 kHz on the fly
// (linear interpolation: good enough for effects). Only the finished
// samples are kept, and decoding stops the moment a sound passes 30
// seconds, so memory never depends on what a file claims or contains: at
// most 30 s x 48 kHz x 2 floats (11.5 MB).
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
    if (m_seen == 0) return;
    while (m_next < static_cast<double>(m_seen)) {
      emit(m_prev[0], m_prev[1]);
      m_next += m_step;
    }
  }

  bool too_long() const { return m_too_long; }

 private:
  bool begin(const decoders::StreamInfo& info) {
    m_channels = info.channels;
    m_mix = downmix_for(info.channels);
    m_step = static_cast<double>(info.sample_rate) / static_cast<double>(kSoundSampleRate);
    m_max_source_frames = static_cast<std::uint64_t>(std::ceil(kMaxSoundSeconds * info.sample_rate));
    if (info.total_frames > m_max_source_frames) {
      m_too_long = true;
      return false;
    }
    const std::uint64_t expected = info.total_frames > 0 ? info.total_frames : static_cast<std::uint64_t>(info.sample_rate);
    m_out.samples.reserve(static_cast<std::size_t>(static_cast<double>(expected) / m_step + 2.0) * kSoundChannels);
    return true;
  }

  bool frames(const float* interleaved, std::size_t count) {
    if (m_seen + count > m_max_source_frames) {
      m_too_long = true;
      return false;
    }
    for (std::size_t f = 0; f < count; ++f) {
      const float* s = interleaved + f * m_channels;
      float left = 0.0f;
      float right = 0.0f;
      for (unsigned c = 0; c < m_channels; ++c) {
        left += s[c] * m_mix.left[c];
        right += s[c] * m_mix.right[c];
      }
      if (m_seen == 0) {
        m_prev[0] = left;
        m_prev[1] = right;
      }
      // Output frames whose position falls between the previous source
      // frame (m_seen - 1) and this one (m_seen).
      const double here = static_cast<double>(m_seen);
      while (m_next <= here) {
        const float t = m_seen == 0 ? 0.0f : static_cast<float>(m_next - (here - 1.0));
        emit(m_prev[0] + (left - m_prev[0]) * t, m_prev[1] + (right - m_prev[1]) * t);
        m_next += m_step;
      }
      m_prev[0] = left;
      m_prev[1] = right;
      ++m_seen;
    }
    return true;
  }

  void emit(float left, float right) {
    m_out.samples.push_back(clamp_sample(left));
    m_out.samples.push_back(clamp_sample(right));
  }

  static float clamp_sample(float v) {
    if (!(v >= -1.0f)) return -1.0f;  // also catches NaN
    return v > 1.0f ? 1.0f : v;
  }

  SoundData& m_out;
  unsigned m_channels = 0;
  Downmix m_mix;
  double m_step = 1.0;  // source frames per output frame
  double m_next = 0.0;  // source position of the next output frame
  std::uint64_t m_seen = 0;
  std::uint64_t m_max_source_frames = 0;
  float m_prev[2] = {0.0f, 0.0f};
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
    error = "sound is longer than 30 seconds (sound effects only; music streaming is not supported yet)";
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
