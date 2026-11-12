#include "views/sprite_bank.h"

#include "core/image.h"

namespace scraps {

SpriteBank::~SpriteBank() { clear(); }

void SpriteBank::clear() {
  if (m_renderer != nullptr) {
    for (const SpriteTexture& sprite : m_sprites) {
      if (sprite.texture != kNoTexture) m_renderer->destroy_texture(sprite.texture);
    }
  }
  m_sprites.clear();
  m_renderer = nullptr;
}

bool SpriteBank::load(const Level& level, const std::string& scene_directory, Renderer2D& renderer,
                      std::string& errors) {
  clear();
  m_renderer = &renderer;
  errors.clear();
  m_sprites.resize(level.images.size());

  std::string folder = scene_directory;
  if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') folder += '/';

  for (std::size_t i = 0; i < level.images.size(); ++i) {
    const ImageDef& def = level.images[i];
    Image image;
    std::string error;
    if (!load_png(folder + std::string(path_view(def.path)), image, error)) {
      errors += error + "\n";
      continue;
    }
    const int frames = def.frames > 0 ? def.frames : 1;
    const int frame_width = def.frame_width > 0 ? def.frame_width : image.width / frames;
    const int frame_height = def.frame_height > 0 ? def.frame_height : image.height;
    if (frame_width <= 0 || frame_height <= 0 || frame_width * frames > image.width || frame_height > image.height) {
      errors += std::string(path_view(def.path)) + ": " + std::to_string(frames) + " frames of " +
                std::to_string(frame_width) + "x" + std::to_string(frame_height) + " do not fit in a " +
                std::to_string(image.width) + "x" + std::to_string(image.height) + " image\n";
      continue;
    }
    const TextureId texture = renderer.create_texture(image.width, image.height, image.rgba.data());
    if (texture == kNoTexture) {
      errors += std::string(path_view(def.path)) + ": the renderer could not create a texture\n";
      continue;
    }
    SpriteTexture& sprite = m_sprites[i];
    sprite.texture = texture;
    sprite.width = image.width;
    sprite.height = image.height;
    sprite.frame_width = frame_width;
    sprite.frame_height = frame_height;
    sprite.frames = frames;
    sprite.fps = def.fps;
  }
  return errors.empty();
}

const SpriteTexture* SpriteBank::find(ImageIndex image) const {
  if (image == kNoImage || image >= m_sprites.size()) return nullptr;
  const SpriteTexture& sprite = m_sprites[image];
  return sprite.texture != kNoTexture ? &sprite : nullptr;
}

std::size_t SpriteBank::loaded_count() const {
  std::size_t count = 0;
  for (const SpriteTexture& sprite : m_sprites) {
    if (sprite.texture != kNoTexture) ++count;
  }
  return count;
}

void SpriteBank::frame_uv(const SpriteTexture& sprite, int frame, float& u0, float& v0, float& u1, float& v1) {
  if (sprite.frames > 0) frame %= sprite.frames;
  if (frame < 0) frame = 0;
  const float width = static_cast<float>(sprite.width);
  const float height = static_cast<float>(sprite.height);
  const float inset = 0.01f;  // in texels
  u0 = (static_cast<float>(frame * sprite.frame_width) + inset) / width;
  u1 = (static_cast<float>((frame + 1) * sprite.frame_width) - inset) / width;
  v0 = inset / height;
  v1 = (static_cast<float>(sprite.frame_height) - inset) / height;
}

}  // namespace scraps
