#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace scraps {

// A decoded picture in memory: 8-bit RGBA, rows top to bottom. Pure data;
// turning it into a GPU texture is the renderer's job (Renderer2D::create_texture).
struct Image {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;  // width * height * 4 bytes
};

// Decodes PNG bytes (any colour type; always returns RGBA). Returns false and
// sets `error` on malformed data or images larger than 8192 pixels a side.
bool decode_png(const std::uint8_t* data, std::size_t size, Image& out, std::string& error);

// Reads a .png file and decodes it.
bool load_png(const std::string& path, Image& out, std::string& error);

}  // namespace scraps
