#pragma once

#include "platform/audio.h"
#include "platform/input.h"
#include "platform/renderer2d.h"
#include "platform/ui.h"
#include "platform/window.h"

namespace scraps {

// The interfaces a backend provides to a client app. Non-owning: the backend
// object owns them and must outlive every use.
struct PlatformServices {
  Window* window = nullptr;
  Input* input = nullptr;
  Renderer2D* renderer = nullptr;
  Audio* audio = nullptr;
  // A UI layer (menus, HUD in HTML), or nullptr. Without one, or when
  // ui->present() is false, the client plays at once with the debug HUD.
  UiHost* ui = nullptr;
};

}  // namespace scraps
