// The one place the audio decoders' implementations are compiled. Memory
// only: we read files ourselves (engine/core/sound.cpp) and hand over bytes,
// so none of them needs stdio. Third-party code: compiled without our
// warning flags (engine/third_party/README.md).
#include "third_party/audio_decoders.h"

#include <vector>

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#define DR_WAV_NO_WCHAR
#include "third_party/dr_libs/dr_wav.h"

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_WCHAR
#include "third_party/dr_libs/dr_flac.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "third_party/dr_libs/dr_mp3.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "third_party/stb/stb_vorbis.c"

namespace scraps::decoders {

namespace {

constexpr unsigned kMaxChannels = 8;
constexpr unsigned kMaxSampleRate = 384000;
constexpr std::size_t kChunkFrames = 4096;

// For each WAV-order position, the Vorbis channel that belongs there.
// Vorbis orders surround channels differently from WAV and FLAC; every
// decoder hands its samples over in WAV order (FL FR FC LFE BL BR SL SR), so
// the downmix needs only one table (engine/core/sound.cpp).
const int* vorbis_to_wav_order(unsigned channels) {
  static const int k3[] = {0, 2, 1};                 // L C R
  static const int k5[] = {0, 2, 1, 3, 4};           // FL C FR RL RR
  static const int k6[] = {0, 2, 1, 5, 3, 4};        // FL C FR RL RR LFE
  static const int k7[] = {0, 2, 1, 6, 5, 3, 4};     // FL C FR SL SR RC LFE
  static const int k8[] = {0, 2, 1, 7, 5, 6, 3, 4};  // FL C FR SL SR RL RR LFE
  switch (channels) {
    case 3: return k3;
    case 5: return k5;
    case 6: return k6;
    case 7: return k7;
    case 8: return k8;
    default: return nullptr;  // 1, 2 and 4 channels are the same in both
  }
}

// Feeds the sink chunk by chunk from `read(frames, destination)` until the
// decoder runs dry or the sink says stop. The chunk buffer is fixed: memory
// never depends on how long the sound is; the sink decides what to keep.
template <typename ReadFn>
Result stream(const StreamInfo& info, const Sink& sink, ReadFn read, const int* reorder = nullptr) {
  if (info.channels == 0 || info.channels > kMaxChannels || info.sample_rate == 0 || info.sample_rate > kMaxSampleRate) {
    return Result::kMalformed;
  }
  if (!sink.begin(sink.user, info)) return Result::kStopped;
  std::vector<float> buffer(kChunkFrames * info.channels);  // once per sound, at load time
  float* chunk = buffer.data();
  float frame[kMaxChannels];
  bool any = false;
  for (;;) {
    const std::size_t got = read(kChunkFrames, chunk);
    if (got == 0) break;
    any = true;
    if (reorder != nullptr) {
      for (std::size_t f = 0; f < got; ++f) {
        float* at = chunk + f * info.channels;
        for (unsigned c = 0; c < info.channels; ++c) frame[c] = at[reorder[c]];
        for (unsigned c = 0; c < info.channels; ++c) at[c] = frame[c];
      }
    }
    if (!sink.frames(sink.user, chunk, got)) return Result::kStopped;
  }
  return any ? Result::kOk : Result::kMalformed;
}

}  // namespace

Result decode_wav(const std::uint8_t* data, std::size_t size, const Sink& sink) {
  drwav wav;
  if (!drwav_init_memory(&wav, data, size, nullptr)) return Result::kMalformed;
  StreamInfo info;
  info.channels = wav.channels;
  info.sample_rate = wav.sampleRate;
  info.total_frames = wav.totalPCMFrameCount;
  const Result result = stream(info, sink, [&](std::size_t frames, float* to) {
    return static_cast<std::size_t>(drwav_read_pcm_frames_f32(&wav, frames, to));
  });
  drwav_uninit(&wav);
  return result;
}

Result decode_flac(const std::uint8_t* data, std::size_t size, const Sink& sink) {
  drflac* flac = drflac_open_memory(data, size, nullptr);
  if (flac == nullptr) return Result::kMalformed;
  StreamInfo info;
  info.channels = flac->channels;
  info.sample_rate = flac->sampleRate;
  info.total_frames = flac->totalPCMFrameCount;
  const Result result = stream(info, sink, [&](std::size_t frames, float* to) {
    return static_cast<std::size_t>(drflac_read_pcm_frames_f32(flac, frames, to));
  });
  drflac_close(flac);
  return result;
}

Result decode_mp3(const std::uint8_t* data, std::size_t size, const Sink& sink) {
  drmp3 mp3;
  if (!drmp3_init_memory(&mp3, data, size, nullptr)) return Result::kMalformed;
  StreamInfo info;
  info.channels = mp3.channels;
  info.sample_rate = mp3.sampleRate;
  const Result result = stream(info, sink, [&](std::size_t frames, float* to) {
    return static_cast<std::size_t>(drmp3_read_pcm_frames_f32(&mp3, frames, to));
  });
  drmp3_uninit(&mp3);
  return result;
}

Result decode_vorbis(const std::uint8_t* data, std::size_t size, const Sink& sink) {
  if (size > 0x7FFFFFFF) return Result::kMalformed;
  int error = 0;
  stb_vorbis* vorbis = stb_vorbis_open_memory(data, static_cast<int>(size), &error, nullptr);
  if (vorbis == nullptr) return Result::kMalformed;
  const stb_vorbis_info vi = stb_vorbis_get_info(vorbis);
  StreamInfo info;
  info.channels = vi.channels > 0 ? static_cast<unsigned>(vi.channels) : 0;
  info.sample_rate = vi.sample_rate;
  const Result result = stream(
      info, sink,
      [&](std::size_t frames, float* to) {
        const int got = stb_vorbis_get_samples_float_interleaved(vorbis, static_cast<int>(info.channels), to,
                                                                 static_cast<int>(frames * info.channels));
        return got > 0 ? static_cast<std::size_t>(got) : 0;
      },
      vorbis_to_wav_order(info.channels));
  stb_vorbis_close(vorbis);
  return result;
}

}  // namespace scraps::decoders
