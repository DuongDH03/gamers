#include "core/scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace scraps {
namespace {

std::string_view trim(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
  return text.substr(begin, end - begin);
}

std::vector<std::string_view> split(std::string_view text) {
  std::vector<std::string_view> tokens;
  std::size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
    const std::size_t start = i;
    while (i < text.size() && text[i] != ' ' && text[i] != '\t') ++i;
    if (i > start) tokens.push_back(text.substr(start, i - start));
  }
  return tokens;
}

bool to_float(std::string_view token, float& out) {
  if (token.empty()) return false;
  const std::string text(token);
  char* end = nullptr;
  const float value = std::strtof(text.c_str(), &end);
  if (end != text.c_str() + text.size() || !std::isfinite(value)) return false;
  out = value;
  return true;
}

bool to_int(std::string_view token, long min_value, long max_value, long& out) {
  if (token.empty()) return false;
  const std::string text(token);
  char* end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  if (end != text.c_str() + text.size() || value < min_value || value > max_value) return false;
  out = value;
  return true;
}

class SceneParser {
 public:
  SceneParser(std::string_view text, Scene& scene) : m_text(text), m_scene(scene) {
    m_symbol_to_tile.fill(-1);
  }

  bool run();
  const std::string& error() const { return m_error; }

 private:
  using Tokens = std::vector<std::string_view>;

  bool next_line(std::string_view& line);
  bool fail(const std::string& message);
  bool expect_count(const Tokens& tokens, std::size_t count, const char* usage);
  bool parse_color(std::string_view r, std::string_view g, std::string_view b, Color& out);
  bool parse_vec3(std::string_view x, std::string_view y, std::string_view z, Vec3& out);
  bool lookup_archetype(std::string_view name, std::uint16_t& out);
  bool lookup_image(std::string_view name, ImageIndex& out);
  bool lookup_symbol(char symbol, TileId& out);
  bool parse_layer_index(std::string_view token, int& z);
  bool reserve_entities(std::uint32_t count);
  static bool within_extent(const Vec3& p) {
    return std::fabs(p.x) <= kWorldExtent && std::fabs(p.y) <= kWorldExtent && std::fabs(p.z) <= kWorldExtent;
  }

  bool on_view(const Tokens& t);
  bool on_size(const Tokens& t);
  bool on_image(const Tokens& t);
  bool on_tile(const Tokens& t);
  bool on_archetype(const Tokens& t);
  bool on_player(const Tokens& t);
  bool on_spawn(const Tokens& t);
  bool on_entity(const Tokens& t);
  bool on_crowd(const Tokens& t);
  bool on_fill(const Tokens& t);
  bool on_layer(const Tokens& t);
  bool on_sky(const Tokens& t);
  bool on_sound(const Tokens& t);
  bool on_music(const Tokens& t);

  std::string_view m_text;
  std::size_t m_position = 0;
  int m_line = 0;
  Scene& m_scene;
  std::string m_error;
  std::array<int, 128> m_symbol_to_tile{};
  bool m_have_view = false;
  bool m_have_size = false;
  bool m_have_player = false;
  bool m_have_music = false;
  std::uint32_t m_entity_total = 0;
};

bool SceneParser::next_line(std::string_view& line) {
  if (m_position >= m_text.size()) return false;
  std::size_t end = m_text.find('\n', m_position);
  if (end == std::string_view::npos) end = m_text.size();
  line = m_text.substr(m_position, end - m_position);
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  m_position = end + 1;
  ++m_line;
  return true;
}

bool SceneParser::fail(const std::string& message) {
  m_error = "line " + std::to_string(m_line) + ": " + message;
  return false;
}

bool SceneParser::expect_count(const Tokens& tokens, std::size_t count, const char* usage) {
  if (tokens.size() != count) return fail(std::string("expected '") + usage + "'");
  return true;
}

