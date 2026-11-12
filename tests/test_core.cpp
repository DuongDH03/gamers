#include <algorithm>
#include <iterator>
#include <initializer_list>
#include <cstdio>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "core/file_watch.h"
#include "core/fixed_step.h"
#include "core/image.h"
#include "core/jobs.h"
#include "core/mixer.h"
#include "core/sound.h"
#include "core/serialize.h"
#include "core/world.h"
#include "harness.h"
#include "sandbox/sandbox.h"
#include "test_world.h"
#include "views/view_factory.h"

using namespace scraps;

namespace {

// ---------------------------------------------------------------- loop (stage 1)

void fixed_step_keeps_60_ticks_per_second(test::Context& ctx) {
  const int frame_rates[] = {24, 30, 60, 75, 144, 240, 1000};
  for (int fps : frame_rates) {
    FixedStepClock clock;
    long ticks = 0;
    for (int frame = 0; frame < fps * 10; ++frame) {
      ticks += clock.advance(1.0 / fps);
      CHECK(clock.alpha() >= 0.0f && clock.alpha() < 1.0f);
    }
    // Ten seconds of frames at any rate are 600 ticks (one may still be pending).
    CHECK(ticks >= 599 && ticks <= 600);
  }
}

void fixed_step_drops_backlog_after_a_stall(test::Context& ctx) {
  FixedStepClock clock(1.0 / 60.0, 8);
  CHECK(clock.advance(5.0) == 8);  // a 5 s stall does not run 300 ticks at once
  CHECK(clock.alpha() < 1.0f);
  CHECK(clock.advance(0.0) == 0);  // and the backlog is gone
  CHECK(clock.advance(-1.0) == 0); // time never runs backwards
}

// ---------------------------------------------------------------- bytes

void byte_reader_never_reads_past_the_end(test::Context& ctx) {
  std::vector<std::uint8_t> bytes;
  ByteWriter writer(bytes);
  writer.write_u16(0xBEEF);
  ByteReader reader(bytes.data(), bytes.size());
  CHECK(reader.read_u16() == 0xBEEF);
  CHECK(reader.ok());
  CHECK(reader.read_u32() == 0);
  CHECK(!reader.ok());
  CHECK(reader.remaining() == 0);
}

void bytes_are_little_endian_and_round_trip(test::Context& ctx) {
  std::vector<std::uint8_t> bytes;
  ByteWriter writer(bytes);
  writer.write_u32(0x11223344u);
  writer.write_f32(-2.5f);
  writer.write_u64(0x0102030405060708ull);
  writer.write_i32(-7);
  CHECK(bytes[0] == 0x44 && bytes[1] == 0x33 && bytes[2] == 0x22 && bytes[3] == 0x11);
  ByteReader reader(bytes.data(), bytes.size());
  CHECK(reader.read_u32() == 0x11223344u);
  CHECK(reader.read_f32() == -2.5f);
  CHECK(reader.read_u64() == 0x0102030405060708ull);
  CHECK(reader.read_i32() == -7);
  CHECK(reader.ok() && reader.remaining() == 0);
}

// ---------------------------------------------------------------- entities (stage 4)

void stale_handles_stop_resolving(test::Context& ctx) {
  World world = test::make_box_world(test::flat_rules());
  const EntityId a = spawn_entity(world, 1, Vec3(1.5f, 1.5f, 1.0f));
  const EntityId b = spawn_entity(world, 1, Vec3(2.5f, 1.5f, 1.0f));
  CHECK(find_entity(world, a) != nullptr);
  CHECK(destroy_entity(world, a));
  CHECK(find_entity(world, a) == nullptr);
  CHECK(!destroy_entity(world, a));
  const EntityId c = spawn_entity(world, 1, Vec3(3.5f, 1.5f, 1.0f));
  CHECK(c.index == a.index);            // the slot is reused...
  CHECK(c.generation != a.generation);  // ...with a new generation
  CHECK(find_entity(world, a) == nullptr);
  CHECK(find_entity(world, b) != nullptr);
  CHECK(find_entity(world, c) != nullptr);
  CHECK(spawn_entity(world, 99, Vec3()).is_valid() == false);  // unknown archetype
}

void world_copies_are_independent(test::Context& ctx) {
  World original = test::make_box_world(test::flat_rules());
  test::step(original, test::one(make_command(CommandType::kPlayerJoin, 0)));
  World copy = original;
  for (int i = 0; i < 30; ++i) {
    test::step(copy, test::one(make_command(CommandType::kMove, 0, Vec3(0.0f, 1.0f, 0.0f))));
  }
  const Entity* before = find_entity(original, player_entity(original, 0));
  const Entity* after = find_entity(copy, player_entity(copy, 0));
  CHECK(before != nullptr && after != nullptr);
  if (before == nullptr || after == nullptr) return;
  CHECK(after->position.y > before->position.y + 1.0f);
  CHECK(original.tick == 1);
  CHECK(copy.tick == 31);
}

// ---------------------------------------------------------------- simulate (stages 3, 6)

void join_move_and_leave(test::Context& ctx) {
  World world = test::make_box_world(test::flat_rules());
  test::step(world, test::one(make_command(CommandType::kPlayerJoin, 3)));
  const EntityId id = player_entity(world, 3);
  const Entity* player = find_entity(world, id);
  CHECK(player != nullptr);
  if (player == nullptr) return;
  CHECK(player->player == 3);
  CHECK(player->position == Vec3(1.5f, 2.5f, 1.0f));

  // Joining twice does not make a second entity.
  const std::size_t alive = count_alive(world);
  test::step(world, test::one(make_command(CommandType::kPlayerJoin, 3)));
  CHECK(count_alive(world) == alive);

  // Move down (+y) for half a second at speed 5: about 2.5 units.
  for (int i = 0; i < 30; ++i) test::step(world, test::one(make_command(CommandType::kMove, 3, Vec3(0.0f, 1.0f, 0.0f))));
  player = find_entity(world, id);
  CHECK(player != nullptr);
  if (player != nullptr) CHECK_NEAR(player->position.y, 5.0f, 0.01);  // from 2.5

  // An over-long direction is clamped to length 1, NaN is ignored.
  test::step(world, test::one(make_command(CommandType::kMove, 3, Vec3(100.0f, 0.0f, 0.0f))));
  player = find_entity(world, id);
  if (player != nullptr) CHECK_NEAR(player->move_intent.x, 1.0f, 1e-6);
  test::step(world, test::one(make_command(CommandType::kMove, 3, Vec3(std::nanf(""), 0.0f, 0.0f))));
  player = find_entity(world, id);
  if (player != nullptr) CHECK(player->move_intent == Vec2());

  test::step(world, test::one(make_command(CommandType::kPlayerLeave, 3)));
  CHECK(find_entity(world, id) == nullptr);
  CHECK(!player_entity(world, 3).is_valid());
}

void walls_stop_movement_and_allow_sliding(test::Context& ctx) {
  World world = test::make_box_world(test::flat_rules());
  test::step(world, test::one(make_command(CommandType::kPlayerJoin, 0)));
  // Spawn (1.5, 2.5); the wall cell is x in [3, 4], y in [2, 3], z in [1, 2].
  for (int i = 0; i < 60; ++i) test::step(world, test::one(make_command(CommandType::kMove, 0, Vec3(1.0f, 0.0f, 0.0f))));
  const Entity* player = find_entity(world, player_entity(world, 0));
  CHECK(player != nullptr);
  if (player == nullptr) return;
  CHECK(player->position.x < 3.0f - 0.3f);           // never inside the wall
  CHECK(player->position.x > 3.0f - 0.3f - 0.01f);   // but right against it
  CHECK(player->velocity.x == 0.0f);

  // Pushing diagonally into the wall slides along it instead of sticking.
  const float y_before = player->position.y;
  for (int i = 0; i < 10; ++i) test::step(world, test::one(make_command(CommandType::kMove, 0, Vec3(1.0f, 1.0f, 0.0f))));
  player = find_entity(world, player_entity(world, 0));
  if (player != nullptr) CHECK(player->position.y > y_before + 0.3f);
}

void gravity_lands_and_jumps_only_from_the_ground(test::Context& ctx) {
  World world = test::make_box_world(test::gravity_rules());
  test::step(world, test::one(make_command(CommandType::kPlayerJoin, 0)));
  test::step(world);
  const EntityId id = player_entity(world, 0);
  const Entity* player = find_entity(world, id);
  CHECK(player != nullptr);
  if (player == nullptr) return;
  CHECK(player->on_ground);
  CHECK_NEAR(player->position.z, 1.0f, 0.001);

  test::step(world, test::one(make_command(CommandType::kJump, 0)));
  player = find_entity(world, id);
  CHECK(player->position.z > 1.1f);
  CHECK(!player->on_ground);

  test::step(world, test::one(make_command(CommandType::kJump, 0)));  // in the air: ignored
  player = find_entity(world, id);
  CHECK(player->velocity.z < 10.0f - 0.4f);

  for (int i = 0; i < 60; ++i) test::step(world);
  player = find_entity(world, id);
  CHECK(player->on_ground);
  CHECK_NEAR(player->position.z, 1.0f, 0.001);
}

void the_pit_removes_things_and_respawns_players(test::Context& ctx) {
  World world = test::make_box_world(test::gravity_rules());
  CHECK(count_alive(world) == 1);  // the rock over the pit
  for (int i = 0; i < 90; ++i) test::step(world);
  CHECK(count_alive(world) == 0);  // it fell below kill_z and was removed

  test::step(world, test::one(make_command(CommandType::kPlayerJoin, 0)));
  const EntityId id = player_entity(world, 0);
  // Walk diagonally into the corner over the pit at (5, 5), fall, respawn.
  bool fell = false;
  bool respawned = false;
  for (int i = 0; i < 240 && !respawned; ++i) {
    test::step(world, test::one(make_command(CommandType::kMove, 0, Vec3(1.0f, 1.0f, 0.0f))));
    const Entity* player = find_entity(world, id);  // the same entity throughout
    CHECK(player != nullptr);
    if (player == nullptr) return;
    if (player->position.z < -1.0f) fell = true;
    if (fell && player->position.z >= 0.99f && player->position.x < 2.0f) respawned = true;
  }
  CHECK(fell);
  CHECK(respawned);
}

void side_rules_lock_the_y_axis(test::Context& ctx) {
  MovementRules rules = test::gravity_rules();
  rules.move_y = false;
  World world = test::make_box_world(rules);
  test::step(world, test::one(make_command(CommandType::kPlayerJoin, 0)));
  for (int i = 0; i < 20; ++i) test::step(world, test::one(make_command(CommandType::kMove, 0, Vec3(0.0f, 1.0f, 0.0f))));
  const Entity* player = find_entity(world, player_entity(world, 0));
  CHECK(player != nullptr);
  if (player != nullptr) CHECK(player->position.y == 2.5f);
}

void random_numbers_live_in_the_world(test::Context& ctx) {
  World a = test::make_box_world(test::flat_rules());
  World b = a;
  for (int i = 0; i < 100; ++i) CHECK(next_random(a) == next_random(b));
  const float r = random_unit(a);
  CHECK(r >= 0.0f && r < 1.0f);
}

// ---------------------------------------------------------------- serialization (Rule 5)

std::vector<std::uint8_t> world_bytes(const World& world) {
  std::vector<std::uint8_t> bytes;
  ByteWriter writer(bytes);
  write_world(writer, world);
  return bytes;
}

void world_round_trips_through_bytes(test::Context& ctx) {
  World world = test::make_box_world(test::gravity_rules());
  test::step(world, test::one(make_command(CommandType::kPlayerJoin, 0)));
  test::step(world, test::one(make_command(CommandType::kPlayerJoin, 1)));
  for (int i = 0; i < 20; ++i) test::step(world, test::one(make_command(CommandType::kMove, 1, Vec3(1.0f, 0.0f, 0.0f))));
  destroy_entity(world, player_entity(world, 0));  // leaves a free slot to serialize

  const std::vector<std::uint8_t> bytes = world_bytes(world);
  World loaded;
  ByteReader reader(bytes.data(), bytes.size());
  CHECK(read_world(reader, loaded));
  CHECK(reader.remaining() == 0);
  CHECK(world_bytes(loaded) == bytes);
  CHECK(loaded.free_slots == world.free_slots);
  CHECK(player_entity(loaded, 1) == player_entity(world, 1));
}

void truncated_or_random_bytes_are_rejected_safely(test::Context& ctx) {
  World world = test::make_box_world(test::gravity_rules());
  test::step(world, test::one(make_command(CommandType::kPlayerJoin, 0)));
  const std::vector<std::uint8_t> bytes = world_bytes(world);

  int accepted = 0;
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    World loaded;
    ByteReader reader(bytes.data(), length);
    if (read_world(reader, loaded)) ++accepted;
  }
  CHECK(accepted == 0);

