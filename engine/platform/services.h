#pragma once

#include "platform/audio.h"
#include "platform/input.h"
#include "platform/renderer2d.h"
#include "platform/window.h"

namespace scraps {

// The interfaces a backend provides to a client app. Non-owning: the backend
// object owns them and must outlive every use.
struct PlatformServices {
  Window* window = nullptr;
  Input* input = nullptr;
  Renderer2D* renderer = nullptr;
  Audio* audio = nullptr;
};

}  // namespace scraps