bool SceneParser::parse_color(std::string_view r, std::string_view g, std::string_view b, Color& out) {
  long red = 0, green = 0, blue = 0;
  if (!to_int(r, 0, 255, red) || !to_int(g, 0, 255, green) || !to_int(b, 0, 255, blue)) {
    return fail("color channels must be integers from 0 to 255");
  }
  out = Color(static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green), static_cast<std::uint8_t>(blue));
  return true;
}

bool SceneParser::parse_vec3(std::string_view x, std::string_view y, std::string_view z, Vec3& out) {
  float vx = 0.0f, vy = 0.0f, vz = 0.0f;
  if (!to_float(x, vx) || !to_float(y, vy) || !to_float(z, vz)) return fail("expected three numbers");
  out = Vec3(vx, vy, vz);
  return true;
}

bool SceneParser::lookup_archetype(std::string_view name, std::uint16_t& out) {
  const int index = find_archetype(m_scene.level, name);
  if (index < 0) return fail("unknown archetype '" + std::string(name) + "' (declare it with 'archetype' first)");
  out = static_cast<std::uint16_t>(index);
  return true;
}

bool SceneParser::lookup_image(std::string_view name, ImageIndex& out) {
  const int index = find_image(m_scene.level, name);
  if (index < 0) return fail("unknown image '" + std::string(name) + "' (declare it with 'image' first)");
  out = static_cast<ImageIndex>(index);
  return true;
}

bool SceneParser::lookup_symbol(char symbol, TileId& out) {
  if (symbol == '.') {
    out = kEmptyTile;
    return true;
  }
  const unsigned char code = static_cast<unsigned char>(symbol);
  if (code >= m_symbol_to_tile.size() || m_symbol_to_tile[code] < 0) {
    return fail(std::string("unknown tile symbol '") + symbol + "' (declare it with 'tile' first)");
  }
  out = static_cast<TileId>(m_symbol_to_tile[code]);
  return true;
}

bool SceneParser::parse_layer_index(std::string_view token, int& z) {
  if (!m_have_size) return fail("'size' must come before layers");
  long value = 0;
  if (!to_int(token, 0, m_scene.level.grid.height - 1, value)) {
    return fail("layer must be from 0 to " + std::to_string(m_scene.level.grid.height - 1));
  }
  z = static_cast<int>(value);
  return true;
}

bool SceneParser::on_view(const Tokens& t) {
  if (!expect_count(t, 2, "view <top-down|side|isometric>")) return false;
  if (!parse_perspective(t[1], m_scene.level.perspective)) {
    return fail("unknown view '" + std::string(t[1]) + "' (use top-down, side or isometric)");
  }
  m_have_view = true;
  return true;
}

bool SceneParser::on_size(const Tokens& t) {
  if (!expect_count(t, 4, "size <width> <depth> <height>")) return false;
  if (m_have_size) return fail("'size' given twice");
  long w = 0, d = 0, h = 0;
  if (!to_int(t[1], 1, kMaxGridSide, w) || !to_int(t[2], 1, kMaxGridSide, d) || !to_int(t[3], 1, kMaxGridSide, h)) {
    return fail("size values must be from 1 to " + std::to_string(kMaxGridSide));
  }
  if (static_cast<std::size_t>(w) * static_cast<std::size_t>(d) * static_cast<std::size_t>(h) > kMaxGridCells) {
    return fail("grid has too many cells");
  }
  m_scene.level.grid.resize(static_cast<int>(w), static_cast<int>(d), static_cast<int>(h));
  m_have_size = true;
  return true;
}

