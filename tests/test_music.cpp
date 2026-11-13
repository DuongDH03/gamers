// Music: streaming decoders (core/music.h), the Mixer's music channel
// (core/mixer.h), the scene's `music` line and the client's SceneMusic.
// The test names start with the group they belong to (core., scene., views.).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/mixer.h"
#include "core/music.h"
#include "core/scene.h"
#include "core/serialize.h"
#include "core/sound.h"
#include "core/world.h"
#include "harness.h"
#include "test_world.h"
#include "views/scene_music.h"

using namespace scraps;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string audio_path(const std::string& name) { return std::string(SCRAPS_TEST_AUDIO_DIR) + "/" + name; }

std::vector<std::uint8_t> read_audio_file(const std::string& name) {
  std::ifstream in(audio_path(name), std::ios::binary);
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A 16-bit PCM WAV file in memory.
std::vector<std::uint8_t> make_wav(const std::vector<std::int16_t>& samples, std::uint32_t rate,
                                   std::uint16_t channels) {
  std::vector<std::uint8_t> bytes;
  auto text = [&bytes](const char* s) { bytes.insert(bytes.end(), s, s + 4); };
  auto u16 = [&bytes](std::uint32_t v) {
    bytes.push_back(static_cast<std::uint8_t>(v & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  };
  auto u32 = [&u16](std::uint32_t v) {
    u16(v & 0xFFFF);
    u16(v >> 16);
  };
  const std::uint32_t data_size = static_cast<std::uint32_t>(samples.size() * 2);
  text("RIFF");
  u32(36 + data_size);
  text("WAVE");
  text("fmt ");
  u32(16);
  u16(1);  // PCM
  u16(channels);
  u32(rate);
  u32(rate * channels * 2);
  u16(channels * 2u);
  u16(16);
  text("data");
  u32(data_size);
  for (std::int16_t s : samples) u16(static_cast<std::uint16_t>(s));
  return bytes;
}

// `frames` frames of one constant value (exact in 16 bits: 16384 is 0.5).
std::vector<std::uint8_t> constant_wav(std::int16_t value, std::size_t frames) {
  return make_wav(std::vector<std::int16_t>(frames, value), 48000, 1);
}

// A sine of `periods` whole periods in `frames` frames, so the file loops
// without a seam of its own.
std::vector<std::uint8_t> sine_wav(std::uint32_t rate, std::size_t frames, int periods, double amplitude) {
  std::vector<std::int16_t> samples(frames);
  for (std::size_t i = 0; i < frames; ++i) {
    const double v = amplitude * std::sin(2.0 * kPi * periods * static_cast<double>(i) / static_cast<double>(frames));
    samples[i] = static_cast<std::int16_t>(std::lround(v * 32767.0));
  }
  return make_wav(samples, rate, 1);
}

// Reads `frames` frames in pieces of awkward sizes, so every chunk and
// block boundary inside the stream is crossed at some odd point.
std::vector<float> read_frames(MusicStream& stream, std::size_t frames) {
  static const std::size_t kPieces[] = {1, 7, 511, 1000, 4097, 800};
  std::vector<float> out;
  std::vector<float> piece(4097 * kSoundChannels);
  for (std::size_t i = 0; out.size() < frames * kSoundChannels; ++i) {
    std::size_t want = kPieces[i % 6];
    want = std::min(want, frames - out.size() / kSoundChannels);
    const std::size_t got = stream.read(piece.data(), want);
    out.insert(out.end(), piece.begin(), piece.begin() + static_cast<std::ptrdiff_t>(got * kSoundChannels));
    if (got < want) break;
  }
  return out;
}

// Reads a stream that does not loop to its end.
std::vector<float> read_all(MusicStream& stream) { return read_frames(stream, std::size_t{1} << 26); }

std::size_t count_differences(const std::vector<float>& a, const std::vector<float>& b, std::size_t samples,
                              float tolerance) {
  std::size_t differ = 0;
  for (std::size_t i = 0; i < samples && i < a.size() && i < b.size(); ++i) {
    if (!(std::fabs(a[i] - b[i]) <= tolerance)) ++differ;
  }
  return differ;
}

// The largest change from one frame to the next, on either side.
float largest_step(const std::vector<float>& stereo) {
  float largest = 0.0f;
  for (std::size_t i = kSoundChannels; i < stereo.size(); ++i) {
    largest = std::max(largest, std::fabs(stereo[i] - stereo[i - kSoundChannels]));
  }
  return largest;
}

// ---------------------------------------------------------------- streaming

void music_streams_equal_full_decode(test::Context& ctx) {
  // Every format and several rates and channel counts: the stream, read in
  // awkward pieces, gives exactly what decoding the whole file gives.
  for (const char* file : {"tone_u8.wav", "tone_s16.wav", "tone_s24.wav", "tone_f32.wav", "tone.aiff", "tone.flac",
                           "tone_6ch.flac", "tone.mp3", "tone.ogg", "loop48.ogg", "loop48.mp3", "loop48.flac"}) {
    const std::vector<std::uint8_t> bytes = read_audio_file(file);
    SoundData full;
    std::string error;
    CHECK(decode_sound(bytes.data(), bytes.size(), full, error));
    const std::size_t frames = full.frames();

    MusicStream once;
    const bool opened = once.open(bytes, error);
    if (!opened) std::printf("    %s: %s\n", file, error.c_str());
    CHECK(opened);
    CHECK(once.format() == detect_audio_format(bytes.data(), bytes.size()));
    once.set_looping(false);
    const std::vector<float> streamed = read_all(once);
    CHECK(once.ended() && !once.failed() && once.loops() == 0);
    CHECK(streamed.size() == full.samples.size());
    const std::size_t differ = count_differences(streamed, full.samples, full.samples.size(), 0.0f);
    if (differ != 0) std::printf("    %s: %lu samples differ\n", file, static_cast<unsigned long>(differ));
    CHECK(differ == 0);

    // Looping, the same up to the last few frames (those already lean
    // toward the first frame of the next pass).
    MusicStream looping;
    CHECK(looping.open(bytes, error));
    const std::vector<float> first = read_frames(looping, frames - 4);
    CHECK(first.size() == (frames - 4) * kSoundChannels);
    CHECK(count_differences(first, full.samples, first.size(), 0.0f) == 0);
  }
}

void music_loops_without_a_gap(test::Context& ctx) {
  std::string error;
  // At 48 kHz nothing is resampled: the second and third passes repeat the
  // first exactly, and the seam is no steeper than the sine itself.
  {
    MusicStream stream;
    CHECK(stream.open(sine_wav(48000, 4800, 10, 0.5), error));
    const std::vector<float> out = read_frames(stream, 4800 * 3 + 1234);
    CHECK(out.size() == (4800 * 3 + 1234) * kSoundChannels);
    CHECK(count_differences(std::vector<float>(out.begin() + 4800 * kSoundChannels, out.end()), out,
                            (4800 * 2 + 1234) * kSoundChannels, 1e-6f) == 0);
    const float sine_step = static_cast<float>(0.5 * 2.0 * kPi * 10.0 / 4800.0);
    CHECK(largest_step(out) <= sine_step * 1.05f);  // 5 %: 16-bit rounding
    CHECK(stream.loops() == 3 && !stream.failed());
  }
  // At 44.1 kHz the resampler interpolates across the seam: still no step
  // larger than the sine's own (a gap or a click would jump by up to 0.5).
  {
    MusicStream stream;
    CHECK(stream.open(sine_wav(44100, 4410, 10, 0.5), error));
    const std::vector<float> out = read_frames(stream, 48000);
    CHECK(out.size() == 48000 * kSoundChannels);
    const float sine_step = static_cast<float>(0.5 * 2.0 * kPi * 100.0 / 48000.0);
    CHECK(largest_step(out) <= sine_step * 1.05f);
    // 48,000 output frames need 44,100 source frames, or one more: 9 or 10 wraps.
    CHECK(stream.loops() == 9 || stream.loops() == 10);
  }
  // Real files at 48 kHz in every format: after the wrap the decoder starts
  // over exactly where it first started (nothing added, nothing dropped).
  for (const char* file : {"loop48.ogg", "loop48.mp3", "loop48.flac", "tone_s24.wav", "tone_6ch.flac"}) {
    const std::vector<std::uint8_t> bytes = read_audio_file(file);
    MusicStream once;
    CHECK(once.open(bytes, error));
    once.set_looping(false);
    const std::size_t frames = read_all(once).size() / kSoundChannels;
    MusicStream looping;
    CHECK(looping.open(bytes, error));
    const std::vector<float> out = read_frames(looping, frames * 2 + 100);
    CHECK(out.size() == (frames * 2 + 100) * kSoundChannels);
    const std::vector<float> second_pass(out.begin() + static_cast<std::ptrdiff_t>(frames * kSoundChannels), out.end());
    const std::size_t differ = count_differences(second_pass, out, (frames + 100) * kSoundChannels, 1e-6f);
    if (differ != 0) {
      std::printf("    %s: %lu samples differ after the wrap\n", file, static_cast<unsigned long>(differ));
    }
    CHECK(differ == 0);
    CHECK(looping.loops() == 2);
  }
  // Longer than any sound effect may be (30 s): refused as an effect,
  // streamed as music, past its end and round again. One OGG with a length
  // in its header, one MP3 without.
  for (const char* file : {"music_40s.ogg", "long_35s.mp3"}) {
    const std::vector<std::uint8_t> bytes = read_audio_file(file);
    SoundData effect;
    CHECK(!decode_sound(bytes.data(), bytes.size(), effect, error));
    MusicStream stream;
    CHECK(stream.open(bytes, error));
    std::vector<float> block(800 * kSoundChannels);
    std::size_t frames = 0;
    std::size_t frames_in_first_pass = 0;
    float peak_after_30s = 0.0f;
    while (frames < 41 * 48000 && !stream.ended()) {
      CHECK(stream.read(block.data(), 800) == 800);
      frames += 800;
      if (stream.loops() == 0) frames_in_first_pass = frames;
      if (frames > 31 * 48000 && stream.loops() == 0) {
        for (float v : block) peak_after_30s = std::max(peak_after_30s, std::fabs(v));
      }
    }
    CHECK(frames_in_first_pass > 34 * 48000);
    CHECK(peak_after_30s > 0.05f);  // still sound after 30 s
    CHECK(stream.loops() == 1 && !stream.failed());
  }
}

void music_rejects_bad_files(test::Context& ctx) {
  MusicStream stream;
  std::string error;
  CHECK(!open_music(audio_path("missing.ogg"), stream, error));
  CHECK(error.find("cannot open") != std::string::npos && !stream.is_open());

  const std::string text = "this is not music";
  CHECK(!stream.open(std::vector<std::uint8_t>(text.begin(), text.end()), error));
  CHECK(error.find("recognise") != std::string::npos);
  CHECK(!stream.open(std::vector<std::uint8_t>(), error));
  CHECK(!stream.open(read_audio_file("tone.opus"), error));
  CHECK(error.find("Opus") != std::string::npos);
  CHECK(!stream.open(read_audio_file("tone.m4a"), error));
  CHECK(error.find("AAC") != std::string::npos);
  CHECK(!stream.open(make_wav(std::vector<std::int16_t>(), 48000, 1), error));  // a header, no sound
  CHECK(!stream.is_open());
  float frame[2] = {9.0f, 9.0f};
  CHECK(stream.read(frame, 1) == 0 && stream.ended());  // a closed stream reads nothing

  // A loop too short to loop (100 frames): it plays once, then the stream
  // reports failure instead of seeking back every few frames.
  CHECK(stream.open(constant_wav(16384, 100), error));
  const std::vector<float> once = read_frames(stream, 1000);
  CHECK(once.size() == 100 * kSoundChannels && stream.ended() && stream.failed());

  // OGG files whose comment header lies about its lengths: stb_vorbis
  // trusts them and writes outside its memory or frees pointers it never
  // set, so the decoder face checks that header first (third_party/
  // audio_decoders.cpp). Refused for music and for sound effects alike.
  {
    const std::vector<std::uint8_t> good = read_audio_file("loop48.ogg");
    const std::uint8_t kComment[] = {3, 'v', 'o', 'r', 'b', 'i', 's'};
    const auto header = std::search(good.begin(), good.end(), std::begin(kComment), std::end(kComment));
    CHECK(header != good.end());
    const std::size_t vendor_at = static_cast<std::size_t>(header - good.begin()) + 7;
    const std::size_t vendor_length = good[vendor_at] | (good[vendor_at + 1] << 8);
    const std::size_t count_at = vendor_at + 4 + vendor_length;
    for (const std::size_t at : {vendor_at, count_at}) {
      for (const std::uint8_t high : {std::uint8_t{0xFF}, std::uint8_t{0x7F}}) {
        std::vector<std::uint8_t> lying = good;
        lying[at] = 0xFF;
        lying[at + 1] = 0xFF;
        lying[at + 2] = 0xFF;
        lying[at + 3] = high;
        CHECK(!stream.open(lying, error));
        SoundData effect;
        CHECK(!decode_sound(lying.data(), lying.size(), effect, error));
      }
    }
    CHECK(stream.open(good, error));  // the unchanged file is fine
  }

  // Damaged files: cut short, bytes changed, a header then noise. Opening
  // may work or not; reading never crashes and never leaves -1..1.
  std::uint32_t seed = 4242;
  auto next = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
  };
  int opened = 0;
  bool bounded = true;
  std::vector<float> block(800 * kSoundChannels);
  auto play_a_second = [&](const std::vector<std::uint8_t>& bytes) {
    MusicStream damaged;
    std::string reason;
    if (!damaged.open(bytes, reason)) return;
    ++opened;
    for (int i = 0; i < 60 && !damaged.ended(); ++i) {
      const std::size_t got = damaged.read(block.data(), 800);
      for (std::size_t s = 0; s < got * kSoundChannels; ++s) {
        if (!(block[s] >= -1.0f && block[s] <= 1.0f)) bounded = false;
      }
    }
  };
  for (const char* file : {"tone_s16.wav", "tone.aiff", "tone.flac", "tone.mp3", "tone.ogg", "loop48.ogg"}) {
    const std::vector<std::uint8_t> bytes = read_audio_file(file);
    for (std::size_t length = 0; length < bytes.size(); length += 1 + bytes.size() / 37) {
      play_a_second(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length)));
    }
    for (int round = 0; round < 60; ++round) {
      std::vector<std::uint8_t> corrupt = bytes;
      for (int k = 0; k < 4; ++k) corrupt[next() % corrupt.size()] = static_cast<std::uint8_t>(next());
      play_a_second(corrupt);
      const std::size_t header = std::min<std::size_t>(64, bytes.size());
      std::vector<std::uint8_t> noise(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(header));
      noise.resize(64 + next() % 4096);
      for (std::size_t i = 64; i < noise.size(); ++i) noise[i] = static_cast<std::uint8_t>(next());
      play_a_second(noise);
    }
  }
  CHECK(bounded);
  std::printf("    %d damaged files still opened\n", opened);
}

