#include "core/fixed_step.h"

#include <cmath>

namespace scraps {

FixedStepClock::FixedStepClock(double step_seconds, int max_steps_per_frame)
    : m_step(step_seconds > 0.0 ? step_seconds : 1.0 / kTickRate),
      m_max_steps(max_steps_per_frame > 0 ? max_steps_per_frame : 1) {}

int FixedStepClock::advance(double frame_seconds) {
  if (!(frame_seconds > 0.0)) frame_seconds = 0.0;  // also rejects NaN
  m_accumulator += frame_seconds;

  int steps = 0;
  while (m_accumulator >= m_step && steps < m_max_steps) {
    m_accumulator -= m_step;
    ++steps;
  }
  if (m_accumulator >= m_step) {
    m_accumulator = std::fmod(m_accumulator, m_step);
  }
  return steps;
}

float FixedStepClock::alpha() const {
  const double a = m_accumulator / m_step;
  if (!(a > 0.0)) return 0.0f;
  // Clamp after converting: a double just below 1 can round up to 1.0f.
  const float alpha = static_cast<float>(a);
  const float largest_below_one = std::nextafter(1.0f, 0.0f);
  return alpha < largest_below_one ? alpha : largest_below_one;
}

}  // namespace scraps