bool SceneParser::on_image(const Tokens& t) {
  if (t.size() < 3) return fail("expected 'image <name> <path> [frames N] [size W H] [fps F]'");
  if (t[1].size() >= kNameCapacity) return fail("image name is too long");
  if (find_image(m_scene.level, t[1]) >= 0) return fail("image '" + std::string(t[1]) + "' declared twice");
  if (m_scene.level.images.size() >= kMaxImages) return fail("too many images");
  if (!is_valid_asset_path(t[2])) {
    return fail("image path must be relative to the scene, use only letters, digits, '-', '_', '.', '/' "
                "and no '..'");
  }

  ImageDef image;
  image.name = make_name(t[1]);
  image.path = make_asset_path(t[2]);
  std::size_t i = 3;
  while (i < t.size()) {
    const std::string_view key = t[i];
    const std::size_t left = t.size() - i - 1;
    if (key == "frames" && left >= 1) {
      long frames = 0;
      if (!to_int(t[i + 1], 1, kMaxImageFrames, frames)) return fail("frames must be from 1 to 256");
      image.frames = static_cast<std::uint16_t>(frames);
      i += 2;
    } else if (key == "size" && left >= 2) {
      long w = 0, h = 0;
      if (!to_int(t[i + 1], 1, 8192, w) || !to_int(t[i + 2], 1, 8192, h)) return fail("frame size must be 1 to 8192");
      image.frame_width = static_cast<std::uint16_t>(w);
      image.frame_height = static_cast<std::uint16_t>(h);
      i += 3;
    } else if (key == "fps" && left >= 1) {
      if (!to_float(t[i + 1], image.fps) || image.fps < 0.0f || image.fps > 120.0f) {
        return fail("fps must be from 0 to 120");
      }
      i += 2;
    } else {
      return fail("unexpected '" + std::string(key) + "' in image (keys: frames, size, fps)");
    }
  }
  m_scene.level.images.push_back(image);
  return true;
}

bool SceneParser::on_tile(const Tokens& t) {
  const bool textured = t.size() == 9 && t[7] == "texture";
  if (t.size() != 7 && !textured) {
    return fail("expected 'tile <symbol> <name> <solid|decor> <r> <g> <b> [texture <image>]'");
  }
  if (t[1].size() != 1 || t[1][0] < '!' || t[1][0] > '~' || t[1][0] == '.') {
    return fail("tile symbol must be one printable character other than '.'");
  }
  const char symbol = t[1][0];
  const unsigned char code = static_cast<unsigned char>(symbol);
  if (m_symbol_to_tile[code] >= 0) return fail(std::string("tile symbol '") + symbol + "' declared twice");
  if (m_scene.level.tiles.size() >= kMaxTileDefs) return fail("too many tiles");
  if (t[2].size() >= kNameCapacity) return fail("tile name is too long");

  TileDef tile;
  tile.name = make_name(t[2]);
  tile.symbol = symbol;
  if (t[3] == "solid") {
    tile.solid = true;
  } else if (t[3] == "decor") {
    tile.solid = false;
  } else {
    return fail("tile kind must be 'solid' or 'decor'");
  }
  if (!parse_color(t[4], t[5], t[6], tile.color)) return false;
  if (textured && !lookup_image(t[8], tile.image)) return false;

  m_symbol_to_tile[code] = static_cast<int>(m_scene.level.tiles.size());
  m_scene.level.tiles.push_back(tile);
  return true;
}

