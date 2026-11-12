#include "views/view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace scraps {

void View::draw(const World& previous, const World& current, float alpha, const Camera& camera,
                Renderer2D& renderer, const SpriteBank* sprites) {
  m_items.clear();
  m_vertices.clear();
  m_batches.clear();
  m_sprites = sprites;
  m_level = &current.level;
  m_tick = current.tick;

  collect_blocks(current.level, camera);

  const float margin = camera.zoom * 2.0f;
  for (std::size_t i = 0; i < current.entities.size(); ++i) {
    const Entity& entity = current.entities[i];
    if (!entity.alive) continue;

    // Interpolate only if the same entity (same generation) existed last tick.
    Vec3 position = entity.position;
    if (i < previous.entities.size()) {
      const Entity& before = previous.entities[i];
      if (before.alive && before.generation == entity.generation) {
        position = lerp(before.position, entity.position, alpha);
      }
    }

    const Vec2 screen = world_to_screen(position, camera);
    if (!on_screen(screen.x - margin, screen.y - margin, screen.x + margin, screen.y + margin, camera)) continue;

    // A shadow on the ground below: the top of the highest solid block under
    // the entity's feet, smaller and fainter the higher the entity is.
    float ground_z = 0.0f;
    if (ground_below(current.level, position, ground_z)) {
      const float t = (position.z - ground_z) / kShadowFadeHeight;
      if (t < 1.0f) {
        DrawItem shadow;
        shadow.kind = ItemKind::kShadow;
        shadow.position = Vec3(position.x, position.y, ground_z);
        shadow_depth(shadow.position, entity, shadow.depth, shadow.tie);
        shadow.order = static_cast<std::uint32_t>(m_items.size());
        shadow.radius = std::max(entity.size.x, entity.size.y) * 0.5f * (1.0f - 0.5f * t);
        shadow.strength = 1.0f - t;
        m_items.push_back(shadow);
      }
    }

    DrawItem item;
    entity_depth(position, entity, item.depth, item.tie);
    item.order = static_cast<std::uint32_t>(m_items.size());
    item.kind = ItemKind::kEntity;
    item.entity = static_cast<std::uint32_t>(i);
    item.position = position;
    m_items.push_back(item);
  }

  // How many batches a frame needs depends on how textures interleave in
  // depth order, which changes as things move. Each item starts at most two
  // (its texture, then a flat overlay such as a shadow), so reserving that
  // keeps the batch list from reallocating mid-game.
  if (m_batches.capacity() < 2 * m_items.size() + 1) m_batches.reserve(2 * m_items.size() + 1);

  // std::sort works in place: no allocation, unlike std::stable_sort.
  std::sort(m_items.begin(), m_items.end(), [](const DrawItem& a, const DrawItem& b) {
    if (a.depth != b.depth) return a.depth < b.depth;
    if (a.tie != b.tie) return a.tie < b.tie;
    return a.order < b.order;
  });

  for (const DrawItem& item : m_items) {
    switch (item.kind) {
      case ItemKind::kEntity: emit_entity(item.position, current.entities[item.entity], camera); break;
      case ItemKind::kShadow: emit_shadow(item, camera); break;
      case ItemKind::kBlock: emit_block(item, current.level, camera); break;
    }
  }

  for (const Batch& batch : m_batches) {
    if (batch.count > 0) renderer.fill_triangles(m_vertices.data() + batch.first, batch.count, batch.texture);
  }
  m_sprites = nullptr;
  m_level = nullptr;
}

void View::push_block(int x, int y, int z, float depth, float tie) {
  DrawItem item;
  item.depth = depth;
  item.tie = tie;
  item.order = static_cast<std::uint32_t>(m_items.size());
  item.kind = ItemKind::kBlock;
  item.x = x;
  item.y = y;
  item.z = z;
  m_items.push_back(item);
}