// ---------------------------------------------------------------- mixer

void mixer_crossfades_music_on_the_curve(test::Context& ctx) {
  std::string error;
  // Tracks of one constant level (at 48 kHz: nothing is resampled), so the
  // mix shows each track's gain directly. A is 0.5, B is 0.25.
  MusicStream a;
  MusicStream b;
  CHECK(a.open(constant_wav(16384, 4800), error));
  CHECK(b.open(constant_wav(8192, 4800), error));
  Mixer mixer;
  mixer.play_music(MusicStream(), 1.0f, 0.0);  // not open: ignored
  CHECK(mixer.music_tracks() == 0 && !mixer.music_playing());

  std::vector<float> out(6000 * kSoundChannels);
  auto mix = [&](std::size_t frames) {
    // In pieces that cross the mixer's music blocks at odd places.
    for (std::size_t done = 0; done < frames;) {
      const std::size_t piece = std::min<std::size_t>(700, frames - done);
      mixer.mix(out.data() + done * kSoundChannels, piece);
      done += piece;
    }
  };
  auto off_by = [&](std::size_t from, std::size_t to, auto expected) {
    double worst = 0.0;
    for (std::size_t f = from; f < to; ++f) {
      const double want = expected(f);
      worst = std::max(worst, std::fabs(out[f * kSoundChannels] - want));
      worst = std::max(worst, std::fabs(out[f * kSoundChannels + 1] - want));
    }
    return worst;
  };

  mixer.play_music(std::move(a), 1.0f, 0.0);  // no fade-in: full at once
  CHECK(!a.is_open() && mixer.music_playing() && mixer.music_tracks() == 1);
  mix(1000);
  CHECK(off_by(0, 1000, [](std::size_t) { return 0.5; }) < 1e-6);

  // B crossfades in over 0.1 s (4800 frames): A follows cos, B follows sin.
  mixer.play_music(std::move(b), 1.0f, 0.1);
  CHECK(mixer.music_tracks() == 2 && mixer.music_playing());
  mix(6000);
  CHECK(off_by(0, 4800, [](std::size_t f) {
          const double p = static_cast<double>(f) / 4800.0;
          return 0.5 * std::cos(p * kPi / 2) + 0.25 * std::sin(p * kPi / 2);
        }) < 1e-5);
  CHECK_NEAR(out[2400 * kSoundChannels], (0.5 + 0.25) * std::sqrt(0.5), 1e-5);  // halfway, both at 0.71
  CHECK(off_by(4800, 6000, [](std::size_t) { return 0.25; }) < 1e-6);
  CHECK(mixer.music_tracks() == 1);

  // A stop fades out over 0.05 s (2400 frames) on the cos curve.
  mixer.stop_music(0.05);
  CHECK(!mixer.music_playing() && mixer.current_music() == nullptr);
  mix(3000);
  CHECK(off_by(0, 2400, [](std::size_t f) { return 0.25 * std::cos(static_cast<double>(f) / 2400.0 * kPi / 2); }) <
        1e-5);
  CHECK(off_by(2400, 3000, [](std::size_t) { return 0.0; }) < 1e-7);
  CHECK(mixer.music_tracks() == 0);

  // Even an instant stop takes 10 ms (480 frames), so it never clicks.
  MusicStream c;
  CHECK(c.open(constant_wav(16384, 4800), error));
  mixer.play_music(std::move(c), 0.5f);  // at volume 0.5: 0.25
  mixer.stop_music(0.0);
  mix(1000);
  CHECK(out[0] > 0.24f && out[240 * kSoundChannels] > 0.05f && out[480 * kSoundChannels] == 0.0f);

  // Music and effects have separate volumes. An effect of 0.1 and music
  // of 0.25: the effects gain applies at once, the music gain glides.
  MusicStream d;
  CHECK(d.open(constant_wav(16384, 4800), error));
  mixer.play_music(std::move(d), 0.5f);
  SoundData effect;
  effect.samples.assign(100000 * kSoundChannels, 0.1f);
  mixer.play(mixer.add(effect));
  mixer.set_effects_gain(0.5f);
  mix(100);
  CHECK(off_by(0, 100, [](std::size_t) { return 0.25 + 0.05; }) < 1e-6);
  mixer.set_music_gain(0.4f);  // a pause duck
  CHECK(mixer.music_gain() == 0.4f && mixer.effects_gain() == 0.5f);
  mix(3000);
  CHECK(out[0] < 0.3f && out[0] > 0.29f);                         // gliding, not jumping
  CHECK(largest_step(std::vector<float>(out.begin(), out.begin() + 3000 * kSoundChannels)) < 0.25f / 2000.0f);
  CHECK(off_by(2400, 3000, [](std::size_t) { return 0.25 * 0.4 + 0.05; }) < 1e-6);  // within 50 ms: there

  // Clearing the effects (a scene reload) leaves the music playing.
  mixer.clear();
  CHECK(mixer.music_playing());
  mix(10);
  CHECK(off_by(0, 10, [](std::size_t) { return 0.25 * 0.4; }) < 1e-6);
}

