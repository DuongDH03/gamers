#include <algorithm>
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "backends/headless/headless_backend.h"
#include "core/scene.h"
#include "harness.h"
#include "test_world.h"
#include "views/controls.h"
#include "views/sound_bank.h"
#include "views/sprite_bank.h"
#include "views/view_factory.h"

using namespace scraps;

namespace {

Camera test_camera(const View& view) {
  Camera camera;
  camera.zoom = view.default_zoom();
  camera.target = Vec3(1.0f, 1.0f, 1.0f);
  return camera;
}

void isometric_looks_along_the_diagonal(test::Context& ctx) {
  const std::unique_ptr<View> view = make_view(Perspective::kIsometric);
  const Camera camera = test_camera(*view);
  const Vec3 p(2.25f, -1.5f, 0.75f);
  const Vec2 a = view->world_to_screen(p, camera);
  const Vec2 b = view->world_to_screen(p + Vec3(1.0f, 1.0f, 1.0f), camera);
  CHECK_NEAR(a.x, b.x, 1e-3);
  CHECK_NEAR(a.y, b.y, 1e-3);
  // Standard 2:1 diamond: one step in x moves half a tile right and a quarter down.
  const Vec2 c = view->world_to_screen(p + Vec3(1.0f, 0.0f, 0.0f), camera);
  CHECK_NEAR(c.x - a.x, camera.zoom * 0.5f, 1e-3);
  CHECK_NEAR(c.y - a.y, camera.zoom * 0.25f, 1e-3);
  // Up in the world is up on screen.
  const Vec2 d = view->world_to_screen(p + Vec3(0.0f, 0.0f, 1.0f), camera);
  CHECK(d.y < a.y);
}

// For every view, the ground direction for a screen direction must look like
// that screen direction when projected back.
void input_directions_match_the_screen(test::Context& ctx) {
  const Perspective perspectives[] = {Perspective::kTopDown, Perspective::kSide, Perspective::kIsometric};
  const Vec2 screen_directions[] = {Vec2(1, 0), Vec2(-1, 0), Vec2(0, 1), Vec2(0, -1), Vec2(1, 1), Vec2(-1, 1)};
  for (Perspective perspective : perspectives) {
    const std::unique_ptr<View> view = make_view(perspective);
    const Camera camera = test_camera(*view);
    for (Vec2 screen : screen_directions) {
      const Vec2 ground = view->screen_to_ground_direction(screen);
      if (perspective == Perspective::kSide) {
        // Only left/right exist on the side view's ground.
        CHECK(ground.y == 0.0f);
        if (screen.x == 0.0f) {
          CHECK(ground == Vec2());
          continue;
        }
        screen = Vec2(screen.x, 0.0f);
      }
      CHECK_NEAR(length(ground), 1.0f, 1e-4);
      const Vec3 origin(3.0f, 3.0f, 1.0f);
      const Vec2 s0 = view->world_to_screen(origin, camera);
      const Vec2 s1 = view->world_to_screen(origin + Vec3(ground.x, ground.y, 0.0f), camera);
      const Vec2 moved = s1 - s0;
      const float cross = moved.x * screen.y - moved.y * screen.x;
      const float dot = moved.x * screen.x + moved.y * screen.y;
      CHECK_NEAR(cross, 0.0f, 1e-2);
      CHECK(dot > 0.0f);
    }
  }
}

World two_block_world(const char* rows_layer0) {
  std::string text =
      "scraps-scene 1\nview isometric\nsize 2 2 1\n"
      "tile b back solid 10 10 10\n"
      "tile f front solid 200 200 200\n"
      "archetype hero size 0.5 0.5 0.5 color 50 100 150\n"
      "player hero\nspawn 0.5 0.5 1\nlayer 0\n";
  text += rows_layer0;
  Scene scene;
  std::string error;
  parse_scene(text, scene, error);
  return make_world(scene, MovementRules());
}

// Index of the first and last vertex with exactly this color, or -1.
void find_color(const std::vector<Vertex2D>& vertices, Color color, long& first, long& last) {
  first = -1;
  last = -1;
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    if (vertices[i].color != color) continue;
    if (first < 0) first = static_cast<long>(i);
    last = static_cast<long>(i);
  }
}

void isometric_paints_back_to_front(test::Context& ctx) {
  // Block "b" at (0, 0) is behind block "f" at (1, 1).
  const World world = two_block_world("b.\n.f\n");
  const std::unique_ptr<View> view = make_view(Perspective::kIsometric);
  headless::HeadlessRenderer renderer(800, 600);
  renderer.set_capture(true);
  Camera camera = test_camera(*view);
  camera.viewport_width = 800.0f;
  camera.viewport_height = 600.0f;
  renderer.begin_frame(Color());
  view->draw(world, world, 0.0f, camera, renderer);

  long back_first = 0, back_last = 0, front_first = 0, front_last = 0;
  find_color(renderer.captured_vertices(), Color(10, 10, 10), back_first, back_last);       // top faces
  find_color(renderer.captured_vertices(), Color(200, 200, 200), front_first, front_last);
  CHECK(back_first >= 0 && front_first >= 0);
  CHECK(back_last < front_first);
  CHECK(renderer.stats().batches == 1);  // the whole world in one batch
}

void isometric_entities_stand_in_front_of_their_floor(test::Context& ctx) {
  World world = two_block_world("ff\nff\n");
  CommandList join;
  join.push(make_command(CommandType::kPlayerJoin, 0));
  test::step(world, join);  // the hero appears at (0.5, 0.5, 1), on block (0, 0, 0)
  const std::unique_ptr<View> view = make_view(Perspective::kIsometric);
  headless::HeadlessRenderer renderer(800, 600);
  renderer.set_capture(true);
  Camera camera = test_camera(*view);
  camera.viewport_width = 800.0f;
  camera.viewport_height = 600.0f;
  renderer.begin_frame(Color());
  view->draw(world, world, 0.0f, camera, renderer);

  long floor_first = 0, floor_last = 0, hero_first = 0, hero_last = 0;
  // All four blocks share one top color. The first such vertex belongs to
  // block (0, 0, 0), under the hero; the last to block (1, 1, 0), in front.
  find_color(renderer.captured_vertices(), Color(200, 200, 200), floor_first, floor_last);
  find_color(renderer.captured_vertices(), Color(50, 100, 150), hero_first, hero_last);
  CHECK(hero_first > floor_first);  // after the block it stands on
  CHECK(hero_last < floor_last);    // before the blocks in front of it
}