void View::use_texture(TextureId texture) {
  // Painter's order must be kept, so a batch can only continue while the
  // texture stays the same.
  if (m_batches.empty() || m_batches.back().texture != texture) {
    Batch batch;
    batch.texture = texture;
    batch.first = m_vertices.size();
    m_batches.push_back(batch);
  }
}

void View::push_triangle(Vec2 a, Vec2 b, Vec2 c, Color color) {
  use_texture(kNoTexture);
  m_vertices.push_back(Vertex2D(a.x, a.y, color));
  m_vertices.push_back(Vertex2D(b.x, b.y, color));
  m_vertices.push_back(Vertex2D(c.x, c.y, color));
  m_batches.back().count += 3;
}

void View::push_quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color color) {
  push_triangle(a, b, c, color);
  push_triangle(a, c, d, color);
}

void View::push_rect(float x, float y, float w, float h, Color color) {
  push_quad(Vec2(x, y), Vec2(x + w, y), Vec2(x + w, y + h), Vec2(x, y + h), color);
}

void View::push_textured_quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color tint, const FaceTexture& face) {
  use_texture(face.texture);
  const Vertex2D va(a.x, a.y, tint, face.u0, face.v0);
  const Vertex2D vb(b.x, b.y, tint, face.u1, face.v0);
  const Vertex2D vc(c.x, c.y, tint, face.u1, face.v1);
  const Vertex2D vd(d.x, d.y, tint, face.u0, face.v1);
  m_vertices.push_back(va);
  m_vertices.push_back(vb);
  m_vertices.push_back(vc);
  m_vertices.push_back(va);
  m_vertices.push_back(vc);
  m_vertices.push_back(vd);
  m_batches.back().count += 6;
}

View::FaceTexture View::face_texture(const Level& level, int x, int y, int z, bool top) const {
  FaceTexture face;
  if (m_sprites == nullptr) return face;
  const TileId id = level.grid.get(x, y, z);
  if (id >= level.tiles.size()) return face;
  const SpriteTexture* sprite = m_sprites->find(level.tiles[id].image);
  if (sprite == nullptr) return face;

  int frame = 0;
  if (!top && sprite->frames >= 2) {
    const bool covered = level.grid.get(x, y, z + 1) != kEmptyTile;
    frame = (covered && sprite->frames >= 3) ? 2 : 1;
  }
  face.texture = sprite->texture;
  SpriteBank::frame_uv(*sprite, frame, face.u0, face.v0, face.u1, face.v1);
  return face;
}

bool View::push_sprite(const Entity& entity, const Vec3& position, Vec2 feet, float width, const Camera& camera) {
  if (m_sprites == nullptr || m_level == nullptr || entity.archetype >= m_level->archetypes.size()) return false;
  const SpriteTexture* sprite = m_sprites->find(m_level->archetypes[entity.archetype].image);
  if (sprite == nullptr) return false;

  // Animation comes from the world tick, so every machine shows the same frame.
  const bool can_move = (entity.flags & kEntityMoves) != 0;
  const bool moving = std::fabs(entity.velocity.x) + std::fabs(entity.velocity.y) > 0.05f;
  int frame = 0;
  if ((moving || !can_move) && sprite->fps > 0.0f && sprite->frames > 1) {
    frame = static_cast<int>(static_cast<float>(m_tick) * sprite->fps / static_cast<float>(kTickRate)) % sprite->frames;
  }

  FaceTexture face;
  face.texture = sprite->texture;
  SpriteBank::frame_uv(*sprite, frame, face.u0, face.v0, face.u1, face.v1);

  // Face the way the entity last moved (kept when it stops), as it looks on
  // this view's screen: the World only knows a ground direction (Rule 4).
  const Vec3 ahead = position + Vec3(entity.facing.x, entity.facing.y, 0.0f) * 0.1f;
  if (world_to_screen(ahead, camera).x < world_to_screen(position, camera).x - 0.01f) std::swap(face.u0, face.u1);

  const float height = width * static_cast<float>(sprite->frame_height) / static_cast<float>(sprite->frame_width);
  const float left = feet.x - width * 0.5f;
  const float top = feet.y - height;
  push_textured_quad(Vec2(left, top), Vec2(left + width, top), Vec2(left + width, feet.y), Vec2(left, feet.y),
                     Color(255, 255, 255), face);
  return true;
}

