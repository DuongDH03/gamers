#pragma once

// Pause for the offline scraps_client (docs/specs/2026-11-14-offline-
// essentials-and-ui.md, part 4). One place that pauses and resumes, so any
// input (P today; a gamepad's start button or a pause menu later) calls the
// same thing.
//
//   paused:  the embedded Server and the Client both stop: no ticks, no
//            snapshots, no inputs, no prediction, timeouts frozen. The loop
//            runs no ticks; frames keep drawing the last world.
//   resume:  both carry on with the next tick, and the step clock forgets
//            its leftover time, so nothing is caught up in a burst (Rule 2).
//
// Online there is no embedded server, and one player cannot stop everyone
// else's world, so pausing is refused.

#include "core/fixed_step.h"
#include "net/client.h"
#include "net/server.h"

namespace scraps_apps {

class OfflinePause {
 public:
  // Returns false, and changes nothing, online (`server` is nullptr).
  bool set(bool paused, scraps::Server* server, scraps::Client& client, scraps::FixedStepClock& clock) {
    if (server == nullptr) return false;
    if (paused == m_paused) return true;
    m_paused = paused;
    server->set_paused(paused);
    client.set_paused(paused);
    if (!paused) clock.reset();
    return true;
  }
  bool toggle(scraps::Server* server, scraps::Client& client, scraps::FixedStepClock& clock) {
    return set(!m_paused, server, client, clock);
  }
  bool paused() const { return m_paused; }

  // The ticks to run this frame: none while paused. The clock is not fed
  // paused time at all, so it has nothing to catch up on.
  int advance(scraps::FixedStepClock& clock, double frame_seconds) const {
    return m_paused ? 0 : clock.advance(frame_seconds);
  }

 private:
  bool m_paused = false;
};

}  // namespace scraps_apps
