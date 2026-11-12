#pragma once

#include "core/math.h"

namespace scraps {

// What part of the world is on screen. Owned by the View layer (Rule 4).
struct Camera {
  Vec3 target;                     // world point shown at the center of the viewport
  float zoom = 40.0f;              // pixels per world unit
  float viewport_width = 1280.0f;  // pixels
  float viewport_height = 720.0f;  // pixels
};

}  // namespace scraps