  // Corrupt one byte at a time and random buffers: must never crash.
  std::uint32_t seed = 12345;
  auto next = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
  };
  for (int round = 0; round < 2000; ++round) {
    std::vector<std::uint8_t> corrupt = bytes;
    corrupt[next() % corrupt.size()] = static_cast<std::uint8_t>(next());
    World loaded;
    ByteReader reader(corrupt.data(), corrupt.size());
    read_world(reader, loaded);

    std::vector<std::uint8_t> noise(next() % 512);
    for (std::uint8_t& b : noise) b = static_cast<std::uint8_t>(next());
    World target = world;
    ByteReader noise_reader(noise.data(), noise.size());
    read_world_state(noise_reader, target);
  }
  CHECK(true);  // reaching here means no crash
}

// Values that would be legal bytes but dangerous state are refused.
void hostile_snapshot_values_are_rejected(test::Context& ctx) {
  World world = test::make_box_world(test::flat_rules());
  test::step(world, test::one(make_command(CommandType::kPlayerJoin, 0)));
  auto accepted = [&world](void (*corrupt)(World&)) {
    World bad = world;
    corrupt(bad);
    std::vector<std::uint8_t> bytes;
    ByteWriter writer(bytes);
    write_world_state(writer, bad);
    World target = world;
    ByteReader reader(bytes.data(), bytes.size());
    return read_world_state(reader, target);
  };
  CHECK(accepted([](World&) {}));  // the untouched world is fine
  CHECK(!accepted([](World& w) { w.entities[0].size = Vec3(1.0e7f, 1.0f, 1.0f); }));
  CHECK(!accepted([](World& w) { w.entities[0].position = Vec3(3.0e9f, 0.0f, 0.0f); }));
  CHECK(!accepted([](World& w) { w.entities[0].move_speed = -1.0f; }));
  CHECK(!accepted([](World& w) {  // the same dead slot twice in the free list
    destroy_entity(w, entity_id_at(w, 0));
    w.free_slots.push_back(w.free_slots.back());
    w.entities.emplace_back();  // keep the dead-slot count equal to the list length
  }));
}