bool SceneParser::on_archetype(const Tokens& t) {
  if (t.size() < 2) return fail("expected 'archetype <name> [kind N] [size X Y Z] [color R G B] [speed S] [flags a,b]'");
  if (t[1].size() >= kNameCapacity) return fail("archetype name is too long");
  if (find_archetype(m_scene.level, t[1]) >= 0) return fail("archetype '" + std::string(t[1]) + "' declared twice");
  if (m_scene.level.archetypes.size() >= kMaxArchetypes) return fail("too many archetypes");

  Archetype archetype;
  archetype.name = make_name(t[1]);
  std::size_t i = 2;
  while (i < t.size()) {
    const std::string_view key = t[i];
    const std::size_t left = t.size() - i - 1;
    if (key == "kind" && left >= 1) {
      long kind = 0;
      if (!to_int(t[i + 1], 0, 65535, kind)) return fail("kind must be from 0 to 65535");
      archetype.kind = static_cast<std::uint16_t>(kind);
      i += 2;
    } else if (key == "size" && left >= 3) {
      if (!parse_vec3(t[i + 1], t[i + 2], t[i + 3], archetype.size)) return false;
      const Vec3& s = archetype.size;
      if (s.x <= 0.0f || s.y <= 0.0f || s.z <= 0.0f || s.x > 64.0f || s.y > 64.0f || s.z > 64.0f) {
        return fail("size values must be above 0 and at most 64");
      }
      i += 4;
    } else if (key == "color" && left >= 3) {
      if (!parse_color(t[i + 1], t[i + 2], t[i + 3], archetype.color)) return false;
      i += 4;
    } else if (key == "speed" && left >= 1) {
      if (!to_float(t[i + 1], archetype.move_speed) || archetype.move_speed < 0.0f || archetype.move_speed > 100.0f) {
        return fail("speed must be from 0 to 100");
      }
      i += 2;
    } else if (key == "flags" && left >= 1) {
      std::string_view list = t[i + 1];
      while (!list.empty()) {
        const std::size_t comma = list.find(',');
        const std::string_view flag = list.substr(0, comma);
        if (flag == "moves") {
          archetype.flags |= kEntityMoves;
        } else if (flag == "gravity") {
          archetype.flags |= kEntityGravity;
        } else if (flag == "collides") {
          archetype.flags |= kEntityCollides;
        } else if (flag == "bounces") {
          archetype.flags |= kEntityBounces;
        } else {
          return fail("unknown flag '" + std::string(flag) + "' (use moves, gravity, collides, bounces)");
        }
        if (comma == std::string_view::npos) break;
        list.remove_prefix(comma + 1);
      }
      i += 2;
    } else if (key == "sprite" && left >= 1) {
      if (!lookup_image(t[i + 1], archetype.image)) return false;
      i += 2;
    } else {
      return fail("unexpected '" + std::string(key) +
                  "' in archetype (keys: kind, size, color, speed, flags, sprite)");
    }
  }
  m_scene.level.archetypes.push_back(archetype);
  return true;
}

bool SceneParser::on_player(const Tokens& t) {
  if (!expect_count(t, 2, "player <archetype>")) return false;
  if (!lookup_archetype(t[1], m_scene.level.player_archetype)) return false;
  m_have_player = true;
  return true;
}

bool SceneParser::on_sky(const Tokens& t) {
  if (t.size() != 4 && t.size() != 7) return fail("expected 'sky <r> <g> <b> [<r> <g> <b>]'");
  Color top;
  if (!parse_color(t[1], t[2], t[3], top)) return false;
  Color bottom = top;
  if (t.size() == 7 && !parse_color(t[4], t[5], t[6], bottom)) return false;
  m_scene.level.sky_top = top;
  m_scene.level.sky_bottom = bottom;
  return true;
}

bool SceneParser::on_sound(const Tokens& t) {
  if (t.size() != 3 && t.size() != 5) return fail("expected 'sound <name> <path> [volume V]'");
  if (t[1].size() >= kNameCapacity) return fail("sound name is too long");
  if (find_sound(m_scene.level, t[1]) >= 0) return fail("sound '" + std::string(t[1]) + "' declared twice");
  if (m_scene.level.sounds.size() >= kMaxSounds) return fail("too many sounds");
  if (!is_valid_asset_path(t[2])) {
    return fail("sound path must be relative to the scene, use only letters, digits, '-', '_', '.', '/' "
                "and no '..'");
  }
  SoundDef sound;
  sound.name = make_name(t[1]);
  sound.path = make_asset_path(t[2]);
  if (t.size() == 5) {
    float volume = 0.0f;
    if (t[3] != "volume" || !to_float(t[4], volume) || volume < 0.0f || volume > 4.0f) {
      return fail("volume must be a number from 0 to 4");
    }
    sound.volume = volume;
  }
  m_scene.level.sounds.push_back(sound);
  return true;
}