void side_view_cuts_away_lanes_in_front(test::Context& ctx) {
  Scene scene;
  std::string error;
  const char* text =
      "scraps-scene 1\nview side\nsize 1 2 1\n"
      "tile s stone solid 1 2 3\n"
      "archetype hero\nplayer hero\nspawn 0.5 0.5 1\n"
      "layer 0\ns\n.\n";
  CHECK(parse_scene(text, scene, error));
  const World world = make_world(scene, MovementRules());
  const std::unique_ptr<View> view = make_view(Perspective::kSide);
  headless::HeadlessRenderer renderer(800, 600);
  Camera camera = test_camera(*view);
  camera.viewport_width = 800.0f;
  camera.viewport_height = 600.0f;

  camera.target = Vec3(0.5f, 0.5f, 0.5f);  // lane 0: the stone is visible
  renderer.begin_frame(Color());
  view->draw(world, world, 0.0f, camera, renderer);
  CHECK(renderer.stats().triangles == 2);

  camera.target = Vec3(0.5f, 1.5f, 0.5f);  // lane 1: the stone is in front, cut away
  renderer.begin_frame(Color());
  view->draw(world, world, 0.0f, camera, renderer);
  CHECK(renderer.stats().triangles == 0);
}

void every_view_draws_every_bundled_scene(test::Context& ctx) {
  const char* names[] = {"top-down", "side", "isometric", "crowd"};
  headless::HeadlessRenderer renderer(1280, 720);
  for (const char* name : names) {
    Scene scene;
    std::string error;
    CHECK(load_scene(SCRAPS_SANDBOX_SCENE_DIR, name, scene, error));
    const World world = make_world(scene, movement_rules_for(scene.level.perspective));
    for (int p = 0; p < 3; ++p) {
      const std::unique_ptr<View> view = make_view(static_cast<Perspective>(p));
      Camera camera;
      camera.zoom = view->default_zoom();
      camera.target = scene.level.spawn_points[0];
      renderer.begin_frame(Color());
      view->draw(world, world, 0.5f, camera, renderer);
      CHECK(renderer.stats().triangles > 0);
      CHECK(renderer.stats().batches == 1);
    }
  }
}

void bundled_art_loads_and_batches_split_only_on_texture_change(test::Context& ctx) {
  const char* names[] = {"top-down", "side", "isometric", "crowd"};
  for (const char* name : names) {
    Scene scene;
    std::string error;
    CHECK(load_scene(SCRAPS_SANDBOX_SCENE_DIR, name, scene, error));
    World world = make_world(scene, movement_rules_for(scene.level.perspective));
    CommandList join;
    join.push(make_command(CommandType::kPlayerJoin, 0));
    test::step(world, join);

    headless::HeadlessRenderer renderer(1280, 720);
    renderer.set_capture(true);
    SpriteBank sprites;
    std::string errors;
    const bool loaded = sprites.load(world.level, SCRAPS_SANDBOX_SCENE_DIR, renderer, errors);
    if (!loaded) std::printf("    %s", errors.c_str());
    CHECK(loaded);
    CHECK(sprites.loaded_count() == world.level.images.size());
    CHECK(renderer.live_textures() == static_cast<int>(world.level.images.size()));

    for (int p = 0; p < 3; ++p) {
      const std::unique_ptr<View> view = make_view(static_cast<Perspective>(p));
      Camera camera;
      camera.zoom = view->default_zoom();
      camera.target = scene.level.spawn_points[0];
      renderer.begin_frame(Color());
      view->draw(world, world, 0.0f, camera, renderer, &sprites);
      const std::vector<TextureId>& batches = renderer.captured_batch_textures();
      CHECK(!batches.empty());
      bool textured = false;
      bool merged = true;
      for (std::size_t i = 0; i < batches.size(); ++i) {
        if (batches[i] != kNoTexture) textured = true;
        if (i > 0 && batches[i] == batches[i - 1]) merged = false;  // should have been one batch
      }
      CHECK(textured);
      CHECK(merged);
    }
  }
  // Destroying the bank releases its textures.
  headless::HeadlessRenderer renderer(64, 64);
  {
    Scene scene;
    std::string error;
    CHECK(load_scene(SCRAPS_SANDBOX_SCENE_DIR, "side", scene, error));
    SpriteBank sprites;
    std::string errors;
    sprites.load(scene.level, SCRAPS_SANDBOX_SCENE_DIR, renderer, errors);
    CHECK(renderer.live_textures() > 0);
  }
  CHECK(renderer.live_textures() == 0);
}

void missing_art_falls_back_to_flat_colour(test::Context& ctx) {
  Scene scene;
  std::string error;
  CHECK(parse_scene(
      "scraps-scene 1\nview top-down\nsize 1 1 1\nimage gone art/does-not-exist.png\n"
      "tile g grass solid 10 20 30 texture gone\narchetype hero\nplayer hero\nspawn 0.5 0.5 1\nfill 0 g\n",
      scene, error));
  const World world = make_world(scene, MovementRules());
  headless::HeadlessRenderer renderer(800, 600);
  renderer.set_capture(true);
  SpriteBank sprites;
  std::string errors;
  CHECK(!sprites.load(world.level, SCRAPS_SANDBOX_SCENE_DIR, renderer, errors));
  CHECK(errors.find("does-not-exist.png") != std::string::npos);
  const std::unique_ptr<View> view = make_view(Perspective::kTopDown);
  Camera camera = test_camera(*view);
  camera.target = Vec3(0.5f, 0.5f, 1.0f);
  renderer.begin_frame(Color());
  view->draw(world, world, 0.0f, camera, renderer, &sprites);
  CHECK(renderer.stats().triangles == 2);  // still drawn
  CHECK(renderer.captured_batch_textures().size() == 1 && renderer.captured_batch_textures()[0] == kNoTexture);
}

