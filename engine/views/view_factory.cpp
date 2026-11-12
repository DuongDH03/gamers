#include "views/view_factory.h"

#include "views/isometric_view.h"
#include "views/side_view.h"
#include "views/top_down_view.h"

namespace scraps {

std::unique_ptr<View> make_view(Perspective perspective) {
  switch (perspective) {
    case Perspective::kSide: return std::make_unique<SideView>();
    case Perspective::kIsometric: return std::make_unique<IsometricView>();
    case Perspective::kTopDown:
    default: return std::make_unique<TopDownView>();
  }
}

MovementRules movement_rules_for(Perspective perspective) { return make_view(perspective)->movement_rules(); }

Perspective next_perspective(Perspective perspective) {
  switch (perspective) {
    case Perspective::kTopDown: return Perspective::kSide;
    case Perspective::kSide: return Perspective::kIsometric;
    case Perspective::kIsometric:
    default: return Perspective::kTopDown;
  }
}

}  // namespace scraps