bool SceneParser::on_music(const Tokens& t) {
  if (t.size() != 2 && t.size() != 4) return fail("expected 'music <path> [volume V]'");
  if (m_have_music) return fail("'music' declared twice (a scene has one track)");
  if (!is_valid_asset_path(t[1])) {
    return fail("music path must be relative to the scene, use only letters, digits, '-', '_', '.', '/' "
                "and no '..'");
  }
  MusicDef music;
  music.path = make_asset_path(t[1]);
  if (t.size() == 4) {
    float volume = 0.0f;
    if (t[2] != "volume" || !to_float(t[3], volume) || volume < 0.0f || volume > 4.0f) {
      return fail("volume must be a number from 0 to 4");
    }
    music.volume = volume;
  }
  m_scene.level.music = music;
  m_have_music = true;
  return true;
}

bool SceneParser::on_spawn(const Tokens& t) {
  if (!expect_count(t, 4, "spawn <x> <y> <z>")) return false;
  if (m_scene.level.spawn_points.size() >= kMaxSpawnPoints) return fail("too many spawn points");
  Vec3 position;
  if (!parse_vec3(t[1], t[2], t[3], position)) return false;
  if (!within_extent(position)) return fail("spawn point is outside the world");
  m_scene.level.spawn_points.push_back(position);
  return true;
}

bool SceneParser::on_entity(const Tokens& t) {
  if (!expect_count(t, 5, "entity <archetype> <x> <y> <z>")) return false;
  SceneEntity entity;
  if (!lookup_archetype(t[1], entity.archetype)) return false;
  if (!parse_vec3(t[2], t[3], t[4], entity.position)) return false;
  if (!within_extent(entity.position)) return fail("position is outside the world");
  if (!reserve_entities(1)) return false;
  m_scene.entities.push_back(entity);
  return true;
}

bool SceneParser::reserve_entities(std::uint32_t count) {
  if (count > kMaxSceneEntities - m_entity_total) {
    return fail("too many entities: a scene may place at most " + std::to_string(kMaxSceneEntities) +
                " (entity lines and crowds together)");
  }
  m_entity_total += count;
  return true;
}

bool SceneParser::on_crowd(const Tokens& t) {
  if (!expect_count(t, 3, "crowd <archetype> <count>")) return false;
  SceneCrowd crowd;
  if (!lookup_archetype(t[1], crowd.archetype)) return false;
  long count = 0;
  if (!to_int(t[2], 0, static_cast<long>(kMaxSceneEntities), count)) {
    return fail("crowd count must be from 0 to " + std::to_string(kMaxSceneEntities));
  }
  crowd.count = static_cast<std::uint32_t>(count);
  if (!reserve_entities(crowd.count)) return false;
  m_scene.crowds.push_back(crowd);
  return true;
}

bool SceneParser::on_fill(const Tokens& t) {
  if (!expect_count(t, 3, "fill <layer> <symbol>")) return false;
  int z = 0;
  if (!parse_layer_index(t[1], z)) return false;
  if (t[2].size() != 1) return fail("fill symbol must be one character");
  TileId id = kEmptyTile;
  if (!lookup_symbol(t[2][0], id)) return false;
  TileGrid& grid = m_scene.level.grid;
  for (int y = 0; y < grid.depth; ++y) {
    for (int x = 0; x < grid.width; ++x) grid.set(x, y, z, id);
  }
  return true;
}