void sprite_frames_follow_the_tick(test::Context& ctx) {
  Scene scene;
  std::string error;
  CHECK(load_scene(SCRAPS_SANDBOX_SCENE_DIR, "top-down", scene, error));
  World world = make_world(scene, movement_rules_for(scene.level.perspective));
  CommandList join;
  join.push(make_command(CommandType::kPlayerJoin, 0));
  test::step(world, join);
  Entity* hero = find_entity(world, player_entity(world, 0));
  CHECK(hero != nullptr);
  if (hero == nullptr) return;

  headless::HeadlessRenderer renderer(1280, 720);
  renderer.set_capture(true);
  SpriteBank sprites;
  std::string errors;
  CHECK(sprites.load(world.level, SCRAPS_SANDBOX_SCENE_DIR, renderer, errors));
  const SpriteTexture* hero_sprite = sprites.find(world.level.archetypes[world.level.player_archetype].image);
  CHECK(hero_sprite != nullptr);
  if (hero_sprite == nullptr) return;
  const std::unique_ptr<View> view = make_view(Perspective::kTopDown);
  Camera camera = test_camera(*view);
  camera.target = hero->position;

  // Draws a frame and returns the u of the hero quad's top-left corner, or -1.
  // A textured quad is pushed as (top-left, top-right, bottom-right, ...), and
  // the hero's is the one whose bottom edge sits on its feet, centred on them.
  auto hero_u = [&]() {
    renderer.begin_frame(Color());
    view->draw(world, world, 0.0f, camera, renderer, &sprites);
    const std::vector<Vertex2D>& v = renderer.captured_vertices();
    const Vec2 feet = view->world_to_screen(hero->position + Vec3(0.0f, hero->size.y * 0.5f, 0.0f), camera);
    for (std::size_t i = 0; i + 2 < v.size(); i += 3) {
      const bool on_feet = std::fabs(v[i + 2].y - feet.y) < 0.01f;
      const bool centred = std::fabs((v[i].x + v[i + 1].x) * 0.5f - feet.x) < 0.01f;
      const bool upright = v[i].y < feet.y - 1.0f && v[i].y == v[i + 1].y;
      if (on_feet && centred && upright) return v[i].u;
    }
    return -1.0f;
  };

  // Standing still: frame 0.
  hero->velocity = Vec3();
  world.tick = 45;
  float u = hero_u();
  CHECK_NEAR(u, 0.0f, 0.01);
  // Moving right at tick 45 with 8 fps: frame (45 * 8 / 60) % 4 = 2, so u starts at 2/4.
  hero->velocity = Vec3(1.0f, 0.0f, 0.0f);
  u = hero_u();
  CHECK_NEAR(u, 0.5f, 0.01);
  // Facing left (simulate sets it from movement) flips the picture: the first
  // corner now has the frame's right edge.
  hero->velocity = Vec3(-1.0f, 0.0f, 0.0f);
  hero->facing = Vec2(-1.0f, 0.0f);
  u = hero_u();
  CHECK_NEAR(u, 0.75f, 0.01);
}

// Counts textured quads drawn mirrored (first corner has the larger u).
int mirrored_quads(const std::vector<Vertex2D>& v) {
  int count = 0;
  for (std::size_t i = 0; i + 5 < v.size(); i += 6) {
    if (v[i].u > v[i + 1].u + 0.001f) ++count;
  }
  return count;
}

void sprite_faces_the_way_it_last_moved(test::Context& ctx) {
  Scene scene;
  std::string error;
  CHECK(load_scene(SCRAPS_SANDBOX_SCENE_DIR, "top-down", scene, error));
  World world = make_world(scene, movement_rules_for(scene.level.perspective));
  CommandList join;
  join.push(make_command(CommandType::kPlayerJoin, 0));
  test::step(world, join);
  for (Entity& entity : world.entities) entity.facing = Vec2(1.0f, 0.0f);
  Entity* hero = find_entity(world, player_entity(world, 0));
  CHECK(hero != nullptr);
  if (hero == nullptr) return;
  hero->velocity = Vec3();  // standing still: only facing decides

  headless::HeadlessRenderer renderer(1280, 720);
  renderer.set_capture(true);
  SpriteBank sprites;
  std::string errors;
  CHECK(sprites.load(world.level, SCRAPS_SANDBOX_SCENE_DIR, renderer, errors));
  for (Perspective perspective : {Perspective::kTopDown, Perspective::kSide, Perspective::kIsometric}) {
    const std::unique_ptr<View> view = make_view(perspective);
    Camera camera = test_camera(*view);
    camera.target = hero->position;
    hero->facing = Vec2(1.0f, 0.0f);
    renderer.begin_frame(Color());
    view->draw(world, world, 0.0f, camera, renderer, &sprites);
    CHECK(mirrored_quads(renderer.captured_vertices()) == 0);
    hero->facing = Vec2(-1.0f, 0.0f);  // walked left, then stopped
    renderer.begin_frame(Color());
    view->draw(world, world, 0.0f, camera, renderer, &sprites);
    CHECK(mirrored_quads(renderer.captured_vertices()) == 1);
  }
}

void shadow_sits_on_the_ground_below(test::Context& ctx) {
  World world = test::make_box_world(test::gravity_rules());
  float ground = 0.0f;
  CHECK(ground_below(world.level, Vec3(1.5f, 2.5f, 1.0f), ground) && ground == 1.0f);   // standing on the floor
  CHECK(ground_below(world.level, Vec3(1.5f, 2.5f, 3.5f), ground) && ground == 1.0f);   // high above it
  CHECK(ground_below(world.level, Vec3(3.5f, 2.5f, 2.2f), ground) && ground == 2.0f);   // over the wall block
  CHECK(!ground_below(world.level, Vec3(5.5f, 5.5f, 1.0f), ground));                    // over the pit
  CHECK(!ground_below(world.level, Vec3(-3.0f, 2.5f, 1.0f), ground));                   // off the grid

  test::step(world, test::one(make_command(CommandType::kPlayerJoin, 0)));
  Entity* hero = find_entity(world, player_entity(world, 0));
  CHECK(hero != nullptr);
  if (hero == nullptr) return;
  headless::HeadlessRenderer renderer(1280, 720);
  renderer.set_capture(true);
  // The darkest shadow drawn (alpha of black, translucent triangles; the
  // top-down view's wall strips are alpha 70, so they never decide this).
  auto darkest = [&](const View& view) {
    int best = 0;
    for (const Vertex2D& v : renderer.captured_vertices()) {
      if (v.color.r == 0 && v.color.g == 0 && v.color.b == 0 && v.color.a < 255 && v.color.a != 70) {
        best = std::max(best, static_cast<int>(v.color.a));
      }
    }
    (void)view;
    return best;
  };
  for (Perspective perspective : {Perspective::kTopDown, Perspective::kSide, Perspective::kIsometric}) {
    const std::unique_ptr<View> view = make_view(perspective);
    Camera camera = test_camera(*view);
    camera.target = hero->position;
    hero->position.z = 1.0f;  // on the floor
    renderer.begin_frame(Color());
    view->draw(world, world, 0.0f, camera, renderer);
    const int on_ground = darkest(*view);
    hero->position.z = 3.0f;  // jumped 2 units: half way to gone
    renderer.begin_frame(Color());
    view->draw(world, world, 0.0f, camera, renderer);
    const int in_the_air = darkest(*view);
    CHECK(on_ground == 90);
    CHECK(in_the_air > 0 && in_the_air < on_ground);
  }
}

