#include "core/music.h"

#include <fstream>
#include <utility>

#include "core/file_path.h"
#include "third_party/audio_decoders.h"

namespace scraps {
namespace {

// Source frames decoded at a time: about 85 ms at 48 kHz, so one decode
// call covers more than a frame's worth of mixing (the client mixes about
// 60 ms ahead), and the buffer stays small (at most 8 channels x 4096).
constexpr std::size_t kChunkFrames = 4096;

}  // namespace

MusicStream::MusicStream() = default;
MusicStream::~MusicStream() = default;
MusicStream::MusicStream(MusicStream&& other) noexcept { *this = std::move(other); }

MusicStream& MusicStream::operator=(MusicStream&& other) noexcept {
  if (this != &other) {
    m_decoder.reset();  // before the bytes it reads from go
    m_bytes = std::move(other.m_bytes);
    m_decoder = std::move(other.m_decoder);
    m_chunk = std::move(other.m_chunk);
    m_chunk_frames = other.m_chunk_frames;
    m_chunk_position = other.m_chunk_position;
    m_mix = other.m_mix;
    m_resampler = other.m_resampler;
    m_format = other.m_format;
    m_source_rate = other.m_source_rate;
    m_source_channels = other.m_source_channels;
    m_loop_frames = other.m_loop_frames;
    m_loops = other.m_loops;
    m_looping = other.m_looping;
    m_ended = other.m_ended;
    m_failed = other.m_failed;
    other.close();
  }
  return *this;
}

void MusicStream::close() {
  m_decoder.reset();
  std::vector<std::uint8_t>().swap(m_bytes);
  std::vector<float>().swap(m_chunk);
  m_chunk_frames = 0;
  m_chunk_position = 0;
  m_mix = Downmix();
  m_resampler = StereoResampler();
  m_format = AudioFormat::kUnknown;
  m_source_rate = 0;
  m_source_channels = 0;
  m_loop_frames = 0;
  m_loops = 0;
  m_ended = false;
  m_failed = false;
}

bool MusicStream::open(std::vector<std::uint8_t> bytes, std::string& error) {
  close();
  if (bytes.size() > kMaxMusicFileBytes) {
    error = "music file is larger than 64 MB";
    return false;
  }
  const AudioFormat format = detect_audio_format(bytes.data(), bytes.size());
  decoders::Codec codec = decoders::Codec::kWav;
  switch (format) {
    case AudioFormat::kWav:
    case AudioFormat::kAiff: codec = decoders::Codec::kWav; break;
    case AudioFormat::kFlac: codec = decoders::Codec::kFlac; break;
    case AudioFormat::kMp3: codec = decoders::Codec::kMp3; break;
    case AudioFormat::kOggVorbis: codec = decoders::Codec::kVorbis; break;
    case AudioFormat::kOpus:
    case AudioFormat::kAac:
    case AudioFormat::kWma:
      error = std::string("this is ") + audio_format_name(format) +
              " audio, which Scraps cannot decode yet; use OGG Vorbis, MP3, FLAC or WAV";
      return false;
    default:
      error = "not a music file Scraps recognises (OGG Vorbis, MP3, FLAC or WAV)";
      return false;
  }

  m_bytes = std::move(bytes);
  std::unique_ptr<decoders::PullDecoder> decoder = std::make_unique<decoders::PullDecoder>();
  decoders::StreamInfo info;
  if (!decoder->open(codec, m_bytes.data(), m_bytes.size(), info)) {
    close();
    error = std::string("cannot decode this ") + audio_format_name(format) + " file (damaged or unusual)";
    return false;
  }
  m_decoder = std::move(decoder);
  m_format = format;
  m_source_rate = info.sample_rate;
  m_source_channels = info.channels;
  m_mix = downmix_for(info.channels);
  m_resampler.reset(info.sample_rate);
  m_chunk.assign(kChunkFrames * info.channels, 0.0f);

  // The first chunk now: a file that opens but does not decode is an error
  // here, not silence later.
  if (!refill()) {
    close();
    error = std::string("this ") + audio_format_name(format) + " file holds no sound (or is damaged)";
    return false;
  }
  return true;
}

bool MusicStream::refill() {
  if (m_decoder == nullptr || m_ended || m_failed) return false;
  std::size_t got = m_decoder->read(m_chunk.data(), kChunkFrames);
  if (got == 0 && m_looping && m_loop_frames > 0) {
    // The end of the track: back to its first frame. The resampler keeps
    // its state, so it interpolates from the last frame into the first.
    if (m_loop_frames < kMinMusicLoopFrames || !m_decoder->rewind()) {
      m_failed = true;
      return false;
    }
    ++m_loops;
    m_loop_frames = 0;
    got = m_decoder->read(m_chunk.data(), kChunkFrames);
  }
  if (got == 0) {
    if (m_looping) m_failed = true;  // nothing at all after a rewind (or at the start)
    return false;
  }
  m_chunk_frames = got;
  m_chunk_position = 0;
  return true;
}

std::size_t MusicStream::read(float* stereo, std::size_t frames) {
  if (stereo == nullptr) return 0;
  std::size_t written = 0;
  float left = 0.0f;
  float right = 0.0f;
  while (written < frames && !m_ended) {
    if (m_resampler.ready()) {
      m_resampler.pop(left, right);
    } else if (m_chunk_position < m_chunk_frames) {
      downmix_frame(m_mix, m_chunk.data() + m_chunk_position * m_source_channels, m_source_channels, left, right);
      ++m_chunk_position;
      ++m_loop_frames;
      m_resampler.push(left, right);
      continue;
    } else if (refill()) {
      continue;
    } else if (!m_failed && m_resampler.tail_ready()) {
      // A stream that does not loop ends like a whole-file decode does.
      m_resampler.pop_tail(left, right);
    } else {
      m_ended = true;
      break;
    }
    stereo[written * kSoundChannels] = clamp_sample(left);
    stereo[written * kSoundChannels + 1] = clamp_sample(right);
    ++written;
  }
  return written;
}

bool open_music(const std::string& path, MusicStream& out, std::string& error) {
  out.close();
  std::ifstream file;
  if (!open_file_for_reading(path, file)) {
    error = "cannot open '" + path + "'";
    return false;
  }
  file.seekg(0, std::ios::end);
  const std::streamoff length = file.tellg();
  if (length < 0 || static_cast<std::uint64_t>(length) > kMaxMusicFileBytes) {
    error = "'" + path + "' is larger than 64 MB";
    return false;
  }
  file.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
  if (length > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), length)) {
    error = "cannot read '" + path + "'";
    return false;
  }
  std::string reason;
  if (!out.open(std::move(bytes), reason)) {
    error = "'" + path + "': " + reason;
    return false;
  }
  return true;
}

}  // namespace scraps