void entities_that_leave_the_world_are_removed(test::Context& ctx) {
  World world = test::make_box_world(test::flat_rules());
  // A ghost: moves, collides with nothing, never stops.
  Archetype ghost;
  ghost.flags = kEntityMoves;
  world.level.archetypes.push_back(ghost);
  const EntityId id = spawn_entity(world, static_cast<std::uint16_t>(world.level.archetypes.size() - 1),
                                   Vec3(kWorldExtent - 1.0f, 0.0f, 1.0f));
  find_entity(world, id)->velocity = Vec3(120.0f, 0.0f, 0.0f);
  test::step(world);
  CHECK(find_entity(world, id) == nullptr);
}

// ---------------------------------------------------------------- job system (stage 8)

void jobs_cover_every_index_exactly_once(test::Context& ctx) {
  JobSystem jobs(3);
  const std::size_t counts[] = {1, 2, 7, 64, 100, 1000, 4099};
  const std::size_t grains[] = {1, 3, 64, 1000, 5000};
  std::vector<int> hits(5000);
  for (std::size_t count : counts) {
    for (std::size_t grain : grains) {
      std::fill(hits.begin(), hits.end(), 0);
      // Each index belongs to exactly one chunk, so plain ints are safe here.
      auto mark = [&hits](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i) hits[i] += 1;
      };
      jobs.parallel_for(count, grain, mark);
      bool exact = true;
      for (std::size_t i = 0; i < hits.size(); ++i) exact = exact && hits[i] == (i < count ? 1 : 0);
      CHECK(exact);
    }
  }
  // Many small jobs back to back: a worker must never mix two of them up.
  std::vector<int> total(256);
  for (int round = 0; round < 2000; ++round) {
    auto add = [&total](std::size_t begin, std::size_t end) {
      for (std::size_t i = begin; i < end; ++i) total[i] += 1;
    };
    jobs.parallel_for(total.size(), 16, add);
  }
  bool all = true;
  for (int value : total) all = all && value == 2000;
  CHECK(all);

  JobSystem none(0);  // no workers: runs on the caller
  std::fill(hits.begin(), hits.end(), 0);
  auto mark = [&hits](std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) hits[i] += 1;
  };
  none.parallel_for(100, 8, mark);
  CHECK(hits[0] == 1 && hits[99] == 1 && hits[100] == 0);
}