void controls_turn_keys_into_commands(test::Context& ctx) {
  headless::HeadlessBackend backend;
  headless::HeadlessInput& input = backend.input();
  const std::unique_ptr<View> top_down = make_view(Perspective::kTopDown);
  const std::unique_ptr<View> side = make_view(Perspective::kSide);
  Controls controls;
  CHECK(controls.bind(Key::kE, game_command(7)));

  // Holding D: one Move to +x each tick.
  input.press(Key::kD);
  backend.window().pump_events();
  controls.sample(input);
  CommandList out;
  controls.emit(*top_down, top_down->movement_rules(), 2, out);
  CHECK(out.size() == 1);
  CHECK(out[0].type == CommandType::kMove && out[0].player == 2);
  CHECK_NEAR(out[0].vec.x, 1.0f, 1e-6);
  CHECK_NEAR(out[0].vec.y, 0.0f, 1e-6);

  // A tap of Space between ticks is latched: exactly one Jump, on the next tick only.
  input.press(Key::kSpace);
  backend.window().pump_events();
  controls.sample(input);
  input.release(Key::kSpace);
  backend.window().pump_events();
  controls.sample(input);  // a second frame before the tick
  out.clear();
  controls.emit(*top_down, top_down->movement_rules(), 2, out);
  CHECK(out.size() == 2 && out[1].type == CommandType::kJump);
  CHECK(controls.jumped());
  out.clear();
  controls.emit(*top_down, top_down->movement_rules(), 2, out);
  CHECK(out.size() == 1);
  CHECK(!controls.jumped());

  // In the side view W also jumps; in top-down it only moves.
  input.release(Key::kD);
  input.press(Key::kW);
  backend.window().pump_events();
  controls.sample(input);
  out.clear();
  controls.emit(*side, side->movement_rules(), 0, out);
  CHECK(out.size() == 2 && out[1].type == CommandType::kJump);

  // Bound game commands.
  input.press(Key::kE);
  backend.window().pump_events();
  controls.sample(input);
  out.clear();
  controls.emit(*top_down, top_down->movement_rules(), 0, out);
  CHECK(out.size() == 2 && out[1].type == game_command(7));
}

// Press, release and wheel are each seen in exactly one frame; the pointer
// moves when the frame starts, like every other headless input.
void headless_pointer_latches_per_frame(test::Context& ctx) {
  headless::HeadlessBackend backend;
  headless::HeadlessInput& input = backend.input();
  headless::HeadlessWindow& window = backend.window();
  CHECK(!input.pointer_inside());
  CHECK(input.wheel() == 0.0f);

  input.move_pointer(100.0f, 50.0f);
  CHECK(input.pointer().x == 0.0f && !input.pointer_inside());  // not before the frame starts
  window.pump_events();
  CHECK(input.pointer().x == 100.0f && input.pointer().y == 50.0f && input.pointer_inside());

  input.press(MouseButton::kLeft);
  window.pump_events();
  CHECK(input.was_pressed(MouseButton::kLeft) && input.is_down(MouseButton::kLeft));
  CHECK(!input.was_released(MouseButton::kLeft));
  CHECK(!input.was_pressed(MouseButton::kRight) && !input.is_down(MouseButton::kRight));
  input.press(MouseButton::kLeft);  // still held: not a new press
  window.pump_events();
  CHECK(!input.was_pressed(MouseButton::kLeft) && input.is_down(MouseButton::kLeft));

  input.release(MouseButton::kLeft);
  window.pump_events();
  CHECK(input.was_released(MouseButton::kLeft) && !input.is_down(MouseButton::kLeft));
  window.pump_events();
  CHECK(!input.was_released(MouseButton::kLeft));
  input.release(MouseButton::kLeft);  // not held: no release
  window.pump_events();
  CHECK(!input.was_released(MouseButton::kLeft));

  // A click shorter than a frame: both halves are seen, in one frame.
  input.press(MouseButton::kRight);
  input.release(MouseButton::kRight);
  window.pump_events();
  CHECK(input.was_pressed(MouseButton::kRight) && input.was_released(MouseButton::kRight));
  CHECK(!input.is_down(MouseButton::kRight));
  window.pump_events();
  CHECK(!input.was_pressed(MouseButton::kRight) && !input.was_released(MouseButton::kRight));

  // Wheel notches add up within a frame and reset after it.
  input.scroll(1.0f);
  input.scroll(0.5f);
  window.pump_events();
  CHECK(input.wheel() == 1.5f);
  window.pump_events();
  CHECK(input.wheel() == 0.0f);

  // Out-of-range buttons are never down.
  input.press(MouseButton::kCount);
  window.pump_events();
  CHECK(!input.is_down(MouseButton::kCount) && !input.was_pressed(MouseButton::kCount));
}

// The coordinate that a View's pointer plane fixes: z for top-down and
// isometric, y (the lane) for the side view.
float plane_coordinate(Perspective perspective, const Vec3& point) {
  return perspective == Perspective::kSide ? point.y : point.z;
}

void screen_to_world_inverts_world_to_screen(test::Context& ctx) {
  const Perspective perspectives[] = {Perspective::kTopDown, Perspective::kSide, Perspective::kIsometric};
  const float zooms[] = {6.0f, 13.7f, 40.0f, 56.0f, 200.0f};
  const Vec3 targets[] = {Vec3(0.0f, 0.0f, 0.0f), Vec3(6.25f, 6.5f, 1.45f), Vec3(20.0f, 1.5f, 3.0f)};
  const Vec3 throughs[] = {Vec3(6.5f, 6.5f, 1.0f), Vec3(2.0f, 0.5f, 3.75f), Vec3(-1.0f, 2.5f, 0.0f)};
  for (Perspective perspective : perspectives) {
    const std::unique_ptr<View> view = make_view(perspective);
    float worst_pixels = 0.0f;
    float worst_plane = 0.0f;
    for (float zoom : zooms) {
      for (const Vec3& target : targets) {
        for (const Vec3& through : throughs) {
          Camera camera;
          camera.zoom = zoom;
          camera.target = target;
          camera.viewport_width = 1280.0f;
          camera.viewport_height = 720.0f;
          for (float x = -40.0f; x <= 1320.0f; x += 97.0f) {
            for (float y = -30.0f; y <= 750.0f; y += 61.0f) {
              const Vec3 world = view->screen_to_world(Vec2(x, y), camera, through);
              const Vec2 back = view->world_to_screen(world, camera);
              worst_pixels = std::max(worst_pixels, std::max(std::fabs(back.x - x), std::fabs(back.y - y)));
              worst_plane = std::max(worst_plane, std::fabs(plane_coordinate(perspective, world) -
                                                            plane_coordinate(perspective, through)));
            }
          }
        }
      }
    }
    if (worst_pixels > 1e-3f) std::printf("    %s: off by %g px\n", view->name(), static_cast<double>(worst_pixels));
    CHECK(worst_pixels <= 1e-3f);
    CHECK(worst_plane == 0.0f);  // exactly on the plane
  }
}

