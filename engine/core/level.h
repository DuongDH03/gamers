#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "core/math.h"
#include "platform/color.h"

namespace scraps {

// Which View a level was authored for. Core only stores this label; views
// interpret it (Rule 4).
enum class Perspective : std::uint8_t { kTopDown = 0, kSide = 1, kIsometric = 2 };

const char* perspective_name(Perspective perspective);
bool parse_perspective(std::string_view text, Perspective& out);

// Fixed-size names keep Level plain, copyable data with no heap strings.
constexpr std::size_t kNameCapacity = 24;
using Name = std::array<char, kNameCapacity>;

Name make_name(std::string_view text);  // truncates to kNameCapacity - 1 chars
std::string_view name_view(const Name& name);

// A file path relative to the scene's folder, e.g. "art/hero.png".
constexpr std::size_t kPathCapacity = 64;
using AssetPath = std::array<char, kPathCapacity>;

AssetPath make_asset_path(std::string_view text);  // truncates to kPathCapacity - 1 chars
std::string_view path_view(const AssetPath& path);
// Relative, at most 63 characters of letters, digits, '-', '_', '.' and '/',
// no ".." and no leading '/': a scene can never point outside its folder.
bool is_valid_asset_path(std::string_view path);

using TileId = std::uint8_t;
constexpr TileId kEmptyTile = 0;
constexpr std::size_t kMaxTileDefs = 64;
constexpr std::size_t kMaxArchetypes = 64;
constexpr std::size_t kMaxSpawnPoints = 64;
constexpr std::size_t kMaxImages = 64;
constexpr int kMaxGridSide = 512;
constexpr std::size_t kMaxGridCells = std::size_t{1} << 22;

using ImageIndex = std::uint16_t;
constexpr ImageIndex kNoImage = 0xFFFF;
constexpr std::uint16_t kMaxImageFrames = 256;

// An image a scene uses: a single picture, or a sprite sheet whose frames sit
// side by side in one row. Only the path is stored: the level stays plain
// data, and only clients ever load the pixels (the server never draws).
struct ImageDef {
  Name name{};
  AssetPath path{};
  std::uint16_t frames = 1;        // frames in the row
  std::uint16_t frame_width = 0;   // pixels; 0 = image width / frames
  std::uint16_t frame_height = 0;  // pixels; 0 = image height
  float fps = 0.0f;                // animation speed while the entity moves
};

// A sound effect: decoded from any common audio file (core/sound.h).
struct SoundDef {
  Name name{};
  AssetPath path{};
  float volume = 1.0f;  // 0..4
};

constexpr std::size_t kMaxSounds = 64;

// Background music: streamed while it plays (core/music.h), looping. An
// empty path means none.
struct MusicDef {
  AssetPath path{};
  float volume = 1.0f;  // 0..4
};

constexpr Color kDefaultSky{24, 26, 34};

struct TileDef {
  Name name{};
  char symbol = '.';
  bool solid = false;  // false = decoration: drawn but walked through
  Color color;         // used when there is no texture (or it failed to load)
  // Texture for the block's faces. With two or more frames, frame 0 is the
  // top face and frame 1 the sides (e.g. grass on top, dirt on the sides).
  ImageIndex image = kNoImage;
};

// The level as a small 3D grid of unit cubes: x (width), y (depth), z (height).
// Cell (x, y, z) covers [x, x+1] x [y, y+1] x [z, z+1] in world units. One grid
// serves all three views: top-down looks along -z, side looks along +y,
// isometric looks along the (1, 1, 1) diagonal.
struct TileGrid {
  int width = 0;
  int depth = 0;
  int height = 0;
  std::vector<TileId> cells;  // index = x + y * width + z * width * depth

  bool contains(int x, int y, int z) const {
    return x >= 0 && y >= 0 && z >= 0 && x < width && y < depth && z < height;
  }
  std::size_t index(int x, int y, int z) const {
    return static_cast<std::size_t>(x) +
           static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(z) * static_cast<std::size_t>(width) * static_cast<std::size_t>(depth);
  }
  // Returns kEmptyTile outside the grid.
  TileId get(int x, int y, int z) const { return contains(x, y, z) ? cells[index(x, y, z)] : kEmptyTile; }
  // Ignored outside the grid.
  void set(int x, int y, int z, TileId id) {
    if (contains(x, y, z)) cells[index(x, y, z)] = id;
  }
  void resize(int w, int d, int h) {
    width = w;
    depth = d;
    height = h;
    cells.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(d) * static_cast<std::size_t>(h),
                 kEmptyTile);
  }
};

// A recipe for spawning an entity: the scene file declares these.
struct Archetype {
  Name name{};
  std::uint16_t kind = 0;
  std::uint32_t flags = 0;
  Vec3 size{0.8f, 0.8f, 0.8f};
  Color color{200, 200, 200};
  float move_speed = 0.0f;
  ImageIndex image = kNoImage;  // sprite; without one the entity is drawn as a box
};

// Everything about a world that does not change while it runs.
struct Level {
  Perspective perspective = Perspective::kTopDown;
  std::vector<ImageDef> images;
  std::vector<TileDef> tiles;  // tiles[0] is always the empty tile
  TileGrid grid;
  std::vector<Archetype> archetypes;
  std::uint16_t player_archetype = 0;
  std::vector<Vec3> spawn_points;
  std::vector<SoundDef> sounds;
  MusicDef music;
  // The background: one colour, or a blend from the top of the screen to the bottom.
  Color sky_top = kDefaultSky;
  Color sky_bottom = kDefaultSky;
};

// Solid means "blocks movement". Outside the grid in x or y counts as solid
// (the level has invisible walls); below or above the grid in z is open.
bool is_solid_cell(const Level& level, int x, int y, int z);

// A grid cell's coordinates (the cell may lie outside any grid).
struct CellCoord {
  int x = 0;
  int y = 0;
  int z = 0;
};

// Points farther than this from the origin on any axis have no cell: their
// coordinates would not fit an int after rounding, and no grid is that big.
constexpr float kMaxCellCoordinate = 1.0e6f;

// The cell that holds `point` (cell (x, y, z) covers [x, x+1) x [y, y+1) x
// [z, z+1)). On the z axis, a point on a cell's floor, or less than 1e-3
// below it, belongs to the cell whose floor that is: z is height (the one
// fact about z core has), so an entity's feet resting on a block top are in
// the open cell they stand in, not in the block, even after float rounding.
// This says nothing about gravity; it is a tie-break at cell boundaries.
// False, with `out` unchanged, for a non-finite point or one beyond
// kMaxCellCoordinate. The client's hover outline and a game's "place on the
// clicked cell" share this rule, so the outline shows the cell a click picks.
bool cell_at_point(const Vec3& point, CellCoord& out);

// Returns the archetype index, or -1 when there is none with that name.
int find_archetype(const Level& level, std::string_view name);
// Returns the image index, or -1 when there is none with that name.
int find_image(const Level& level, std::string_view name);
// Returns the sound index, or -1 when there is none with that name.
int find_sound(const Level& level, std::string_view name);

// Where player number `player` (re)appears. Players share spawn points round-robin.
Vec3 spawn_point_for(const Level& level, std::uint8_t player);

}  // namespace scraps
