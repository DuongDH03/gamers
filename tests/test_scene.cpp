#include <string>

#include "core/scene.h"
#include "core/serialize.h"
#include "core/world.h"
#include "harness.h"
#include "test_world.h"
#include "views/view_factory.h"

using namespace scraps;

namespace {

void the_fixture_scene_parses(test::Context& ctx) {
  Scene scene;
  std::string error;
  CHECK(parse_scene(test::kBoxScene, scene, error));
  CHECK(error.empty());
  const Level& level = scene.level;
  CHECK(level.perspective == Perspective::kTopDown);
  CHECK(level.grid.width == 6 && level.grid.depth == 6 && level.grid.height == 2);
  CHECK(level.tiles.size() == 3);  // empty + ground + wall
  CHECK(level.archetypes.size() == 2);
  CHECK(level.player_archetype == 0);
  CHECK(level.spawn_points.size() == 1);
  CHECK(scene.entities.size() == 1);
  CHECK(is_solid_cell(level, 3, 2, 1));
  CHECK(!is_solid_cell(level, 2, 2, 1));
  CHECK(!is_solid_cell(level, 5, 5, 0));  // the pit
  CHECK(is_solid_cell(level, -1, 0, 1));  // outside x/y: invisible wall
  CHECK(!is_solid_cell(level, 0, 0, 2));  // above the grid: open
  CHECK(level.archetypes[0].flags == (kEntityMoves | kEntityGravity | kEntityCollides));
  CHECK(scene.content_hash != 0);
}

void errors_name_the_line_and_the_problem(test::Context& ctx) {
  struct Case {
    const char* text;
    const char* expected;
  };
  const Case cases[] = {
      {"", "empty scene"},
      {"view top-down\n", "line 1: the first line must be 'scraps-scene 1'"},
      {"scraps-scene 2\n", "unsupported scene format version"},
      {"scraps-scene 1\nbogus 1 2\n", "line 2: unknown directive 'bogus'"},
      {"scraps-scene 1\nview sideways\n", "unknown view 'sideways'"},
      {"scraps-scene 1\nsize 2 2 1\nsize 2 2 1\n", "line 3: 'size' given twice"},
      {"scraps-scene 1\nsize 2 0 1\n", "size values must be from 1"},
      {"scraps-scene 1\nlayer 0\n", "'size' must come before layers"},
      {"scraps-scene 1\nsize 3 2 1\nlayer 0\n...\n..\n", "line 5: row 1 of layer 0 has 2 characters, expected 3"},
      {"scraps-scene 1\nsize 2 1 1\nlayer 0\n.x\n", "unknown tile symbol 'x'"},
      {"scraps-scene 1\nsize 2 1 1\nlayer 0\n", "needs 1 rows"},
      {"scraps-scene 1\nsize 2 1 1\nlayer 1\n..\n", "layer must be from 0 to 0"},
      {"scraps-scene 1\ntile . a solid 1 2 3\n", "one printable character other than '.'"},
      {"scraps-scene 1\ntile a a solid 1 2 300\n", "color channels"},
      {"scraps-scene 1\ntile a a lava 1 2 3\n", "'solid' or 'decor'"},
      {"scraps-scene 1\nplayer hero\n", "unknown archetype 'hero'"},
      {"scraps-scene 1\narchetype a flags fly\n", "unknown flag 'fly'"},
      {"scraps-scene 1\narchetype a size 1 1\n", "unexpected 'size' in archetype"},
      {"scraps-scene 1\nview side\nsize 1 1 1\n", "missing 'player'"},
      {"scraps-scene 1\nview side\nsize 1 1 1\narchetype a\nplayer a\n", "missing 'spawn'"},
      {"scraps-scene 1\nsize 1 1 1\narchetype a\nplayer a\nspawn 0 0\n", "expected 'spawn <x> <y> <z>'"},
      {"scraps-scene 1\nspawn 0 0 nan\n", "expected three numbers"},
      {"scraps-scene 1\nspawn 0 0 1e9\n", "spawn point is outside the world"},
      {"scraps-scene 1\nimage a ../secret.png\n", "image path must be relative"},
      {"scraps-scene 1\nimage a C:/x.png\n", "image path must be relative"},
      {"scraps-scene 1\nimage a a.png frames 0\n", "frames must be from 1 to 256"},
      {"scraps-scene 1\nimage a a.png\nimage a b.png\n", "image 'a' declared twice"},
      {"scraps-scene 1\ntile g grass solid 1 2 3 texture nope\n", "unknown image 'nope'"},
      {"scraps-scene 1\ntile g grass solid 1 2 3 texture\n", "[texture <image>]"},
      {"scraps-scene 1\narchetype a sprite nope\n", "unknown image 'nope'"},
      {"scraps-scene 1\narchetype a\ncrowd a 3000\ncrowd a 3000\n", "line 4: too many entities"},
  };
  for (const Case& c : cases) {
    Scene scene;
    std::string error;
    const bool ok = parse_scene(c.text, scene, error);
    CHECK(!ok);
    const bool matches = error.find(c.expected) != std::string::npos;
    if (!matches) std::printf("    expected \"%s\", got \"%s\"\n", c.expected, error.c_str());
    CHECK(matches);
  }
}

void images_textures_and_sprites_parse(test::Context& ctx) {
  const char* text =
      "scraps-scene 1\nview isometric\nsize 1 1 1\n"
      "image grass art/grass.png frames 3\n"
      "image hero art/hero.png frames 4 size 16 24 fps 8\n"
      "tile g grass solid 1 2 3 texture grass\n"
      "tile s stone solid 4 5 6\n"
      "archetype hero sprite hero\nplayer hero\nspawn 0.5 0.5 1\n";
  Scene scene;
  std::string error;
  CHECK(parse_scene(text, scene, error));
  const Level& level = scene.level;
  CHECK(level.images.size() == 2);
  CHECK(path_view(level.images[0].path) == "art/grass.png");
  CHECK(level.images[0].frames == 3 && level.images[0].frame_width == 0);
  CHECK(level.images[1].frame_width == 16 && level.images[1].frame_height == 24 && level.images[1].fps == 8.0f);
  CHECK(level.tiles[1].image == 0);
  CHECK(level.tiles[2].image == kNoImage);
  CHECK(level.archetypes[0].image == 1);
  CHECK(is_valid_asset_path("art/sub_dir/x-1.png"));
  CHECK(!is_valid_asset_path("/abs.png"));
  CHECK(!is_valid_asset_path("a\\b.png"));
}

void hash_ignores_line_endings(test::Context& ctx) {
  const std::string lf = "scraps-scene 1\nview side\n";
  const std::string crlf = "scraps-scene 1\r\nview side\r\n";
  CHECK(hash_scene_text(lf) == hash_scene_text(crlf));
  CHECK(hash_scene_text(lf) != hash_scene_text("scraps-scene 1\nview top-down\n"));

  // CRLF files also parse.
  std::string text = test::kBoxScene;
  std::string with_crlf;
  for (char c : text) {
    if (c == '\n') with_crlf += '\r';
    with_crlf += c;
  }
  Scene a, b;
  std::string error;
  CHECK(parse_scene(text, a, error));
  CHECK(parse_scene(with_crlf, b, error));
  CHECK(a.content_hash == b.content_hash);
}

void scene_names_cannot_be_paths(test::Context& ctx) {
  CHECK(is_valid_scene_name("top-down"));
  CHECK(is_valid_scene_name("level_2"));
  CHECK(!is_valid_scene_name(""));
  CHECK(!is_valid_scene_name("../secret"));
  CHECK(!is_valid_scene_name("a/b"));
  CHECK(!is_valid_scene_name("C:"));
  CHECK(!is_valid_scene_name("has space"));
  CHECK(!is_valid_scene_name("abcdefghijabcdefghijabcdefghij12"));  // 32 characters
}

void bundled_scenes_load_and_build_worlds(test::Context& ctx) {
  struct Case {
    const char* name;
    Perspective perspective;
  };
  const Case cases[] = {{"top-down", Perspective::kTopDown},
                        {"side", Perspective::kSide},
                        {"isometric", Perspective::kIsometric},
                        {"crowd", Perspective::kTopDown}};
  for (const Case& c : cases) {
    Scene scene;
    std::string error;
    const bool ok = load_scene(SCRAPS_SANDBOX_SCENE_DIR, c.name, scene, error);
    if (!ok) std::printf("    %s\n", error.c_str());
    CHECK(ok);
    if (!ok) continue;
    CHECK(scene.name == c.name);
    CHECK(scene.level.perspective == c.perspective);
    const World world = make_world(scene, movement_rules_for(scene.level.perspective));
    std::size_t expected = scene.entities.size();
    for (const SceneCrowd& crowd : scene.crowds) expected += crowd.count;
    CHECK(count_alive(world) == expected);

    // Every spawn point and scene entity starts outside solid cells.
    for (const Vec3& spawn : scene.level.spawn_points) {
      const Archetype& hero = scene.level.archetypes[scene.level.player_archetype];
      const Vec3 min(spawn.x - hero.size.x * 0.5f, spawn.y - hero.size.y * 0.5f, spawn.z);
      const Vec3 max(spawn.x + hero.size.x * 0.5f, spawn.y + hero.size.y * 0.5f, spawn.z + hero.size.z);
      CHECK(!box_overlaps_solid(scene.level, min, max));
    }
  }
  Scene missing;
  std::string error;
  CHECK(!load_scene(SCRAPS_SANDBOX_SCENE_DIR, "no-such-scene", missing, error));
  CHECK(error.find("cannot open") != std::string::npos);
}

}  // namespace