void screen_to_world_known_points(test::Context& ctx) {
  Camera camera;
  camera.zoom = 40.0f;
  camera.target = Vec3(2.0f, 3.0f, 1.0f);
  camera.viewport_width = 800.0f;
  camera.viewport_height = 600.0f;
  const Vec2 centre(400.0f, 300.0f);
  const Vec3 through(7.0f, 0.5f, 2.0f);

  // The centre of the screen is the camera target, moved along the viewing
  // direction onto the plane.
  const std::unique_ptr<View> top_down = make_view(Perspective::kTopDown);
  const std::unique_ptr<View> side = make_view(Perspective::kSide);
  const std::unique_ptr<View> iso = make_view(Perspective::kIsometric);
  CHECK(top_down->screen_to_world(centre, camera, through) == Vec3(2.0f, 3.0f, 2.0f));
  CHECK(side->screen_to_world(centre, camera, through) == Vec3(2.0f, 0.5f, 1.0f));
  CHECK(iso->screen_to_world(centre, camera, through) == Vec3(3.0f, 4.0f, 2.0f));  // target + (1, 1, 1)

  // One cell right and one cell up on screen (40 px each).
  CHECK(top_down->screen_to_world(Vec2(440.0f, 260.0f), camera, through) == Vec3(3.0f, 2.0f, 2.0f));
  CHECK(side->screen_to_world(Vec2(440.0f, 260.0f), camera, through) == Vec3(3.0f, 0.5f, 2.0f));

  // Isometric, worked by hand on the target's own plane (z = 1): 40 px right
  // and 20 px down is a = 2 * 40 / 40 = 2, b = 4 * 20 / 40 = 2, so
  // x = 2 + (2 + 2) / 2 = 4, y = 3 + (2 - 2) / 2 = 3: two cells along +x.
  const Vec3 on_target_plane(0.0f, 0.0f, 1.0f);
  CHECK(iso->screen_to_world(Vec2(440.0f, 320.0f), camera, on_target_plane) == Vec3(4.0f, 3.0f, 1.0f));
  // 40 px left and 20 px down is two cells along +y.
  CHECK(iso->screen_to_world(Vec2(360.0f, 320.0f), camera, on_target_plane) == Vec3(2.0f, 5.0f, 1.0f));

  // A camera with no zoom picks nothing new.
  Camera flat = camera;
  flat.zoom = 0.0f;
  for (const View* view : {top_down.get(), side.get(), iso.get()}) {
    CHECK(view->screen_to_world(centre, flat, through) == through);
  }
}

void click_is_emitted_once(test::Context& ctx) {
  headless::HeadlessBackend backend;
  headless::HeadlessInput& input = backend.input();
  const std::unique_ptr<View> view = make_view(Perspective::kTopDown);
  Camera camera;
  camera.zoom = 40.0f;
  camera.target = Vec3(5.0f, 5.0f, 0.0f);
  camera.viewport_width = 1280.0f;
  camera.viewport_height = 720.0f;
  const Vec3 through(5.0f, 5.0f, 1.0f);
  const CommandType place = game_command(9);

  Controls controls;
  CHECK(controls.bind(Key::kE, game_command(7)));
  CHECK(controls.bind(MouseButton::kLeft, place));
  CHECK(!controls.bind(MouseButton::kCount, place));

  auto count = [](const CommandList& list, CommandType type) {
    int n = 0;
    for (const Command& command : list) n += command.type == type ? 1 : 0;
    return n;
  };

  // Frame 1: the click, one cell right and two up from the centre. No tick runs.
  input.move_pointer(680.0f, 280.0f);
  input.press(MouseButton::kLeft);
  backend.window().pump_events();
  controls.sample(input, *view, camera, through);
  // Frame 2: still no tick; the button is up and the camera has moved. The
  // click keeps the point it had when it was pressed.
  input.release(MouseButton::kLeft);
  input.move_pointer(10.0f, 10.0f);
  camera.target = Vec3(9.0f, 9.0f, 0.0f);
  backend.window().pump_events();
  controls.sample(input, *view, camera, through);

  CommandList out;
  controls.emit(*view, view->movement_rules(), 3, out);
  CHECK(count(out, place) == 1);
  CHECK(count(out, game_command(7)) == 0);
  for (const Command& command : out) {
    if (command.type != place) continue;
    CHECK(command.player == 3);
    CHECK_NEAR(command.vec.x, 6.0f, 1e-5);
    CHECK_NEAR(command.vec.y, 3.0f, 1e-5);
    CHECK_NEAR(command.vec.z, 1.0f, 1e-5);
  }
  out.clear();
  controls.emit(*view, view->movement_rules(), 3, out);  // the next tick: gone
  CHECK(count(out, place) == 0);

  // Holding the button is one click, not one per frame.
  input.press(MouseButton::kLeft);
  for (int frame = 0; frame < 3; ++frame) {
    backend.window().pump_events();
    controls.sample(input, *view, camera, through);
    out.clear();
    controls.emit(*view, view->movement_rules(), 3, out);
    CHECK(count(out, place) == (frame == 0 ? 1 : 0));
  }
  input.release(MouseButton::kLeft);

  // Keys only: without a view there is nothing to pick with, so no click.
  input.press(MouseButton::kLeft);
  backend.window().pump_events();
  controls.sample(input);
  out.clear();
  controls.emit(*view, view->movement_rules(), 3, out);
  CHECK(count(out, place) == 0);

  // Keys and buttons share kMaxBindings slots.
  Controls full;
  for (std::size_t i = 0; i < Controls::kMaxBindings; ++i) {
    const CommandType type = game_command(static_cast<std::uint16_t>(i));
    CHECK(i % 2 == 0 ? full.bind(Key::kE, type) : full.bind(MouseButton::kRight, type));
  }
  CHECK(!full.bind(MouseButton::kLeft, place));
  CHECK(!full.bind(Key::kQ, place));
}

// ------------------------------------------------------------ gamepad

int count_type(const CommandList& list, CommandType type) {
  int n = 0;
  for (const Command& command : list) n += command.type == type ? 1 : 0;
  return n;
}

// One frame and one tick: what Controls sends for the input as it is now.
CommandList frame_and_tick(headless::HeadlessBackend& backend, Controls& controls, const View& view,
                           PlayerId player = 0) {
  backend.window().pump_events();
  controls.sample(backend.input());
  CommandList out;
  controls.emit(view, view.movement_rules(), player, out);
  return out;
}

Vec2 move_of(const CommandList& list) {
  for (const Command& command : list) {
    if (command.type == CommandType::kMove) return Vec2(command.vec.x, command.vec.y);
  }
  return Vec2(99.0f, 99.0f);  // no Move at all: fails every check below
}