void threaded_simulate_matches_single_threaded(test::Context& ctx) {
  Scene scene;
  std::string error;
  CHECK(load_scene(SCRAPS_SANDBOX_SCENE_DIR, "crowd", scene, error));
  World serial = make_world(scene, movement_rules_for(scene.level.perspective));
  World threaded = serial;
  JobSystem jobs(4);
  for (int t = 0; t < 180; ++t) {
    CommandList commands;
    if (t == 0) commands.push(make_command(CommandType::kPlayerJoin, 0));
    commands.push(make_command(CommandType::kMove, 0, Vec3(1.0f, 0.3f, 0.0f)));
    sandbox::simulate(serial, commands, serial.tick + 1);
    sandbox::simulate(threaded, commands, threaded.tick + 1, &jobs);
  }
  std::vector<std::uint8_t> a, b;
  ByteWriter wa(a), wb(b);
  write_world(wa, serial);
  write_world(wb, threaded);
  CHECK(a == b);  // byte for byte
}

// ---------------------------------------------------------------- images and files

void png_files_decode_and_bad_data_fails(test::Context& ctx) {
  Image image;
  std::string error;
  CHECK(load_png(std::string(SCRAPS_SANDBOX_SCENE_DIR) + "/art/hero.png", image, error));
  CHECK(image.width == 64 && image.height == 24);
  CHECK(image.rgba.size() == 64u * 24u * 4u);
  CHECK(image.rgba[3] == 0);  // the top-left corner is transparent

  const std::uint8_t garbage[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  CHECK(!decode_png(garbage, sizeof(garbage), image, error));
  CHECK(error.find("cannot decode PNG") != std::string::npos);
  CHECK(!load_png("no/such/file.png", image, error));
  CHECK(error.find("cannot open") != std::string::npos);
}

void file_watcher_notices_edits(test::Context& ctx) {
  namespace fs = std::filesystem;
  const fs::path path = fs::temp_directory_path() / "scraps_watch_test.txt";
  { std::ofstream(path) << "one"; }
  FileWatcher watcher(path.string());
  CHECK(!watcher.changed());
  std::error_code ignored;
  fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds(2), ignored);
  CHECK(watcher.changed());
  CHECK(!watcher.changed());  // once per change
  fs::remove(path, ignored);
  CHECK(watcher.changed());  // disappearing counts as a change
  CHECK(!watcher.changed());
}

// One rule for "which cell holds this point", shared by the client's hover
// outline and the sandbox's coin placing.
void cell_at_point_rounds_feet_into_the_open_cell(test::Context& ctx) {
  CellCoord cell;
  CHECK(cell_at_point(Vec3(2.5f, 3.25f, 1.5f), cell) && cell.x == 2 && cell.y == 3 && cell.z == 1);
  CHECK(cell_at_point(Vec3(-0.5f, -1.0f, -0.25f), cell) && cell.x == -1 && cell.y == -1 && cell.z == -1);
  // Feet on a block top (z = 1, maybe a hair under it after float maths)
  // are in the open cell z = 1, not in the block below.
  CHECK(cell_at_point(Vec3(0.5f, 0.5f, 1.0f), cell) && cell.z == 1);
  CHECK(cell_at_point(Vec3(0.5f, 0.5f, 1.0f - 1e-4f), cell) && cell.z == 1);
  CHECK(cell_at_point(Vec3(0.5f, 0.5f, 1.0f - 1e-2f), cell) && cell.z == 0);
  // x and y have no such nudge: plain floor(), a hair under a boundary is the lower cell.
  CHECK(cell_at_point(Vec3(1.0f - 1e-4f, 2.0f, 0.0f), cell) && cell.x == 0 && cell.y == 2);
  // No cell for points no int can index; `out` is left alone.
  cell = CellCoord{7, 8, 9};
  CHECK(!cell_at_point(Vec3(std::nanf(""), 0.0f, 0.0f), cell));
  CHECK(!cell_at_point(Vec3(0.0f, std::numeric_limits<float>::infinity(), 0.0f), cell));
  CHECK(!cell_at_point(Vec3(0.0f, 0.0f, 3.0e9f), cell));
  CHECK(!cell_at_point(Vec3(-2.0e6f, 0.0f, 0.0f), cell));
  CHECK(cell.x == 7 && cell.y == 8 && cell.z == 9);
}

void worlds_with_images_round_trip(test::Context& ctx) {
  Scene scene;
  std::string error;
  CHECK(load_scene(SCRAPS_SANDBOX_SCENE_DIR, "isometric", scene, error));
  const World world = make_world(scene, movement_rules_for(scene.level.perspective));
  CHECK(!world.level.images.empty());
  std::vector<std::uint8_t> bytes;
  ByteWriter writer(bytes);
  write_world(writer, world);
  World loaded;
  ByteReader reader(bytes.data(), bytes.size());
  CHECK(read_world(reader, loaded));
  CHECK(loaded.level.images.size() == world.level.images.size());
  CHECK(path_view(loaded.level.images[0].path) == path_view(world.level.images[0].path));
  CHECK(loaded.level.tiles[1].image == world.level.tiles[1].image);
  std::vector<std::uint8_t> again;
  ByteWriter writer2(again);
  write_world(writer2, loaded);
  CHECK(again == bytes);
}

