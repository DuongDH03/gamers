#pragma once

#include <string>
#include <vector>

#include "core/level.h"
#include "platform/renderer2d.h"

namespace scraps {

// One loaded image of a level, as a texture plus its sprite-sheet layout.
struct SpriteTexture {
  TextureId texture = kNoTexture;
  int width = 0;   // whole image, pixels
  int height = 0;
  int frame_width = 0;
  int frame_height = 0;
  int frames = 1;
  float fps = 0.0f;
};

// Loads every image a level declares and turns it into renderer textures.
// Client side only: the server never loads pixels. Images that fail to load
// are reported and simply missing, so whatever uses them falls back to flat
// colours; art problems never stop the game.
class SpriteBank {
 public:
  SpriteBank() = default;
  ~SpriteBank();
  SpriteBank(const SpriteBank&) = delete;
  SpriteBank& operator=(const SpriteBank&) = delete;

  // Replaces what was loaded before. Returns false if any image failed;
  // `errors` then lists them, one per line.
  bool load(const Level& level, const std::string& scene_directory, Renderer2D& renderer, std::string& errors);
  void clear();

  // nullptr when the level has no such image or it failed to load.
  const SpriteTexture* find(ImageIndex image) const;
  std::size_t loaded_count() const;

  // Texture coordinates of one frame. A tiny inset keeps nearest-pixel
  // sampling from bleeding in a column of the neighbouring frame.
  static void frame_uv(const SpriteTexture& sprite, int frame, float& u0, float& v0, float& u1, float& v1);

 private:
  Renderer2D* m_renderer = nullptr;  // the renderer that owns the textures (not owned)
  std::vector<SpriteTexture> m_sprites;  // index = ImageIndex
};

}  // namespace scraps