// Press, release and axes are seen at the next frame; a tap inside one frame
// is both pressed and released in it; nothing works while unplugged.
void headless_gamepad_latches_per_frame(test::Context& ctx) {
  headless::HeadlessBackend backend;
  headless::HeadlessInput& input = backend.input();
  headless::HeadlessWindow& window = backend.window();
  CHECK(!input.gamepad_connected());

  // Unplugged: presses and axes are ignored.
  input.press(GamepadButton::kSouth);
  input.set_axis(GamepadAxis::kLeftX, 1.0f);
  window.pump_events();
  CHECK(!input.is_down(GamepadButton::kSouth) && !input.was_pressed(GamepadButton::kSouth));
  CHECK(input.axis(GamepadAxis::kLeftX) == 0.0f);

  input.connect_gamepad(true);
  CHECK(!input.gamepad_connected());  // not before the frame starts
  window.pump_events();
  CHECK(input.gamepad_connected());

  input.press(GamepadButton::kSouth);
  input.set_axis(GamepadAxis::kLeftX, -0.5f);
  CHECK(!input.is_down(GamepadButton::kSouth) && input.axis(GamepadAxis::kLeftX) == 0.0f);
  window.pump_events();
  CHECK(input.is_down(GamepadButton::kSouth) && input.was_pressed(GamepadButton::kSouth));
  CHECK(!input.was_released(GamepadButton::kSouth));
  CHECK(input.axis(GamepadAxis::kLeftX) == -0.5f);
  window.pump_events();  // still held, axis still there
  CHECK(input.is_down(GamepadButton::kSouth) && !input.was_pressed(GamepadButton::kSouth));
  CHECK(input.axis(GamepadAxis::kLeftX) == -0.5f);
  input.release(GamepadButton::kSouth);
  window.pump_events();
  CHECK(!input.is_down(GamepadButton::kSouth) && input.was_released(GamepadButton::kSouth));
  window.pump_events();
  CHECK(!input.was_released(GamepadButton::kSouth));

  // A tap shorter than a frame.
  input.press(GamepadButton::kWest);
  input.release(GamepadButton::kWest);
  window.pump_events();
  CHECK(input.was_pressed(GamepadButton::kWest) && input.was_released(GamepadButton::kWest));
  CHECK(!input.is_down(GamepadButton::kWest));

  // Axes are clamped to their range; not finite is 0.
  input.set_axis(GamepadAxis::kLeftY, 3.0f);
  input.set_axis(GamepadAxis::kRightX, -7.0f);
  input.set_axis(GamepadAxis::kLeftTrigger, -0.5f);
  input.set_axis(GamepadAxis::kRightTrigger, std::numeric_limits<float>::quiet_NaN());
  input.set_axis(GamepadAxis::kRightY, std::numeric_limits<float>::infinity());
  window.pump_events();
  CHECK(input.axis(GamepadAxis::kLeftY) == 1.0f);
  CHECK(input.axis(GamepadAxis::kRightX) == -1.0f);
  CHECK(input.axis(GamepadAxis::kLeftTrigger) == 0.0f);
  CHECK(input.axis(GamepadAxis::kRightTrigger) == 0.0f);
  CHECK(input.axis(GamepadAxis::kRightY) == 0.0f);

  // Out of range is never down and reads 0.
  input.press(GamepadButton::kCount);
  window.pump_events();
  CHECK(!input.is_down(GamepadButton::kCount) && !input.was_pressed(GamepadButton::kCount));
  CHECK(input.axis(GamepadAxis::kCount) == 0.0f);

  // Unplugged while held: let go, axes centred, no "released".
  input.press(GamepadButton::kNorth);
  window.pump_events();
  input.connect_gamepad(false);
  window.pump_events();
  CHECK(!input.gamepad_connected());
  CHECK(!input.is_down(GamepadButton::kNorth) && !input.was_released(GamepadButton::kNorth));
  CHECK(input.axis(GamepadAxis::kLeftX) == 0.0f && input.axis(GamepadAxis::kLeftY) == 0.0f);
}

void stick_dead_zone_is_radial_and_rescaled(test::Context& ctx) {
  const float dz = Controls::kStickDeadZone;
  CHECK(dz == 0.2f);
  // Resting, or inside the circle: nothing.
  CHECK(Controls::apply_dead_zone(Vec2()) == Vec2());
  CHECK(Controls::apply_dead_zone(Vec2(0.19f, 0.0f)) == Vec2());
  CHECK(Controls::apply_dead_zone(Vec2(0.0f, -0.199f)) == Vec2());
  // Radial: (0.15, 0.15) is 0.21 long, just outside, though each axis alone is inside.
  const Vec2 diagonal = Controls::apply_dead_zone(Vec2(0.15f, 0.15f));
  CHECK(diagonal.x > 0.0f && diagonal.y > 0.0f);
  CHECK_NEAR(length(diagonal), (std::sqrt(0.045f) - dz) / (1.0f - dz), 1e-5);
  CHECK_NEAR(diagonal.x, diagonal.y, 1e-6);  // direction kept
  // Rescaled: starts at 0 at the edge, half way at 0.6, all the way at 1.
  CHECK_NEAR(length(Controls::apply_dead_zone(Vec2(0.2001f, 0.0f))), 0.0f, 1e-3);
  CHECK_NEAR(Controls::apply_dead_zone(Vec2(0.6f, 0.0f)).x, 0.5f, 1e-5);
  CHECK_NEAR(Controls::apply_dead_zone(Vec2(0.0f, -1.0f)).y, -1.0f, 1e-6);
  // A stick's corner can read (1, 1): still no longer than 1.
  CHECK_NEAR(length(Controls::apply_dead_zone(Vec2(1.0f, 1.0f))), 1.0f, 1e-5);
  // Garbage in, nothing out.
  CHECK(Controls::apply_dead_zone(Vec2(std::numeric_limits<float>::quiet_NaN(), 0.5f)) == Vec2());
  CHECK(Controls::apply_dead_zone(Vec2(std::numeric_limits<float>::infinity(), 0.0f)) == Vec2());

  // Through Controls: a stick resting a little off centre does not walk.
  headless::HeadlessBackend backend;
  backend.input().connect_gamepad(true);
  backend.input().set_axis(GamepadAxis::kLeftX, 0.12f);
  backend.input().set_axis(GamepadAxis::kLeftY, -0.1f);
  const std::unique_ptr<View> view = make_view(Perspective::kTopDown);
  Controls controls;
  CHECK(move_of(frame_and_tick(backend, controls, *view)) == Vec2());
}

