#pragma once

namespace scraps {

struct WindowDesc {
  const char* title = "Scraps";
  int width = 1280;
  int height = 720;
  bool vsync = true;
};

class Window {
 public:
  virtual ~Window() = default;

  // Processes pending OS events and updates the Input state. Call once per frame.
  virtual void pump_events() = 0;
  virtual bool close_requested() const = 0;
  virtual void set_title(const char* title) = 0;
};

}  // namespace scraps
