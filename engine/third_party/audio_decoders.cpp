// The one place the audio decoders' implementations are compiled. Memory
// only: we read files ourselves (engine/core/sound.cpp) and hand over bytes,
// so none of them needs stdio. Third-party code: compiled without our
// warning flags (engine/third_party/README.md).
#include "third_party/audio_decoders.h"

#include <climits>
#include <cstddef>
#include <cstring>
#include <new>
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

// Puts each frame's channels in WAV order (`reorder` null: they already are).
void reorder_frames(float* chunk, std::size_t frames, unsigned channels, const int* reorder) {
  if (reorder == nullptr) return;
  float frame[kMaxChannels];
  for (std::size_t f = 0; f < frames; ++f) {
    float* at = chunk + f * channels;
    for (unsigned c = 0; c < channels; ++c) frame[c] = at[reorder[c]];
    for (unsigned c = 0; c < channels; ++c) at[c] = frame[c];
  }
}

bool info_is_valid(const StreamInfo& info) {
  return info.channels > 0 && info.channels <= kMaxChannels && info.sample_rate > 0 &&
         info.sample_rate <= kMaxSampleRate;
}

// Feeds the sink chunk by chunk from `read(frames, destination)` until the
// decoder runs dry or the sink says stop. The chunk buffer is fixed: memory
// never depends on how long the sound is; the sink decides what to keep.
template <typename ReadFn>
Result stream(const StreamInfo& info, const Sink& sink, ReadFn read, const int* reorder = nullptr) {
  if (!info_is_valid(info)) return Result::kMalformed;
  if (!sink.begin(sink.user, info)) return Result::kStopped;
  std::vector<float> buffer(kChunkFrames * info.channels);  // once per sound, at load time
  float* chunk = buffer.data();
  bool any = false;
  for (;;) {
    const std::size_t got = read(kChunkFrames, chunk);
    if (got == 0) break;
    any = true;
    reorder_frames(chunk, got, info.channels, reorder);
    if (!sink.frames(sink.user, chunk, got)) return Result::kStopped;
  }
  return any ? Result::kOk : Result::kMalformed;
}

// stb_vorbis 1.22 trusts the lengths inside a Vorbis comment header (the
// second packet: vendor string, then a count of comments, each with its
// length). A damaged or hostile file with a negative or oversized length
// makes it write outside its memory, or, when the allocation then fails,
// free pointers it never set (both found by core.music_rejects_bad_files).
// So before stb_vorbis sees a file, the Ogg pages are walked here to put
// that packet together, and every length in it must fit inside it. The
// rest of the setup stb_vorbis checks itself (upstream fuzzes it, in its
// default malloc mode, which is the mode used here).
bool vorbis_comment_header_is_sane(const std::uint8_t* data, std::size_t size) {
  std::vector<std::uint8_t> comment;  // packet 1 (packet 0 is the identification header)
  int packet = 0;
  std::size_t page = 0;
  while (packet < 2) {
    if (size < 27 || page > size - 27 || std::memcmp(data + page, "OggS", 4) != 0) return false;
    const std::size_t segments = data[page + 26];
    if (segments > size - page - 27) return false;
    const std::uint8_t* lacing = data + page + 27;
    std::size_t body = page + 27 + segments;
    for (std::size_t s = 0; s < segments && packet < 2; ++s) {
      const std::size_t length = lacing[s];
      if (length > size - body) return false;
      if (packet == 1) comment.insert(comment.end(), data + body, data + body + length);
      body += length;
      if (length < 255) ++packet;  // a lacing value under 255 ends a packet
    }
    page = body;
  }
  // 0x03 "vorbis", vendor length + vendor, comment count, each length +
  // text, then the framing byte.
  const std::size_t total = comment.size();
  if (total < 7 || comment[0] != 3 || std::memcmp(comment.data() + 1, "vorbis", 6) != 0) return false;
  std::size_t at = 7;
  auto read_length = [&](std::size_t& value) {
    if (total - at < 4) return false;
    value = static_cast<std::size_t>(comment[at]) | (static_cast<std::size_t>(comment[at + 1]) << 8) |
            (static_cast<std::size_t>(comment[at + 2]) << 16) | (static_cast<std::size_t>(comment[at + 3]) << 24);
    at += 4;
    return true;
  };
  std::size_t vendor = 0;
  if (!read_length(vendor) || vendor > total - at) return false;
  at += vendor;
  std::size_t count = 0;
  if (!read_length(count) || count > (total - at) / 4) return false;
  for (std::size_t i = 0; i < count; ++i) {
    std::size_t length = 0;
    if (!read_length(length) || length > total - at) return false;
    at += length;
  }
  return at < total;
}