// The stick moves like the keys, through the View's screen-to-ground mapping
// (Rule 4), but analog: what the player sees is the direction pushed, and
// Move's vec is as long as the push (after the dead zone).
void stick_moves_the_way_it_is_pushed_in_every_view(test::Context& ctx) {
  const Perspective perspectives[] = {Perspective::kTopDown, Perspective::kSide, Perspective::kIsometric};
  const Vec2 pushes[] = {Vec2(1, 0), Vec2(-1, 0), Vec2(0, 1), Vec2(0, -1), Vec2(0.6f, 0.8f), Vec2(-0.8f, -0.6f)};
  const float amounts[] = {1.0f, 0.6f};  // all the way; half way after the dead zone
  for (Perspective perspective : perspectives) {
    const std::unique_ptr<View> view = make_view(perspective);
    const Camera camera = test_camera(*view);
    headless::HeadlessBackend backend;
    backend.input().connect_gamepad(true);
    Controls controls;
    for (Vec2 push : pushes) {
      for (float amount : amounts) {
        backend.input().set_axis(GamepadAxis::kLeftX, push.x * amount);
        backend.input().set_axis(GamepadAxis::kLeftY, push.y * amount);
        const Vec2 ground = move_of(frame_and_tick(backend, controls, *view));
        const float expected = (amount - Controls::kStickDeadZone) / (1.0f - Controls::kStickDeadZone);
        Vec2 screen = push;
        if (perspective == Perspective::kSide) {
          // Only left/right exist on the side view's ground, and only the
          // sideways part of the push counts: up alone does not walk.
          CHECK(ground.y == 0.0f);
          CHECK_NEAR(std::fabs(ground.x), std::fabs(push.x) * expected, 1e-4);
          if (push.x == 0.0f) continue;
          screen = Vec2(push.x, 0.0f);
        } else {
          CHECK_NEAR(length(ground), expected, 1e-4);
        }
        CHECK(length(ground) <= 1.0f + 1e-6f);
        const Vec3 origin(3.0f, 3.0f, 1.0f);
        const Vec2 s0 = view->world_to_screen(origin, camera);
        const Vec2 s1 = view->world_to_screen(origin + Vec3(ground.x, ground.y, 0.0f), camera);
        const Vec2 moved = s1 - s0;
        const float cross = moved.x * screen.y - moved.y * screen.x;
        const float dot = moved.x * screen.x + moved.y * screen.y;
        CHECK_NEAR(cross / length(moved), 0.0f, 1e-2);
        CHECK(dot > 0.0f);
      }
    }
    // A held key wins over the stick: D walks right at full speed even with
    // the stick pushed left.
    backend.input().set_axis(GamepadAxis::kLeftX, -1.0f);
    backend.input().set_axis(GamepadAxis::kLeftY, 0.0f);
    backend.input().press(Key::kD);
    const Vec2 keyed = move_of(frame_and_tick(backend, controls, *view));
    CHECK(keyed == view->screen_to_ground_direction(Vec2(1.0f, 0.0f)));
    backend.input().release(Key::kD);
  }
}

// The d-pad is WASD: the same Move for every combination in every view, and
// in the side view d-pad up jumps like W.
void dpad_equals_wasd(test::Context& ctx) {
  const Perspective perspectives[] = {Perspective::kTopDown, Perspective::kSide, Perspective::kIsometric};
  const Key keys[4] = {Key::kW, Key::kA, Key::kS, Key::kD};
  const GamepadButton dpad[4] = {GamepadButton::kDpadUp, GamepadButton::kDpadLeft, GamepadButton::kDpadDown,
                                 GamepadButton::kDpadRight};
  for (Perspective perspective : perspectives) {
    const std::unique_ptr<View> view = make_view(perspective);
    for (int mask = 0; mask < 16; ++mask) {
      headless::HeadlessBackend by_keys;
      headless::HeadlessBackend by_pad;
      by_pad.input().connect_gamepad(true);
      for (int i = 0; i < 4; ++i) {
        if ((mask & (1 << i)) == 0) continue;
        by_keys.input().press(keys[i]);
        by_pad.input().press(dpad[i]);
      }
      Controls key_controls;
      Controls pad_controls;
      const CommandList a = frame_and_tick(by_keys, key_controls, *view);
      const CommandList b = frame_and_tick(by_pad, pad_controls, *view);
      CHECK(a.size() == b.size());
      for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        CHECK(a[i].type == b[i].type && a[i].vec == b[i].vec);
      }
      // Up jumps exactly where W does (only the side view).
      const bool jumps = (mask & 1) != 0 && perspective == Perspective::kSide;
      CHECK(count_type(b, CommandType::kJump) == (jumps ? 1 : 0));
    }
  }
}

// South jumps, and a bound gamepad button sends its command; a tap between
// two ticks (pressed and released before the tick runs) is sent exactly once,
// and a held button is not repeated.
void gamepad_tap_between_ticks_is_emitted_once(test::Context& ctx) {
  headless::HeadlessBackend backend;
  headless::HeadlessInput& input = backend.input();
  const std::unique_ptr<View> view = make_view(Perspective::kTopDown);
  const CommandType drop = game_command(4);
  Controls controls;
  CHECK(controls.bind(GamepadButton::kWest, drop));
  input.connect_gamepad(true);

  // Frame 1: south and west go down. Frame 2: both come up. Then the tick.
  input.press(GamepadButton::kSouth);
  input.press(GamepadButton::kWest);
  backend.window().pump_events();
  controls.sample(input);
  input.release(GamepadButton::kSouth);
  input.release(GamepadButton::kWest);
  backend.window().pump_events();
  controls.sample(input);
  CommandList out;
  controls.emit(*view, view->movement_rules(), 1, out);
  CHECK(count_type(out, CommandType::kJump) == 1);
  CHECK(count_type(out, drop) == 1);
  CHECK(controls.jumped());
  out.clear();
  controls.emit(*view, view->movement_rules(), 1, out);  // the next tick: gone
  CHECK(count_type(out, CommandType::kJump) == 0 && count_type(out, drop) == 0);

  // A tap inside a single frame counts too.
  input.press(GamepadButton::kWest);
  input.release(GamepadButton::kWest);
  out = frame_and_tick(backend, controls, *view, 1);
  CHECK(count_type(out, drop) == 1);

  // Two ticks in one frame: the press goes with the first one only.
  input.press(GamepadButton::kSouth);
  backend.window().pump_events();
  controls.sample(input);
  out.clear();
  controls.emit(*view, view->movement_rules(), 1, out);
  controls.emit(*view, view->movement_rules(), 1, out);
  CHECK(count_type(out, CommandType::kJump) == 1);
  // Held over more frames: still that one jump.
  for (int frame = 0; frame < 3; ++frame) {
    CHECK(count_type(frame_and_tick(backend, controls, *view, 1), CommandType::kJump) == 0);
  }
  input.release(GamepadButton::kSouth);
}

// A gamepad binding sends its command (with the player); bindings share the
// slots, an invalid button is refused, and clear_bindings drops it.
void gamepad_binding_emits_its_command(test::Context& ctx) {
  headless::HeadlessBackend backend;
  headless::HeadlessInput& input = backend.input();
  const std::unique_ptr<View> view = make_view(Perspective::kIsometric);
  const CommandType pad_command = game_command(11);
  const CommandType key_command = game_command(12);
  Controls controls;
  CHECK(controls.bind(Key::kE, key_command));
  CHECK(controls.bind(GamepadButton::kRightShoulder, pad_command));
  CHECK(!controls.bind(GamepadButton::kCount, pad_command));
  input.connect_gamepad(true);

  input.press(GamepadButton::kRightShoulder);
  CommandList out = frame_and_tick(backend, controls, *view, 5);
  CHECK(count_type(out, pad_command) == 1 && count_type(out, key_command) == 0);
  for (const Command& command : out) {
    if (command.type == pad_command) CHECK(command.player == 5);
  }
  input.release(GamepadButton::kRightShoulder);
  // The key binding is not set off by the pad, nor the pad binding by the key.
  input.press(Key::kE);
  out = frame_and_tick(backend, controls, *view, 5);
  CHECK(count_type(out, pad_command) == 0 && count_type(out, key_command) == 1);
  input.release(Key::kE);

  // Swapped game code: the binding is gone, latched presses too.
  input.press(GamepadButton::kRightShoulder);
  backend.window().pump_events();
  controls.sample(input);
  controls.clear_bindings();
  out.clear();
  controls.emit(*view, view->movement_rules(), 5, out);
  CHECK(count_type(out, pad_command) == 0);
  input.release(GamepadButton::kRightShoulder);

  // Keys, mouse and gamepad buttons share kMaxBindings slots.
  Controls full;
  for (std::size_t i = 0; i < Controls::kMaxBindings; ++i) {
    const CommandType type = game_command(static_cast<std::uint16_t>(i));
    CHECK(i % 3 == 0 ? full.bind(Key::kE, type)
                     : (i % 3 == 1 ? full.bind(MouseButton::kRight, type) : full.bind(GamepadButton::kNorth, type)));
  }
  CHECK(!full.bind(GamepadButton::kSouth, pad_command));
}