bool SceneParser::on_layer(const Tokens& t) {
  if (!expect_count(t, 2, "layer <z>")) return false;
  int z = 0;
  if (!parse_layer_index(t[1], z)) return false;
  TileGrid& grid = m_scene.level.grid;
  for (int y = 0; y < grid.depth; ++y) {
    std::string_view row;
    if (!next_line(row)) {
      return fail("layer " + std::to_string(z) + " needs " + std::to_string(grid.depth) + " rows, file ended after " +
                  std::to_string(y));
    }
    row = trim(row);
    if (row.size() != static_cast<std::size_t>(grid.width)) {
      return fail("row " + std::to_string(y) + " of layer " + std::to_string(z) + " has " + std::to_string(row.size()) +
                  " characters, expected " + std::to_string(grid.width));
    }
    for (int x = 0; x < grid.width; ++x) {
      TileId id = kEmptyTile;
      if (!lookup_symbol(row[static_cast<std::size_t>(x)], id)) return false;
      grid.set(x, y, z, id);
    }
  }
  return true;
}

bool SceneParser::run() {
  TileDef empty;
  empty.name = make_name("empty");
  empty.symbol = '.';
  empty.solid = false;
  m_scene.level.tiles.push_back(empty);

  bool have_header = false;
  std::string_view line;
  while (next_line(line)) {
    const std::string_view text = trim(line);
    if (text.empty() || text[0] == '#') continue;
    const Tokens tokens = split(text);
    const std::string_view directive = tokens[0];

    if (!have_header) {
      if (directive != "scraps-scene" || tokens.size() != 2) return fail("the first line must be 'scraps-scene 1'");
      if (tokens[1] != "1") return fail("unsupported scene format version '" + std::string(tokens[1]) + "'");
      have_header = true;
      continue;
    }

    bool ok = false;
    if (directive == "view") {
      ok = on_view(tokens);
    } else if (directive == "size") {
      ok = on_size(tokens);
    } else if (directive == "image") {
      ok = on_image(tokens);
    } else if (directive == "tile") {
      ok = on_tile(tokens);
    } else if (directive == "archetype") {
      ok = on_archetype(tokens);
    } else if (directive == "player") {
      ok = on_player(tokens);
    } else if (directive == "spawn") {
      ok = on_spawn(tokens);
    } else if (directive == "entity") {
      ok = on_entity(tokens);
    } else if (directive == "crowd") {
      ok = on_crowd(tokens);
    } else if (directive == "fill") {
      ok = on_fill(tokens);
    } else if (directive == "layer") {
      ok = on_layer(tokens);
    } else if (directive == "sky") {
      ok = on_sky(tokens);
    } else if (directive == "sound") {
      ok = on_sound(tokens);
    } else if (directive == "music") {
      ok = on_music(tokens);
    } else {
      ok = fail("unknown directive '" + std::string(directive) + "'");
    }
    if (!ok) return false;
  }

  if (!have_header) return fail("empty scene (the first line must be 'scraps-scene 1')");
  if (!m_have_view) return fail("missing 'view'");
  if (!m_have_size) return fail("missing 'size'");
  if (!m_have_player) return fail("missing 'player'");
  if (m_scene.level.spawn_points.empty()) return fail("missing 'spawn' (at least one is needed)");
  return true;
}

std::string_view file_stem(std::string_view path) {
  const std::size_t slash = path.find_last_of("/\\");
  if (slash != std::string_view::npos) path.remove_prefix(slash + 1);
  const std::size_t dot = path.rfind('.');
  if (dot != std::string_view::npos && dot > 0) path = path.substr(0, dot);
  return path;
}

// A random spot on the players' floor (the height of the first spawn point):
// a column whose lowest standing place is at that height, where the
// archetype's box fits. Returns false after a few misses.
bool find_crowd_position(World& world, const Archetype& archetype, Vec3& out) {
  const TileGrid& grid = world.level.grid;
  const Vec3 spawn = spawn_point_for(world.level, 0);
  const int floor_z = static_cast<int>(std::floor(spawn.z + 0.5f));
  for (int attempt = 0; attempt < 32; ++attempt) {
    const int x = std::min(grid.width - 1, static_cast<int>(random_unit(world) * static_cast<float>(grid.width)));
    const int y = std::min(grid.depth - 1, static_cast<int>(random_unit(world) * static_cast<float>(grid.depth)));
    for (int z = 1; z <= grid.height; ++z) {
      if (!is_solid_cell(world.level, x, y, z - 1) || is_solid_cell(world.level, x, y, z)) continue;
      if (z != floor_z) break;  // this column's floor is higher or lower than the players'
      const Vec3 position(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, static_cast<float>(z));
      const Vec3 min(position.x - archetype.size.x * 0.5f, position.y - archetype.size.y * 0.5f, position.z);
      const Vec3 max(position.x + archetype.size.x * 0.5f, position.y + archetype.size.y * 0.5f,
                     position.z + archetype.size.z);
      if (!box_overlaps_solid(world.level, min, max)) {
        out = position;
        return true;
      }
      break;
    }
  }
  return false;
}

}  // namespace