// ---------------------------------------------------------------- scene

void music_line_parses_and_round_trips(test::Context& ctx) {
  Scene scene;
  std::string error;
  CHECK(parse_scene(test::kBoxScene, scene, error));
  CHECK(path_view(scene.level.music.path).empty());  // no line: no music
  CHECK(parse_scene(std::string(test::kBoxScene) + "music sounds/theme.ogg volume 0.6\n", scene, error));
  CHECK(path_view(scene.level.music.path) == "sounds/theme.ogg" && scene.level.music.volume == 0.6f);
  CHECK(parse_scene(std::string(test::kBoxScene) + "music theme.mp3\n", scene, error));
  CHECK(scene.level.music.volume == 1.0f);

  CHECK(!parse_scene(std::string(test::kBoxScene) + "music ../theme.ogg\n", scene, error));
  CHECK(error.find("relative") != std::string::npos);
  CHECK(!parse_scene(std::string(test::kBoxScene) + "music a.ogg volume 5\n", scene, error));
  CHECK(!parse_scene(std::string(test::kBoxScene) + "music a.ogg loud\n", scene, error));
  CHECK(!parse_scene(std::string(test::kBoxScene) + "music\n", scene, error));
  CHECK(!parse_scene(std::string(test::kBoxScene) + "music a.ogg\nmusic b.ogg\n", scene, error));
  CHECK(error.find("twice") != std::string::npos && error.find("line") != std::string::npos);

  // In the World's bytes (saves, replays): with music and without.
  for (const char* line : {"music sounds/theme.ogg volume 0.6\n", ""}) {
    CHECK(parse_scene(std::string(test::kBoxScene) + line, scene, error));
    World world = make_world(scene, MovementRules());
    std::vector<std::uint8_t> bytes;
    ByteWriter writer(bytes);
    write_world(writer, world);
    World loaded;
    ByteReader reader(bytes.data(), bytes.size());
    CHECK(read_world(reader, loaded));
    CHECK(path_view(loaded.level.music.path) == path_view(world.level.music.path));
    CHECK(loaded.level.music.volume == world.level.music.volume);
  }
  // Hostile bytes: a path outside the folder, or a volume out of range.
  World world = make_world(scene, MovementRules());
  world.level.music.path = make_asset_path("../../secret.ogg");
  std::vector<std::uint8_t> bytes;
  ByteWriter writer(bytes);
  write_world(writer, world);
  World loaded;
  ByteReader reader(bytes.data(), bytes.size());
  CHECK(!read_world(reader, loaded));
  world.level.music.path = make_asset_path("ok.ogg");
  world.level.music.volume = 9.0f;
  bytes.clear();
  ByteWriter writer2(bytes);
  write_world(writer2, world);
  ByteReader reader2(bytes.data(), bytes.size());
  CHECK(!read_world(reader2, loaded));
}

