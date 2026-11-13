#include "core/image.h"

#include <climits>
#include <cstring>
#include <fstream>
#include <iterator>

#include "core/file_path.h"
#include "third_party/stb/stb_image.h"

namespace scraps {

bool decode_png(const std::uint8_t* data, std::size_t size, Image& out, std::string& error) {
  if (data == nullptr || size == 0) {
    error = "empty image data";
    return false;
  }
  if (size > static_cast<std::size_t>(INT_MAX)) {
    error = "image file too large";
    return false;
  }
  int width = 0;
  int height = 0;
  int channels_in_file = 0;
  // Ask for 4 channels: stb converts grey, grey+alpha and RGB to RGBA.
  stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels_in_file, 4);
  if (pixels == nullptr) {
    error = std::string("cannot decode PNG: ") + stbi_failure_reason();
    return false;
  }
  out.width = width;
  out.height = height;
  const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
  out.rgba.assign(pixels, pixels + bytes);
  stbi_image_free(pixels);
  return true;
}

bool load_png(const std::string& path, Image& out, std::string& error) {
  std::ifstream file;
  if (!open_file_for_reading(path, file)) {
    error = "cannot open image '" + path + "'";
    return false;
  }
  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (!decode_png(bytes.data(), bytes.size(), out, error)) {
    error = path + ": " + error;
    return false;
  }
  return true;
}

}  // namespace scraps