std::uint32_t hash_scene_text(std::string_view text) {
  std::uint32_t hash = 2166136261u;
  for (char c : text) {
    if (c == '\r') continue;
    hash ^= static_cast<std::uint8_t>(c);
    hash *= 16777619u;
  }
  return hash;
}

bool is_valid_scene_name(std::string_view name) {
  if (name.empty() || name.size() > 31) return false;
  for (char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

bool parse_scene(std::string_view text, Scene& out, std::string& error) {
  out = Scene();
  out.name = "unnamed";
  SceneParser parser(text, out);
  if (!parser.run()) {
    error = parser.error();
    return false;
  }
  out.content_hash = hash_scene_text(text);
  return true;
}

bool load_scene_file(const std::string& path, Scene& out, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "cannot open scene file '" + path + "'";
    return false;
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  if (!parse_scene(contents.str(), out, error)) {
    error = path + ": " + error;
    return false;
  }
  out.name = std::string(file_stem(path));
  return true;
}

bool load_scene(const std::string& directory, std::string_view name, Scene& out, std::string& error) {
  if (!is_valid_scene_name(name)) {
    error = "invalid scene name '" + std::string(name) + "'";
    return false;
  }
  std::string path = directory;
  if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';
  path += std::string(name) + ".scene";
  return load_scene_file(path, out, error);
}

World make_world(const Scene& scene, const MovementRules& rules) {
  World world;
  world.rules = rules;
  world.level = scene.level;
  world.rng_state ^= (static_cast<std::uint64_t>(scene.content_hash) << 32) | scene.content_hash;

  // Reserve generously so that spawning during play does not allocate in
  // simulate (code rules: no allocation in simulate once stage 5 is done).
  std::size_t expected = scene.entities.size() + static_cast<std::size_t>(kMaxPlayers);
  for (const SceneCrowd& crowd : scene.crowds) expected += crowd.count;
  const std::size_t capacity = std::min<std::size_t>(kMaxEntities, std::max<std::size_t>(256, expected * 2));
  world.entities.reserve(capacity);
  world.free_slots.reserve(capacity);

  for (const SceneEntity& entity : scene.entities) spawn_entity(world, entity.archetype, entity.position);

  for (const SceneCrowd& crowd : scene.crowds) {
    if (crowd.archetype >= world.level.archetypes.size()) continue;
    const Archetype archetype = world.level.archetypes[crowd.archetype];
    for (std::uint32_t i = 0; i < crowd.count; ++i) {
      Vec3 position;
      if (!find_crowd_position(world, archetype, position)) continue;
      Entity* entity = find_entity(world, spawn_entity(world, crowd.archetype, position));
      if (entity == nullptr) break;
      if ((archetype.flags & kEntityMoves) != 0 && archetype.move_speed > 0.0f) {
        const float angle = random_unit(world) * 2.0f * kPi;
        entity->velocity = Vec3(std::cos(angle) * archetype.move_speed, std::sin(angle) * archetype.move_speed, 0.0f);
        if (!rules.move_x) entity->velocity.x = 0.0f;
        if (!rules.move_y) entity->velocity.y = 0.0f;
      }
    }
  }
  return world;
}

}  // namespace scraps