// ---------------------------------------------------------------- sound

std::vector<std::uint8_t> read_test_file(const std::string& name) {
  std::ifstream in(std::string(SCRAPS_TEST_AUDIO_DIR) + "/" + name, std::ios::binary);
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

float rms(const SoundData& sound, int channel) {
  double sum = 0.0;
  const std::size_t frames = sound.frames();
  for (std::size_t i = 0; i < frames; ++i) {
    const double v = sound.samples[i * kSoundChannels + static_cast<std::size_t>(channel)];
    sum += v * v;
  }
  return frames > 0 ? static_cast<float>(std::sqrt(sum / static_cast<double>(frames))) : 0.0f;
}

void sounds_decode_from_every_format(test::Context& ctx) {
  struct Case {
    const char* file;
    AudioFormat format;
  };
  const Case cases[] = {
      {"tone_u8.wav", AudioFormat::kWav},     {"tone_s16.wav", AudioFormat::kWav},
      {"tone_s24.wav", AudioFormat::kWav},    {"tone_f32.wav", AudioFormat::kWav},
      {"tone.aiff", AudioFormat::kAiff},      {"tone.flac", AudioFormat::kFlac},
      {"tone_6ch.flac", AudioFormat::kFlac},  {"tone.mp3", AudioFormat::kMp3},
      {"tone.ogg", AudioFormat::kOggVorbis},  {"flac_named_as.wav", AudioFormat::kFlac},
  };
  // Every file holds the same tone: 0.2 s of a sine at amplitude 1/8, so
  // 9,600 frames at 48 kHz and an RMS of 0.125 / sqrt(2) on both sides.
  const float expected_rms = 0.125f / std::sqrt(2.0f);
  for (const Case& c : cases) {
    const std::vector<std::uint8_t> bytes = read_test_file(c.file);
    CHECK(!bytes.empty());
    CHECK(detect_audio_format(bytes.data(), bytes.size()) == c.format);
    SoundData sound;
    std::string error;
    const bool ok = decode_sound(bytes.data(), bytes.size(), sound, error);
    if (!ok) std::printf("    %s: %s\n", c.file, error.c_str());
    CHECK(ok);
    // Lossy formats add a little encoder padding at the ends.
    CHECK(sound.frames() >= 9500 && sound.frames() <= 12000);
    CHECK_NEAR(rms(sound, 0), expected_rms, expected_rms * 0.2);
    CHECK_NEAR(rms(sound, 1), expected_rms, expected_rms * 0.2);
  }

  // Recognised but not decodable: a clear error that names the format.
  for (const char* file : {"tone.opus", "tone.m4a"}) {
    const std::vector<std::uint8_t> bytes = read_test_file(file);
    SoundData sound;
    std::string error;
    CHECK(!decode_sound(bytes.data(), bytes.size(), sound, error));
    CHECK(error.find(std::string(file) == "tone.opus" ? "Opus" : "AAC") != std::string::npos);
  }
  // 35 KB that decodes to 60 s of 8-channel audio: refused as too long, and
  // decoding stops at 30 s instead of first holding everything.
  {
    const std::vector<std::uint8_t> bytes = read_test_file("long_silence_8ch.flac");
    SoundData sound;
    std::string error;
    const auto start = std::chrono::steady_clock::now();
    CHECK(!decode_sound(bytes.data(), bytes.size(), sound, error));
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(error.find("30 seconds") != std::string::npos);
    CHECK(sound.samples.empty() && sound.samples.capacity() <= static_cast<std::size_t>(kMaxSoundSeconds * kSoundSampleRate * kSoundChannels));
    CHECK(seconds < 5.0);
  }
  // The same for a format whose header does not say how long it is: the
  // decoder stops on its own at 30 s.
  {
    const std::vector<std::uint8_t> bytes = read_test_file("long_35s.mp3");
    CHECK(!bytes.empty());
    SoundData sound;
    std::string error;
    CHECK(!decode_sound(bytes.data(), bytes.size(), sound, error));
    CHECK(error.find("30 seconds") != std::string::npos);
  }

  const std::uint8_t text[] = "this is not a sound";
  SoundData sound;
  std::string error;
  CHECK(!decode_sound(text, sizeof(text), sound, error));
  CHECK(error.find("recognise") != std::string::npos);
  CHECK(!load_sound(std::string(SCRAPS_TEST_AUDIO_DIR) + "/missing.wav", sound, error));
  CHECK(load_sound(std::string(SCRAPS_TEST_AUDIO_DIR) + "/tone.ogg", sound, error));
}

void sound_rejects_hostile_input(test::Context& ctx) {
  std::uint32_t seed = 777;
  auto next = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
  };
  int decoded = 0;
  bool all_bounded = true;
  for (const char* file : {"tone_s16.wav", "tone.aiff", "tone.flac", "tone.mp3", "tone.ogg"}) {
    const std::vector<std::uint8_t> bytes = read_test_file(file);
    SoundData sound;
    std::string error;
    // Cut short at many lengths, and with bytes changed at random: never a crash.
    for (std::size_t length = 0; length < bytes.size(); length += 1 + bytes.size() / 97) {
      if (decode_sound(bytes.data(), length, sound, error)) ++decoded;
    }
    for (int round = 0; round < 300; ++round) {
      std::vector<std::uint8_t> corrupt = bytes;
      for (int k = 0; k < 4; ++k) corrupt[next() % corrupt.size()] = static_cast<std::uint8_t>(next());
      if (decode_sound(corrupt.data(), corrupt.size(), sound, error)) ++decoded;
      // A valid header followed by noise.
      std::vector<std::uint8_t> noise(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(64, bytes.size())));
      noise.resize(64 + next() % 2048);
      for (std::size_t i = 64; i < noise.size(); ++i) noise[i] = static_cast<std::uint8_t>(next());
      if (decode_sound(noise.data(), noise.size(), sound, error)) ++decoded;
      CHECK(sound.samples.size() % kSoundChannels == 0);
      if (sound.frames() > static_cast<std::size_t>(kMaxSoundSeconds * kSoundSampleRate)) all_bounded = false;
    }
  }
  // Reaching here means no crash. Some damaged files still decode (which is
  // fine), but never into more than the 30-second limit.
  CHECK(all_bounded);
  std::printf("    %d damaged files still decoded\n", decoded);
}