void sky_and_sound_lines_parse(test::Context& ctx) {
  Scene scene;
  std::string error;
  CHECK(parse_scene(std::string(test::kBoxScene) + "sky 10 20 30\n", scene, error));
  CHECK(scene.level.sky_top.r == 10 && scene.level.sky_bottom.b == 30);  // one colour: both the same
  CHECK(parse_scene(std::string(test::kBoxScene) + "sky 1 2 3 4 5 6\nsound boing art/boing.ogg volume 0.5\n", scene,
                    error));
  CHECK(scene.level.sky_top.g == 2 && scene.level.sky_bottom.g == 5);
  CHECK(scene.level.sounds.size() == 1 && scene.level.sounds[0].volume == 0.5f);
  CHECK(find_sound(scene.level, "boing") == 0);
  CHECK(!parse_scene(std::string(test::kBoxScene) + "sky 1 2\n", scene, error));
  CHECK(error.find("sky") != std::string::npos);
  CHECK(!parse_scene(std::string(test::kBoxScene) + "sound a ../secret.wav\n", scene, error));
  CHECK(!parse_scene(std::string(test::kBoxScene) + "sound a a.wav volume 9\n", scene, error));
  CHECK(!parse_scene(std::string(test::kBoxScene) + "sound a a.wav\nsound a b.wav\n", scene, error));

  // Without a sky line: the default; and sky and sounds survive a round trip.
  CHECK(parse_scene(test::kBoxScene, scene, error));
  CHECK(scene.level.sky_top.r == kDefaultSky.r && scene.level.sky_bottom.b == kDefaultSky.b);
  CHECK(parse_scene(std::string(test::kBoxScene) + "sky 1 2 3 4 5 6\nsound boing x.flac volume 2\n", scene, error));
  World world = make_world(scene, MovementRules());
  std::vector<std::uint8_t> bytes;
  ByteWriter writer(bytes);
  write_world(writer, world);
  World loaded;
  ByteReader reader(bytes.data(), bytes.size());
  CHECK(read_world(reader, loaded));
  CHECK(loaded.level.sky_bottom.b == 6 && loaded.level.sounds.size() == 1);
  CHECK(path_view(loaded.level.sounds[0].path) == "x.flac" && loaded.level.sounds[0].volume == 2.0f);
}

void register_scene_tests(test::Registry& registry) {
  registry.add("scene.sky_and_sound_lines_parse", sky_and_sound_lines_parse);
  registry.add("scene.the_fixture_scene_parses", the_fixture_scene_parses);
  registry.add("scene.errors_name_the_line_and_the_problem", errors_name_the_line_and_the_problem);
  registry.add("scene.images_textures_and_sprites_parse", images_textures_and_sprites_parse);
  registry.add("scene.hash_ignores_line_endings", hash_ignores_line_endings);
  registry.add("scene.scene_names_cannot_be_paths", scene_names_cannot_be_paths);
  registry.add("scene.bundled_scenes_load_and_build_worlds", bundled_scenes_load_and_build_worlds);
}
