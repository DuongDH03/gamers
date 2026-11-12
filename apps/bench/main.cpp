// scraps_bench: measures where frame time goes, headless, so the numbers do
// not depend on a GPU or a window. Used by the `bench` agent.
//
//   simulate      one tick of the game's simulate
//   copy world    previous = current (done every tick for interpolation)
//   serialize     write_world_state (what a full snapshot costs to build)
//   delta         write_world_delta against the state 6 ticks earlier (a
//                 client ~100 ms behind), and read_world_delta on the client side
//   draw <view>   View::draw into the headless renderer (CPU side only)

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "backends/headless/headless_backend.h"
#include "core/jobs.h"
#include "core/scene.h"
#include "core/serialize.h"
#include "core/world.h"
#include "sandbox/sandbox.h"
#include "views/sprite_bank.h"
#include "views/view_factory.h"

namespace {

using Clock = std::chrono::steady_clock;

double milliseconds_since(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Timing {
  double total = 0.0;
  double worst = 0.0;
  long samples = 0;

  void add(double ms) {
    total += ms;
    worst = std::max(worst, ms);
    ++samples;
  }
  double average() const { return samples > 0 ? total / static_cast<double>(samples) : 0.0; }
};

void report(const char* name, const Timing& timing) {
  std::printf("  %-22s avg %8.4f ms   worst %8.4f ms\n", name, timing.average(), timing.worst);
}

}  // namespace

int main(int argc, char** argv) {
  using namespace scraps;

  std::string scene_name = "crowd";
  long ticks = 600;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string arg = argv[i];
    if (arg == "--scene") {
      scene_name = argv[i + 1];
    } else if (arg == "--ticks") {
      ticks = std::max(1L, std::strtol(argv[i + 1], nullptr, 10));
    }
  }

  Scene scene;
  std::string error;
  if (!load_scene(SCRAPS_SANDBOX_SCENE_DIR, scene_name, scene, error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }

  World world = make_world(scene, movement_rules_for(scene.level.perspective));
  CommandList join;
  join.push(make_command(CommandType::kPlayerJoin, 0));
  sandbox::simulate(world, join, world.tick + 1);

  std::printf("scene '%s' (%s), %lu entities, %ld ticks; budget per frame at 60 Hz: 16.667 ms\n",
              scene.name.c_str(), perspective_name(scene.level.perspective),
              static_cast<unsigned long>(count_alive(world)), ticks);

  Timing simulate_time, copy_time, serialize_time, delta_write_time, delta_read_time;
  World previous = world;
  std::vector<std::uint8_t> bytes;
  bytes.reserve(1 << 20);
  constexpr long kDeltaAge = 6;  // ticks between the baseline and now: ~100 ms round trip
  std::vector<WorldState> kept(kDeltaAge);
  for (WorldState& state : kept) {
    reserve_state(state, world.entities.capacity());
    copy_state(world, state);
  }
  World client = world;
  client.entities.reserve(world.entities.capacity());
  client.free_slots.reserve(world.free_slots.capacity());
  std::vector<std::uint8_t> delta;
  delta.reserve(1 << 20);
  std::size_t delta_total = 0;
  std::size_t delta_largest = 0;
  long bad_deltas = 0;
  CommandList commands;
  for (long t = 0; t < ticks; ++t) {
    commands.clear();
    commands.push(make_command(CommandType::kMove, 0, Vec3(1.0f, 0.5f, 0.0f)));

    auto start = Clock::now();
    previous = world;
    copy_time.add(milliseconds_since(start));

    start = Clock::now();
    sandbox::simulate(world, commands, world.tick + 1);
    simulate_time.add(milliseconds_since(start));

    start = Clock::now();
    bytes.clear();
    ByteWriter writer(bytes);
    write_world_state(writer, world);
    serialize_time.add(milliseconds_since(start));

    WorldState& baseline = kept[static_cast<std::size_t>(t % kDeltaAge)];
    start = Clock::now();
    delta.clear();
    ByteWriter delta_writer(delta);
    write_world_delta(delta_writer, baseline, world);
    delta_write_time.add(milliseconds_since(start));
    start = Clock::now();
    ByteReader delta_reader(delta.data(), delta.size());
    if (!read_world_delta(delta_reader, baseline, client)) ++bad_deltas;
    delta_read_time.add(milliseconds_since(start));
    delta_total += delta.size();
    delta_largest = std::max(delta_largest, delta.size());
    copy_state(world, baseline);  // becomes the baseline kDeltaAge ticks from now
  }
  report("simulate (1 thread)", simulate_time);
  report("copy world", copy_time);
  report("serialize state", serialize_time);
  report("delta encode", delta_write_time);
  report("delta decode", delta_read_time);
  std::printf("  snapshot size          %lu bytes (%lu datagrams)\n", static_cast<unsigned long>(bytes.size()),
              static_cast<unsigned long>((bytes.size() + 1023) / 1024));
  const std::size_t delta_average = delta_total / static_cast<std::size_t>(ticks);
  std::printf("  delta size             avg %lu bytes, largest %lu (%lu datagrams), %.1f%% of full%s\n",
              static_cast<unsigned long>(delta_average), static_cast<unsigned long>(delta_largest),
              static_cast<unsigned long>((delta_largest + 1023) / 1024),
              bytes.empty() ? 0.0 : 100.0 * static_cast<double>(delta_average) / static_cast<double>(bytes.size()),
              bad_deltas != 0 ? "  (SOME DELTAS DID NOT DECODE)" : "");

  // The same ticks with movement spread over worker threads (stage 8).
  {
    JobSystem jobs(JobSystem::default_worker_count());
    World threaded = previous;
    Timing threaded_time;
    for (long t = 0; t < ticks; ++t) {
      commands.clear();
      commands.push(make_command(CommandType::kMove, 0, Vec3(1.0f, 0.5f, 0.0f)));
      const auto start = Clock::now();
      sandbox::simulate(threaded, commands, threaded.tick + 1, &jobs);
      threaded_time.add(milliseconds_since(start));
    }
    const std::string label = "simulate (" + std::to_string(jobs.worker_count() + 1) + " threads)";
    report(label.c_str(), threaded_time);
  }

  headless::HeadlessRenderer renderer(1280, 720);
  SpriteBank sprites;
  std::string sprite_errors;
  sprites.load(world.level, SCRAPS_SANDBOX_SCENE_DIR, renderer, sprite_errors);
  const Perspective perspectives[3] = {Perspective::kTopDown, Perspective::kSide, Perspective::kIsometric};
  for (Perspective perspective : perspectives) {
    std::unique_ptr<View> view = make_view(perspective);
    Camera camera;
    camera.zoom = view->default_zoom();
    camera.target = Vec3(static_cast<float>(world.level.grid.width) * 0.5f,
                         static_cast<float>(world.level.grid.depth) * 0.5f, 1.0f);
    Timing draw_time;
    for (int frame = 0; frame < 120; ++frame) {
      renderer.begin_frame(Color());
      const auto start = Clock::now();
      view->draw(previous, world, 0.5f, camera, renderer, &sprites);
      draw_time.add(milliseconds_since(start));
      renderer.end_frame();
    }
    const std::string label = std::string("draw ") + view->name();
    report(label.c_str(), draw_time);
    std::printf("  %-22s %lu triangles in %lu batch(es)\n", "", static_cast<unsigned long>(renderer.stats().triangles),
                static_cast<unsigned long>(renderer.stats().batches));
  }
  return 0;
}