void mixer_mixes_and_replaces_voices(test::Context& ctx) {
  Mixer mixer;
  SoundData long_sound;
  long_sound.samples.assign(100 * kSoundChannels, 0.25f);
  SoundData short_sound;
  short_sound.samples.assign(50 * kSoundChannels, 0.25f);
  const SoundId a = mixer.add(long_sound);
  const SoundId b = mixer.add(short_sound);

  std::vector<float> out(200 * kSoundChannels, 9.0f);
  mixer.play(a);
  mixer.play(b, 2.0f);
  CHECK(mixer.playing() == 2);
  mixer.mix(out.data(), 200);
  CHECK_NEAR(out[0], 0.75f, 1e-6);                     // 0.25 + 0.25 * 2
  CHECK_NEAR(out[60 * kSoundChannels + 1], 0.25f, 1e-6);  // the short one ended
  CHECK_NEAR(out[150 * kSoundChannels], 0.0f, 1e-6);      // both ended: silence, old contents overwritten
  CHECK(mixer.playing() == 0);

  mixer.play(a, 4.0f);
  mixer.play(a, 4.0f);
  mixer.mix(out.data(), 1);
  CHECK(out[0] == 1.0f);  // 2.0 clamped to full scale

  mixer.stop_all();
  for (int i = 0; i < 33; ++i) mixer.play(a);
  CHECK(mixer.playing() == Mixer::kVoices);  // the 33rd replaced the oldest
  mixer.play(kNoSound);
  mixer.play(a, -1.0f);
  CHECK(mixer.playing() == Mixer::kVoices);
  mixer.clear();
  CHECK(mixer.playing() == 0 && mixer.sound_count() == 0);
}

// ---------------------------------------------------------------- facing

void facing_follows_movement_and_stays(test::Context& ctx) {
  World world = test::make_box_world(test::flat_rules());
  test::step(world, test::one(make_command(CommandType::kPlayerJoin, 0)));
  const EntityId id = player_entity(world, 0);
  CHECK(find_entity(world, id)->facing.x == 1.0f);  // starts facing +x
  for (int i = 0; i < 5; ++i) test::step(world, test::one(make_command(CommandType::kMove, 0, Vec3(-1.0f, 0.0f, 0.0f))));
  CHECK_NEAR(find_entity(world, id)->facing.x, -1.0f, 1e-6);
  for (int i = 0; i < 5; ++i) test::step(world, test::one(make_command(CommandType::kMove, 0, Vec3())));
  CHECK_NEAR(find_entity(world, id)->facing.x, -1.0f, 1e-6);  // stopped: still facing left
  CHECK(find_entity(world, id)->velocity.x == 0.0f);

  // Something that is not a player faces where it goes.
  Archetype ghost;
  ghost.flags = kEntityMoves;
  world.level.archetypes.push_back(ghost);
  const EntityId g = spawn_entity(world, static_cast<std::uint16_t>(world.level.archetypes.size() - 1),
                                  Vec3(2.5f, 2.5f, 1.0f));
  find_entity(world, g)->velocity = Vec3(0.0f, 3.0f, 0.0f);
  test::step(world);
  CHECK_NEAR(find_entity(world, g)->facing.y, 1.0f, 1e-6);
}

// ---------------------------------------------------------------- delta snapshots

std::vector<std::uint8_t> state_bytes(const World& world) {
  std::vector<std::uint8_t> bytes;
  ByteWriter writer(bytes);
  write_world_state(writer, world);
  return bytes;
}

WorldState state_of(const World& world) {
  WorldState state;
  copy_state(world, state);
  return state;
}

std::vector<std::uint8_t> delta_bytes(const WorldState& baseline, const World& current) {
  std::vector<std::uint8_t> bytes;
  ByteWriter writer(bytes);
  write_world_delta(writer, baseline, current);
  return bytes;
}

bool apply_delta(const std::vector<std::uint8_t>& bytes, const WorldState& baseline, World& out) {
  ByteReader reader(bytes.data(), bytes.size());
  return read_world_delta(reader, baseline, out) && reader.remaining() == 0;
}

