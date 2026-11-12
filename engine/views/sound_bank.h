#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/level.h"
#include "core/mixer.h"

namespace scraps {

// The client's sound effects for one level: decodes every `sound` line of
// the scene (any common format, core/sound.h) into a Mixer and plays them by
// name. Presentation only: sounds never touch the World (Rule 1).
class SoundBank {
 public:
  // Clears `mixer` and loads every sound of `level` into it. A sound that
  // fails is listed in `errors` and stays silent; the rest still load.
  // Returns false if any failed.
  bool load(const Level& level, const std::string& scene_directory, Mixer& mixer, std::string& errors);

  // Plays the named sound at its scene volume times `volume`. Unknown names
  // and sounds that failed to load are silent.
  void play(std::string_view name, Mixer& mixer, float volume = 1.0f) const;

 private:
  struct Entry {
    Name name{};
    SoundId id = kNoSound;
    float volume = 1.0f;
  };
  std::vector<Entry> m_entries;
};

}  // namespace scraps