void View::shadow_depth(const Vec3& ground, const Entity& entity, float& depth, float& tie) const {
  entity_depth(ground, entity, depth, tie);
}

namespace {

// Twelve segments are plenty for something as soft as a shadow. The unit
// circle is computed once (a thread-safe local static), not per entity.
constexpr int kShadowSegments = 12;

const std::array<Vec2, kShadowSegments + 1>& unit_circle() {
  static const std::array<Vec2, kShadowSegments + 1> circle = [] {
    std::array<Vec2, kShadowSegments + 1> points{};
    for (int i = 0; i <= kShadowSegments; ++i) {
      const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(kShadowSegments);
      points[static_cast<std::size_t>(i)] = Vec2(std::cos(angle), std::sin(angle));
    }
    return points;
  }();
  return circle;
}

}  // namespace

void View::emit_shadow(const DrawItem& item, const Camera& camera) {
  // A circle on the ground plane, projected: round from above, a 2:1
  // ellipse in isometric.
  const std::array<Vec2, kShadowSegments + 1>& circle = unit_circle();
  const Vec2 center = world_to_screen(item.position, camera);
  const Color color(0, 0, 0, static_cast<std::uint8_t>(90.0f * item.strength));
  Vec2 last = world_to_screen(item.position + Vec3(item.radius, 0.0f, 0.0f), camera);
  for (std::size_t i = 1; i < circle.size(); ++i) {
    const Vec2 point =
        world_to_screen(item.position + Vec3(circle[i].x * item.radius, circle[i].y * item.radius, 0.0f), camera);
    push_triangle(center, last, point, color);
    last = point;
  }
}

void View::push_ellipse(Vec2 center, float rx, float ry, Color color) {
  const std::array<Vec2, kShadowSegments + 1>& circle = unit_circle();
  Vec2 last(center.x + rx, center.y);
  for (std::size_t i = 1; i < circle.size(); ++i) {
    const Vec2 point(center.x + circle[i].x * rx, center.y + circle[i].y * ry);
    push_triangle(center, last, point, color);
    last = point;
  }
}

bool ground_below(const Level& level, const Vec3& position, float& ground_z) {
  const TileGrid& grid = level.grid;
  const int x = static_cast<int>(std::floor(position.x));
  const int y = static_cast<int>(std::floor(position.y));
  if (x < 0 || y < 0 || x >= grid.width || y >= grid.depth) return false;
  // The cell the feet are in, or the one below when standing exactly on a top.
  int z = static_cast<int>(std::floor(position.z - 0.001f));
  if (z >= grid.height) z = grid.height - 1;
  for (; z >= 0; --z) {
    if (is_solid_cell(level, x, y, z)) {
      ground_z = static_cast<float>(z + 1);
      return true;
    }
  }
  return false;
}

void draw_sky(const Level& level, Renderer2D& renderer) {
  const float w = static_cast<float>(renderer.output_width());
  const float h = static_cast<float>(renderer.output_height());
  const Vertex2D quad[6] = {Vertex2D(0.0f, 0.0f, level.sky_top),   Vertex2D(w, 0.0f, level.sky_top),
                            Vertex2D(w, h, level.sky_bottom),      Vertex2D(0.0f, 0.0f, level.sky_top),
                            Vertex2D(w, h, level.sky_bottom),      Vertex2D(0.0f, h, level.sky_bottom)};
  renderer.fill_triangles(quad, 6, kNoTexture);
}

bool View::on_screen(float min_x, float min_y, float max_x, float max_y, const Camera& camera) {
  return max_x >= 0.0f && max_y >= 0.0f && min_x <= camera.viewport_width && min_y <= camera.viewport_height;
}

}  // namespace scraps