// The server encodes against its own copy of a past state; the client decodes
// against the copy it rebuilt itself. Both must end at the server's bytes.
void delta_round_trips(test::Context& ctx) {
  World server = test::make_box_world(test::gravity_rules());
  test::step(server, test::one(make_command(CommandType::kPlayerJoin, 0)));
  World client = server;  // same level; its state is overwritten by every delta

  std::uint32_t seed = 2026;
  auto next = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
  };
  auto random_live = [&]() -> Entity* {
    if (count_alive(server) == 0) return nullptr;
    for (;;) {
      Entity& entity = server.entities[next() % server.entities.size()];
      if (entity.alive) return &entity;
    }
  };

  constexpr std::size_t kKept = 8;
  std::vector<WorldState> sent;     // the server's copies
  std::vector<WorldState> rebuilt;  // the client's copies of the same snapshots
  sent.push_back(state_of(server));
  rebuilt.push_back(state_of(server));

  int mismatches = 0;
  int failures = 0;
  std::size_t largest_change = 0;
  for (int round = 0; round < 3000; ++round) {
    const std::uint32_t changes = next() % 5;
    bool stepped = false;
    for (std::uint32_t c = 0; c < changes; ++c) {
      switch (next() % 9) {
        case 0:
          if (count_alive(server) < 300) {
            spawn_entity(server, 1, Vec3(static_cast<float>(next() % 600) / 100.0f,
                                         static_cast<float>(next() % 600) / 100.0f, 1.0f));
          }
          break;
        case 1:
          if (Entity* e = random_live()) {
            destroy_entity(server, entity_id_at(server, static_cast<std::uint32_t>(e - server.entities.data())));
          }
          break;
        case 2:
          if (Entity* e = random_live()) e->position.x += static_cast<float>(next() % 100) / 1000.0f;
          break;
        case 3:
          if (Entity* e = random_live()) {
            e->velocity.y = static_cast<float>(next() % 50) / 10.0f;
            e->on_ground = !e->on_ground;
          }
          break;
        case 4:
          if (Entity* e = random_live()) e->user[next() % 4] = static_cast<std::int32_t>(next());
          break;
        case 5:
          if (Entity* e = random_live()) e->position.z = (next() % 2) != 0 ? -0.0f : 0.0f;  // sign of zero matters
          break;
        case 6:
          if (Entity* e = random_live()) e->color.r = static_cast<std::uint8_t>(next());
          break;
        case 7: {
          const PlayerId player = static_cast<PlayerId>(next() % 3);
          const CommandType type = (next() % 2) != 0 ? CommandType::kPlayerJoin : CommandType::kPlayerLeave;
          test::step(server, test::one(make_command(type, player)));
          stepped = true;
          break;
        }
        default:
          test::step(server, test::one(make_command(CommandType::kMove, 0, Vec3(1.0f, 0.0f, 0.0f))));
          stepped = true;
          break;
      }
    }
    if (!stepped) ++server.tick;
    next_random(server);

    // Any kept snapshot may be the client's acked one (losses, reordering).
    const std::size_t k = next() % sent.size();
    const std::vector<std::uint8_t> bytes = delta_bytes(sent[k], server);
    largest_change = std::max(largest_change, bytes.size());
    if (!apply_delta(bytes, rebuilt[k], client)) {
      ++failures;
      continue;
    }
    if (state_bytes(client) != state_bytes(server)) ++mismatches;

    // Decoding over the server's own copy (stale data in dead slots) gives
    // the same state too.
    World again = server;
    if (!apply_delta(bytes, sent[k], again) || state_bytes(again) != state_bytes(server)) ++mismatches;

    sent.push_back(state_of(server));
    rebuilt.push_back(state_of(client));
    if (sent.size() > kKept) {
      sent.erase(sent.begin());
      rebuilt.erase(rebuilt.begin());
    }
  }
  CHECK(failures == 0);
  CHECK(mismatches == 0);
  CHECK(largest_change > 0);

  // Nothing changed: header only.
  const WorldState now = state_of(server);
  CHECK(delta_bytes(now, server).size() < 32);
}

