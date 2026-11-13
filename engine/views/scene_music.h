#pragma once

#include <string>

#include "core/level.h"
#include "core/mixer.h"

namespace scraps {

// The client's background music for the running scene: the scene's `music`
// line (docs/scene-format.md) streamed through the Mixer. Presentation only:
// music never touches the World (Rule 1).
class SceneMusic {
 public:
  // Fade times, in seconds: the first track fades in, a different track
  // crossfades with the old one, and a scene without music fades it out.
  static constexpr double kFadeInSeconds = 1.0;
  static constexpr double kCrossfadeSeconds = 1.5;
  static constexpr double kFadeOutSeconds = 1.0;

  // Call whenever a new world arrives (first connection or a hot reload).
  // Same file as now: it keeps playing (only its volume follows the scene).
  // Another file: it is opened and crossfades in. None: the music fades
  // out. A file that fails is reported in `error` and stays silent (what
  // played before fades out); returns false then.
  bool update(const Level& level, const std::string& scene_directory, Mixer& mixer, std::string& error);

  // The scene path of the track that is playing, or empty.
  const std::string& path() const { return m_path; }

 private:
  std::string m_path;
};

}  // namespace scraps