// ---------------------------------------------------------------- client

void scene_music_keeps_playing_across_reloads(test::Context& ctx) {
  const std::string dir = SCRAPS_TEST_AUDIO_DIR;
  Mixer mixer;
  SceneMusic music;
  std::string error;
  std::vector<float> out(4800 * kSoundChannels);
  Level level;
  level.music.path = make_asset_path("music_40s.ogg");
  CHECK(music.update(level, dir, mixer, error));
  CHECK(music.path() == "music_40s.ogg" && mixer.music_playing() && mixer.music_tracks() == 1);
  const MusicStream* first = mixer.current_music();
  mixer.mix(out.data(), 4800);

  // A hot reload of a scene with the same music: the same stream plays on
  // (no restart, no crossfade); only its volume follows the scene.
  level.music.volume = 0.5f;
  CHECK(music.update(level, dir, mixer, error));
  CHECK(mixer.current_music() == first && mixer.music_tracks() == 1);

  // Another file: it crossfades in.
  level.music.path = make_asset_path("loop48.ogg");
  CHECK(music.update(level, dir, mixer, error));
  CHECK(music.path() == "loop48.ogg" && mixer.music_tracks() == 2 && mixer.current_music() != first);
  for (int i = 0; i < 20; ++i) mixer.mix(out.data(), 4800);  // 2 s: the crossfade is over
  CHECK(mixer.music_tracks() == 1);

  // A file that fails: reported, and the old music fades out.
  level.music.path = make_asset_path("no-such-file.ogg");
  CHECK(!music.update(level, dir, mixer, error));
  CHECK(error.find("no-such-file.ogg") != std::string::npos && !mixer.music_playing() && music.path().empty());

  // No music line: silence.
  level.music.path = make_asset_path("loop48.ogg");
  CHECK(music.update(level, dir, mixer, error));
  level.music.path = AssetPath{};
  CHECK(music.update(level, dir, mixer, error));
  CHECK(!mixer.music_playing() && music.path().empty());
}