void delta_rejects_hostile_input(test::Context& ctx) {
  World server = test::make_box_world(test::gravity_rules());
  test::step(server, test::one(make_command(CommandType::kPlayerJoin, 0)));
  test::step(server, test::one(make_command(CommandType::kPlayerJoin, 1)));
  test::step(server, test::one(make_command(CommandType::kPlayerJoin, 2)));
  destroy_entity(server, player_entity(server, 2));  // the baseline has a dead slot
  const WorldState baseline = state_of(server);
  // Changes of every kind: a death, a birth, movement.
  destroy_entity(server, player_entity(server, 1));
  test::step(server);
  spawn_entity(server, 1, Vec3(2.5f, 2.5f, 1.0f));
  spawn_entity(server, 1, Vec3(3.5f, 4.5f, 1.0f));
  for (int i = 0; i < 5; ++i) test::step(server, test::one(make_command(CommandType::kMove, 0, Vec3(1.0f, 0.0f, 0.0f))));
  const std::vector<std::uint8_t> bytes = delta_bytes(baseline, server);

  World target = server;
  CHECK(apply_delta(bytes, baseline, target));
  CHECK(state_bytes(target) == state_bytes(server));

  int accepted = 0;
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    World t = server;
    ByteReader reader(bytes.data(), length);
    if (read_world_delta(reader, baseline, t)) ++accepted;
  }
  CHECK(accepted == 0);  // every truncation is refused

  // Over the wrong baseline (another tick): refused, not rebuilt into nonsense.
  World later = server;
  test::step(later);
  CHECK(!apply_delta(bytes, state_of(later), target));

  // Corrupt bytes and noise: must never crash.
  std::uint32_t seed = 4242;
  auto next = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
  };
  for (int round = 0; round < 3000; ++round) {
    std::vector<std::uint8_t> corrupt = bytes;
    corrupt[next() % corrupt.size()] = static_cast<std::uint8_t>(next());
    World t = server;
    apply_delta(corrupt, baseline, t);
    std::vector<std::uint8_t> noise(next() % 256);
    for (std::uint8_t& b : noise) b = static_cast<std::uint8_t>(next());
    if (noise.size() >= 4 && (round % 2) == 0) {  // half of them pass the tick check
      for (int i = 0; i < 4; ++i) noise[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(baseline.tick >> (8 * i));
    }
    apply_delta(noise, baseline, t);
  }

  // Hand-made deltas: header, then (skip, mask, groups...) entries, then the
  // free-list and player-table flags. Group bits: 1 slot, 2 pose, 16 look, 64 invalid.
  const std::uint32_t slots = static_cast<std::uint32_t>(baseline.entities.size());
  std::uint32_t dead = 0;
  while (dead < slots && baseline.entities[dead].alive) ++dead;
  std::uint32_t live = 0;
  while (live < slots && !baseline.entities[live].alive) ++live;
  CHECK(dead < slots && live < slots);
  auto hand_made = [&](std::uint32_t slot_count, std::uint32_t skip, std::uint8_t mask,
                       void (*groups)(ByteWriter&)) {
    std::vector<std::uint8_t> out;
    ByteWriter w(out);
    w.write_u32(baseline.tick);
    w.write_varint(1);
    w.write_u64(baseline.rng_state);
    w.write_varint(slot_count);
    w.write_varint(1);
    w.write_varint(skip);
    w.write_u8(mask);
    groups(w);
    w.write_u8(0);
    w.write_u8(0);
    World t = server;
    return apply_delta(out, baseline, t);
  };
  auto pose = [](ByteWriter& w) { w.write_vec3(Vec3(1.0f, 1.0f, 1.0f)); };
  CHECK(hand_made(slots, live, 2, pose));        // control: a plain move is fine
  CHECK(!hand_made(slots, slots, 2, pose));      // slot index out of range
  CHECK(!hand_made(slots, live, 0, pose));       // empty mask
  CHECK(!hand_made(slots, live, 2 | 64, pose));  // unknown group
  CHECK(!hand_made(kMaxEntities + 1, live, 2, pose));
  CHECK(!hand_made(slots + 60000, live, 2, pose));   // huge growth from a few bytes
  CHECK(!hand_made(slots, dead, 2, pose));           // moving a dead slot
  CHECK(!hand_made(slots, dead, 1 | 2, [](ByteWriter& w) {  // a birth without every field
    w.write_u32(7);
    w.write_bool(true);
    w.write_vec3(Vec3(1.0f, 1.0f, 1.0f));
  }));
  CHECK(!hand_made(slots, live, 16, [](ByteWriter& w) {  // archetype that does not exist
    w.write_u16(999);
    w.write_u16(0);
    w.write_u32(0);
    w.write_vec3(Vec3(0.5f, 0.5f, 0.5f));
    w.write_color(Color());
    w.write_f32(1.0f);
  }));

  // Legal bytes, dangerous values.
  World bad = server;
  bad.entities[live].size = Vec3(1.0e7f, 1.0f, 1.0f);
  CHECK(!apply_delta(delta_bytes(baseline, bad), baseline, target));
  bad = server;
  bad.free_slots.push_back(bad.free_slots.empty() ? 0 : bad.free_slots.back());  // a duplicate free slot
  CHECK(!apply_delta(delta_bytes(baseline, bad), baseline, target));
}

}  // namespace

void register_core_tests(test::Registry& registry) {
  registry.add("core.jobs_cover_every_index_exactly_once", jobs_cover_every_index_exactly_once);
  registry.add("core.threaded_simulate_matches_single_threaded", threaded_simulate_matches_single_threaded);
  registry.add("core.png_files_decode_and_bad_data_fails", png_files_decode_and_bad_data_fails);
  registry.add("core.file_watcher_notices_edits", file_watcher_notices_edits);
  registry.add("core.cell_at_point_rounds_feet_into_the_open_cell", cell_at_point_rounds_feet_into_the_open_cell);
  registry.add("core.worlds_with_images_round_trip", worlds_with_images_round_trip);
  registry.add("core.hostile_snapshot_values_are_rejected", hostile_snapshot_values_are_rejected);
  registry.add("core.entities_that_leave_the_world_are_removed", entities_that_leave_the_world_are_removed);
  registry.add("core.fixed_step_keeps_60_ticks_per_second", fixed_step_keeps_60_ticks_per_second);
  registry.add("core.fixed_step_drops_backlog_after_a_stall", fixed_step_drops_backlog_after_a_stall);
  registry.add("core.byte_reader_never_reads_past_the_end", byte_reader_never_reads_past_the_end);
  registry.add("core.bytes_are_little_endian_and_round_trip", bytes_are_little_endian_and_round_trip);
  registry.add("core.stale_handles_stop_resolving", stale_handles_stop_resolving);
  registry.add("core.world_copies_are_independent", world_copies_are_independent);
  registry.add("core.join_move_and_leave", join_move_and_leave);
  registry.add("core.walls_stop_movement_and_allow_sliding", walls_stop_movement_and_allow_sliding);
  registry.add("core.gravity_lands_and_jumps_only_from_the_ground", gravity_lands_and_jumps_only_from_the_ground);
  registry.add("core.the_pit_removes_things_and_respawns_players", the_pit_removes_things_and_respawns_players);
  registry.add("core.side_rules_lock_the_y_axis", side_rules_lock_the_y_axis);
  registry.add("core.random_numbers_live_in_the_world", random_numbers_live_in_the_world);
  registry.add("core.world_round_trips_through_bytes", world_round_trips_through_bytes);
  registry.add("core.truncated_or_random_bytes_are_rejected_safely", truncated_or_random_bytes_are_rejected_safely);
  registry.add("core.delta_round_trips", delta_round_trips);
  registry.add("core.facing_follows_movement_and_stays", facing_follows_movement_and_stays);
  registry.add("core.sounds_decode_from_every_format", sounds_decode_from_every_format);
  registry.add("core.sound_rejects_hostile_input", sound_rejects_hostile_input);
  registry.add("core.mixer_mixes_and_replaces_voices", mixer_mixes_and_replaces_voices);
  registry.add("core.delta_rejects_hostile_input", delta_rejects_hostile_input);
}