// A pad plugged in while the game runs works from the next frame; before it,
// and after it is pulled out, nothing it did moves or sends anything.
void gamepad_connected_mid_game(test::Context& ctx) {
  headless::HeadlessBackend backend;
  headless::HeadlessInput& input = backend.input();
  const std::unique_ptr<View> view = make_view(Perspective::kSide);
  const CommandType drop = game_command(2);
  Controls controls;
  CHECK(controls.bind(GamepadButton::kWest, drop));

  // Keyboard play, no pad.
  for (int tick = 0; tick < 5; ++tick) {
    input.press(Key::kD);
    const CommandList out = frame_and_tick(backend, controls, *view);
    CHECK(move_of(out) == Vec2(1.0f, 0.0f));
  }
  input.release(Key::kD);
  // Pressing on an unplugged pad does nothing.
  input.press(GamepadButton::kSouth);
  input.set_axis(GamepadAxis::kLeftX, -1.0f);
  CommandList out = frame_and_tick(backend, controls, *view);
  CHECK(move_of(out) == Vec2() && count_type(out, CommandType::kJump) == 0);

  // Plugged in: the stick walks left, south jumps, west drops.
  input.connect_gamepad(true);
  input.set_axis(GamepadAxis::kLeftX, -1.0f);
  input.press(GamepadButton::kSouth);
  input.press(GamepadButton::kWest);
  out = frame_and_tick(backend, controls, *view);
  CHECK_NEAR(move_of(out).x, -1.0f, 1e-6);
  CHECK(count_type(out, CommandType::kJump) == 1);
  CHECK(count_type(out, drop) == 1);
  input.release(GamepadButton::kSouth);
  input.release(GamepadButton::kWest);
  CHECK_NEAR(move_of(frame_and_tick(backend, controls, *view)).x, -1.0f, 1e-6);

  // Pulled out with the stick still pushed: the player stops.
  input.connect_gamepad(false);
  out = frame_and_tick(backend, controls, *view);
  CHECK(move_of(out) == Vec2());
  CHECK(count_type(out, CommandType::kJump) == 0 && count_type(out, drop) == 0);
  // And the keyboard still works.
  input.press(Key::kA);
  CHECK(move_of(frame_and_tick(backend, controls, *view)) == Vec2(-1.0f, 0.0f));
}

}  // namespace

void bundled_sounds_load_in_every_scene(test::Context& ctx) {
  for (const char* name : {"top-down", "side", "isometric", "crowd"}) {
    Scene scene;
    std::string error;
    CHECK(load_scene(SCRAPS_SANDBOX_SCENE_DIR, name, scene, error));
    Mixer mixer;
    SoundBank bank;
    std::string errors;
    const bool ok = bank.load(scene.level, SCRAPS_SANDBOX_SCENE_DIR, mixer, errors);
    if (!ok) std::printf("    %s: %s", name, errors.c_str());
    CHECK(ok);
    CHECK(mixer.sound_count() == scene.level.sounds.size() && mixer.sound_count() >= 3);
    bank.play("coin", mixer);
    bank.play("no such sound", mixer);
    CHECK(mixer.playing() == 1);
    std::vector<float> out(480 * kSoundChannels);
    mixer.mix(out.data(), 480);
    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    CHECK(peak > 0.05f);  // it is audible
  }
}

void register_view_tests(test::Registry& registry) {
  registry.add("views.bundled_sounds_load_in_every_scene", bundled_sounds_load_in_every_scene);
  registry.add("views.sprite_faces_the_way_it_last_moved", sprite_faces_the_way_it_last_moved);
  registry.add("views.shadow_sits_on_the_ground_below", shadow_sits_on_the_ground_below);
  registry.add("views.isometric_looks_along_the_diagonal", isometric_looks_along_the_diagonal);
  registry.add("views.input_directions_match_the_screen", input_directions_match_the_screen);
  registry.add("views.isometric_paints_back_to_front", isometric_paints_back_to_front);
  registry.add("views.isometric_entities_stand_in_front_of_their_floor", isometric_entities_stand_in_front_of_their_floor);
  registry.add("views.side_view_cuts_away_lanes_in_front", side_view_cuts_away_lanes_in_front);
  registry.add("views.every_view_draws_every_bundled_scene", every_view_draws_every_bundled_scene);
  registry.add("views.bundled_art_loads_and_batches_split_only_on_texture_change",
               bundled_art_loads_and_batches_split_only_on_texture_change);
  registry.add("views.missing_art_falls_back_to_flat_colour", missing_art_falls_back_to_flat_colour);
  registry.add("views.sprite_frames_follow_the_tick", sprite_frames_follow_the_tick);
  registry.add("views.controls_turn_keys_into_commands", controls_turn_keys_into_commands);
  registry.add("views.screen_to_world_inverts_world_to_screen", screen_to_world_inverts_world_to_screen);
  registry.add("views.screen_to_world_known_points", screen_to_world_known_points);
  registry.add("platform.headless_pointer_latches_per_frame", headless_pointer_latches_per_frame);
  registry.add("controls.click_is_emitted_once", click_is_emitted_once);
  registry.add("platform.headless_gamepad_latches_per_frame", headless_gamepad_latches_per_frame);
  registry.add("controls.stick_dead_zone_is_radial_and_rescaled", stick_dead_zone_is_radial_and_rescaled);
  registry.add("controls.stick_moves_the_way_it_is_pushed_in_every_view",
               stick_moves_the_way_it_is_pushed_in_every_view);
  registry.add("controls.dpad_equals_wasd", dpad_equals_wasd);
  registry.add("controls.gamepad_tap_between_ticks_is_emitted_once", gamepad_tap_between_ticks_is_emitted_once);
  registry.add("controls.gamepad_binding_emits_its_command", gamepad_binding_emits_its_command);
  registry.add("controls.gamepad_connected_mid_game", gamepad_connected_mid_game);
}