void bundled_music_plays_in_every_scene(test::Context& ctx) {
  for (const char* name : {"top-down", "side", "isometric", "crowd"}) {
    Scene scene;
    std::string error;
    CHECK(load_scene(SCRAPS_SANDBOX_SCENE_DIR, name, scene, error));
    CHECK(!path_view(scene.level.music.path).empty());
    Mixer mixer;
    SceneMusic music;
    const bool ok = music.update(scene.level, SCRAPS_SANDBOX_SCENE_DIR, mixer, error);
    if (!ok) std::printf("    %s: %s\n", name, error.c_str());
    CHECK(ok && mixer.music_playing());
    const MusicStream* stream = mixer.current_music();
    CHECK(stream != nullptr && stream->format() == AudioFormat::kOggVorbis);
    std::vector<float> out(48000 * kSoundChannels);
    mixer.mix(out.data(), 48000);  // the first second, past the fade-in
    float peak = 0.0f;
    for (std::size_t i = 24000 * kSoundChannels; i < out.size(); ++i) peak = std::max(peak, std::fabs(out[i]));
    CHECK(peak > 0.05f);  // it is audible
  }
}

}  // namespace

void register_music_tests(test::Registry& registry) {
  registry.add("core.music_streams_equal_full_decode", music_streams_equal_full_decode);
  registry.add("core.music_loops_without_a_gap", music_loops_without_a_gap);
  registry.add("core.music_rejects_bad_files", music_rejects_bad_files);
  registry.add("core.mixer_crossfades_music_on_the_curve", mixer_crossfades_music_on_the_curve);
  registry.add("scene.music_line_parses_and_round_trips", music_line_parses_and_round_trips);
  registry.add("views.scene_music_keeps_playing_across_reloads", scene_music_keeps_playing_across_reloads);
  registry.add("views.bundled_music_plays_in_every_scene", bundled_music_plays_in_every_scene);
}
