#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/level.h"
#include "core/math.h"
#include "core/movement.h"
#include "core/world.h"
#include "platform/renderer2d.h"
#include "views/camera.h"
#include "views/sprite_bank.h"

namespace scraps {

// A way of looking at the world (Rule 4). A View owns three things:
//   1. projection: world (x, y, z) -> screen pixels, and the camera
//   2. draw order: which things are painted first
//   3. movement rules: gravity, jumping, allowed axes
//
// Core never asks "which way is down"; it asks the View's MovementRules.
//
// Drawing uses the painter's algorithm: every block and entity becomes a
// DrawItem with a depth key, items are sorted back to front, and each one is
// turned into triangles. Consecutive triangles with the same texture share
// one batch, so a frame is a handful of renderer calls, not one per object.
class View {
 public:
  virtual ~View() = default;

  virtual Perspective perspective() const = 0;
  const char* name() const { return perspective_name(perspective()); }

  virtual MovementRules movement_rules() const = 0;
  virtual float default_zoom() const = 0;

  // Projects a world point to screen pixels.
  virtual Vec2 world_to_screen(const Vec3& point, const Camera& camera) const = 0;
  // The world point under `screen` (pixels) on this view's pointer plane
  // through `through` (usually the player's feet). Top-down and isometric
  // use the plane z = through.z, the side view the plane y = through.y (the
  // lane). Which plane is the View's choice, so core never learns which way
  // is down (Rule 4). Exact inverse of world_to_screen on that plane; a
  // camera with no zoom gives `through` back.
  virtual Vec3 screen_to_world(Vec2 screen, const Camera& camera, const Vec3& through) const = 0;
  // Turns a direction on screen (x right, y down, e.g. from WASD) into the
  // world ground direction (x, y) that looks like it on screen. Returns a unit
  // vector or zero.
  virtual Vec2 screen_to_ground_direction(Vec2 screen_direction) const = 0;

  // Draws the level and every live entity. Entity positions are interpolated
  // between `previous` and `current` by `alpha` (Rule 2); pass the same world
  // twice with alpha 0 to draw it exactly as it is (Client::display_world).
  // With `sprites`, textured tiles and sprite entities are drawn with their
  // images; without, everything is flat colour. Allocation-free once its
  // buffers have grown to the size of the scene.
  void draw(const World& previous, const World& current, float alpha, const Camera& camera, Renderer2D& renderer,
            const SpriteBank* sprites = nullptr);

  // Shadows fade out completely this high above the ground (world units).
  static constexpr float kShadowFadeHeight = 4.0f;

 protected:
  enum class ItemKind : std::uint8_t { kBlock, kEntity, kShadow };

  struct DrawItem {
    float depth = 0.0f;       // painted in increasing order
    float tie = 0.0f;         // second key, for equal depth
    std::uint32_t order = 0;  // third key: insertion order keeps sorting deterministic
    ItemKind kind = ItemKind::kBlock;
    int x = 0, y = 0, z = 0;  // block cell
    std::uint32_t entity = 0; // entity slot
    Vec3 position;            // interpolated entity position; for a shadow, the point on the ground
    float radius = 0.0f;      // shadow: radius on the ground, world units
    float strength = 0.0f;    // shadow: 0..1, how dark
  };

  // A texture for one face of a block: frame 0 for the top; for sides,
  // frame 1 (or frame 2 when another block sits on top, if the image has 3).
  struct FaceTexture {
    TextureId texture = kNoTexture;
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
  };

  // Subclasses push one item per visible block, using push_block().
  virtual void collect_blocks(const Level& level, const Camera& camera) = 0;
  // Depth keys for an entity at `position`.
  virtual void entity_depth(const Vec3& position, const Entity& entity, float& depth, float& tie) const = 0;
  // Turn an item into triangles with the push_* helpers.
  virtual void emit_block(const DrawItem& item, const Level& level, const Camera& camera) = 0;
  virtual void emit_entity(const Vec3& position, const Entity& entity, const Camera& camera) = 0;
  // Depth keys for a shadow on the ground at `ground`, cast by `entity`. By
  // default the entity's own keys at that point: inserted just before the
  // entity, so it is painted right under it.
  virtual void shadow_depth(const Vec3& ground, const Entity& entity, float& depth, float& tie) const;
  // A dark ellipse: by default a circle on the ground plane, projected.
  virtual void emit_shadow(const DrawItem& item, const Camera& camera);

  void push_block(int x, int y, int z, float depth, float tie);
  void push_triangle(Vec2 a, Vec2 b, Vec2 c, Color color);
  void push_quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color color);  // corners in order around the quad
  void push_rect(float x, float y, float w, float h, Color color);
  // Corners a, b, c, d get texture corners (u0,v0) (u1,v0) (u1,v1) (u0,v1).
  void push_textured_quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color tint, const FaceTexture& face);

  // The texture for a face of block (x, y, z), or kNoTexture.
  FaceTexture face_texture(const Level& level, int x, int y, int z, bool top) const;
  // Draws the entity's sprite as an upright picture standing on `feet`
  // (screen pixels), `width` pixels wide. Animates from the world tick while
  // the entity moves (always, for things that never move), mirrored when
  // the entity's `facing` points left on this view's screen (kept after it
  // stops). Returns false if it has no sprite.
  bool push_sprite(const Entity& entity, const Vec3& position, Vec2 feet, float width, const Camera& camera);

  // Sprites are drawn a little wider than the entity's collision box, as is
  // usual in 2D games: the box is for physics, the picture is for people.
  static constexpr float kSpriteScale = 1.3f;

  // A filled ellipse around `center`, `rx` by `ry` pixels.
  void push_ellipse(Vec2 center, float rx, float ry, Color color);

  // True if the screen-space box (with a margin) touches the viewport.
  static bool on_screen(float min_x, float min_y, float max_x, float max_y, const Camera& camera);

 private:
  struct Batch {
    TextureId texture = kNoTexture;
    std::size_t first = 0;
    std::size_t count = 0;
  };
  void use_texture(TextureId texture);

  std::vector<DrawItem> m_items;
  std::vector<Vertex2D> m_vertices;
  std::vector<Batch> m_batches;

  // Valid only during draw().
  const SpriteBank* m_sprites = nullptr;
  const Level* m_level = nullptr;
  Tick m_tick = 0;
};

// Finds the ground under `position` along −z (views own "down", Rule 4):
// the top of the highest solid block at or below the feet, in the same
// column. Returns false when there is none (over a pit, or off the grid).
bool ground_below(const Level& level, const Vec3& position, float& ground_z);

// Fills the screen with the level's sky: one colour, or a blend from
// sky_top at the top to sky_bottom at the bottom. Call right after begin_frame.
void draw_sky(const Level& level, Renderer2D& renderer);

}  // namespace scraps
