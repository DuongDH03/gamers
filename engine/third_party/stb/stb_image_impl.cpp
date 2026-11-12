// The one place stb_image's implementation is compiled. PNG only; we read
// files ourselves (engine/core/image.cpp) and hand stb the bytes, so it needs
// no stdio. Compiled in its own target without our warning flags, because it
// is third-party code (engine/third_party/README.md).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 8192  // refuse absurd images instead of allocating gigabytes
#include "third_party/stb/stb_image.h"
