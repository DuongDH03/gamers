#pragma once

#include "core/tick.h"

namespace scraps {

// Turns variable frame times into a whole number of fixed simulation steps
// (Rule 2). The loop calls advance() once per rendered frame with the real
// time that passed, runs that many ticks, then renders with alpha().
//
//   frame time  ──►  accumulator  ──►  N ticks of exactly step_seconds
//                         └── leftover / step = alpha (for interpolation)
class FixedStepClock {
 public:
  explicit FixedStepClock(double step_seconds = 1.0 / kTickRate, int max_steps_per_frame = 8);

  // Adds `frame_seconds` of real time and returns how many ticks to run now.
  // Never returns more than max_steps_per_frame: after a long stall (debugger,
  // window drag) the backlog is dropped so the game slows down instead of
  // freezing in a "spiral of death".
  int advance(double frame_seconds);

  // Forgets the time not yet turned into ticks. Called when the game resumes
  // from a pause, so the paused time is never caught up in a burst of ticks
  // (Rule 2: a pause stops ticks, it never adds faster or slower ones).
  void reset() { m_accumulator = 0.0; }

  // How far the render time is between the last tick and the next, in [0, 1).
  float alpha() const;
  double step_seconds() const { return m_step; }

 private:
  double m_step;
  int m_max_steps;
  double m_accumulator = 0.0;
};

}  // namespace scraps
