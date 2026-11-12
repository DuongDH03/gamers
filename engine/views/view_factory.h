#pragma once

#include <memory>

#include "core/level.h"
#include "core/movement.h"
#include "views/view.h"

namespace scraps {

std::unique_ptr<View> make_view(Perspective perspective);

// The movement rules a scene gets from its perspective. The server uses this
// when it builds the world; clients receive the rules from the server.
MovementRules movement_rules_for(Perspective perspective);

// top-down -> side -> isometric -> top-down
Perspective next_perspective(Perspective perspective);

}  // namespace scraps