stb_vorbis* open_vorbis(const std::uint8_t* data, std::size_t size) {
  if (data == nullptr || size == 0 || size > 0x7FFFFFFF) return nullptr;
  if (!vorbis_comment_header_is_sane(data, size)) return nullptr;
  int error = 0;
  return stb_vorbis_open_memory(data, static_cast<int>(size), &error, nullptr);
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
  stb_vorbis* vorbis = open_vorbis(data, size);
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

// ---------------------------------------------------------------- pull decoding

namespace {

// dr_libs allocate through these, so all their memory comes from operator
// new (counted by the allocation test). Each block remembers its size in a
// small header, which realloc needs to copy the old contents.
constexpr std::size_t kHeader =
    alignof(std::max_align_t) > sizeof(std::size_t) ? alignof(std::max_align_t) : sizeof(std::size_t);

void* heap_malloc(std::size_t size, void*) {
  void* block = ::operator new(size + kHeader, std::nothrow);
  if (block == nullptr) return nullptr;
  std::memcpy(block, &size, sizeof(size));
  return static_cast<unsigned char*>(block) + kHeader;
}

void heap_free(void* p, void*) {
  if (p != nullptr) ::operator delete(static_cast<unsigned char*>(p) - kHeader);
}

void* heap_realloc(void* p, std::size_t size, void* user) {
  if (p == nullptr) return heap_malloc(size, user);
  if (size == 0) {
    heap_free(p, user);
    return nullptr;
  }
  std::size_t old = 0;
  std::memcpy(&old, static_cast<unsigned char*>(p) - kHeader, sizeof(old));
  void* moved = heap_malloc(size, user);
  if (moved == nullptr) return nullptr;
  std::memcpy(moved, p, old < size ? old : size);
  heap_free(p, user);
  return moved;
}

template <typename Callbacks>
Callbacks heap_callbacks() {
  Callbacks callbacks;
  callbacks.pUserData = nullptr;
  callbacks.onMalloc = heap_malloc;
  callbacks.onRealloc = heap_realloc;
  callbacks.onFree = heap_free;
  return callbacks;
}

}  // namespace

struct PullDecoder::State {
  Codec codec = Codec::kWav;
  unsigned channels = 0;
  drwav wav{};
  bool wav_open = false;
  drflac* flac = nullptr;
  drmp3 mp3{};
  bool mp3_open = false;
  stb_vorbis* vorbis = nullptr;
  const int* reorder = nullptr;

  State() = default;
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  ~State() {
    if (wav_open) drwav_uninit(&wav);
    if (flac != nullptr) drflac_close(flac);
    if (mp3_open) drmp3_uninit(&mp3);
    if (vorbis != nullptr) stb_vorbis_close(vorbis);
  }
};

PullDecoder::PullDecoder() = default;
PullDecoder::~PullDecoder() = default;

void PullDecoder::close() { m_state.reset(); }

bool PullDecoder::open(Codec codec, const std::uint8_t* data, std::size_t size, StreamInfo& info) {
  close();
  info = StreamInfo();
  if (data == nullptr || size == 0) return false;
  std::unique_ptr<State> state(new (std::nothrow) State());
  if (state == nullptr) return false;
  state->codec = codec;
  switch (codec) {
    case Codec::kWav: {
      const drwav_allocation_callbacks callbacks = heap_callbacks<drwav_allocation_callbacks>();
      if (!drwav_init_memory(&state->wav, data, size, &callbacks)) return false;
      state->wav_open = true;
      info.channels = state->wav.channels;
      info.sample_rate = state->wav.sampleRate;
      info.total_frames = state->wav.totalPCMFrameCount;
      break;
    }
    case Codec::kFlac: {
      const drflac_allocation_callbacks callbacks = heap_callbacks<drflac_allocation_callbacks>();
      state->flac = drflac_open_memory(data, size, &callbacks);
      if (state->flac == nullptr) return false;
      info.channels = state->flac->channels;
      info.sample_rate = state->flac->sampleRate;
      info.total_frames = state->flac->totalPCMFrameCount;
      break;
    }
    case Codec::kMp3: {
      const drmp3_allocation_callbacks callbacks = heap_callbacks<drmp3_allocation_callbacks>();
      if (!drmp3_init_memory(&state->mp3, data, size, &callbacks)) return false;
      state->mp3_open = true;
      info.channels = state->mp3.channels;
      info.sample_rate = state->mp3.sampleRate;
      break;
    }
    case Codec::kVorbis: {
      state->vorbis = open_vorbis(data, size);
      if (state->vorbis == nullptr) return false;
      const stb_vorbis_info vi = stb_vorbis_get_info(state->vorbis);
      info.channels = vi.channels > 0 ? static_cast<unsigned>(vi.channels) : 0;
      info.sample_rate = vi.sample_rate;
      state->reorder = vorbis_to_wav_order(info.channels);
      break;
    }
  }
  if (!info_is_valid(info)) return false;
  state->channels = info.channels;
  m_state = std::move(state);
  return true;
}

std::size_t PullDecoder::read(float* interleaved, std::size_t frames) {
  if (m_state == nullptr || interleaved == nullptr || frames == 0) return 0;
  State& s = *m_state;
  std::size_t got = 0;
  switch (s.codec) {
    case Codec::kWav: got = static_cast<std::size_t>(drwav_read_pcm_frames_f32(&s.wav, frames, interleaved)); break;
    case Codec::kFlac: got = static_cast<std::size_t>(drflac_read_pcm_frames_f32(s.flac, frames, interleaved)); break;
    case Codec::kMp3: got = static_cast<std::size_t>(drmp3_read_pcm_frames_f32(&s.mp3, frames, interleaved)); break;
    case Codec::kVorbis: {
      const std::size_t most = static_cast<std::size_t>(INT_MAX) / s.channels;
      if (frames > most) frames = most;
      const int n = stb_vorbis_get_samples_float_interleaved(s.vorbis, static_cast<int>(s.channels), interleaved,
                                                             static_cast<int>(frames * s.channels));
      got = n > 0 ? static_cast<std::size_t>(n) : 0;
      break;
    }
  }
  if (got > frames) got = frames;
  reorder_frames(interleaved, got, s.channels, s.reorder);
  return got;
}

bool PullDecoder::rewind() {
  if (m_state == nullptr) return false;
  State& s = *m_state;
  switch (s.codec) {
    case Codec::kWav: return drwav_seek_to_pcm_frame(&s.wav, 0) != 0;
    case Codec::kFlac: return drflac_seek_to_pcm_frame(s.flac, 0) != 0;
    case Codec::kMp3: return drmp3_seek_to_pcm_frame(&s.mp3, 0) != 0;
    case Codec::kVorbis: return stb_vorbis_seek_start(s.vorbis) != 0;
  }
  return false;
}

}  // namespace scraps::decoders
