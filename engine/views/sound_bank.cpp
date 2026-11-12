#include "views/sound_bank.h"

#include <utility>

#include "core/sound.h"

namespace scraps {

bool SoundBank::load(const Level& level, const std::string& scene_directory, Mixer& mixer, std::string& errors) {
  mixer.clear();
  m_entries.clear();
  errors.clear();

  std::string folder = scene_directory;
  if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') folder += '/';

  for (const SoundDef& def : level.sounds) {
    Entry entry;
    entry.name = def.name;
    entry.volume = def.volume;
    SoundData sound;
    std::string error;
    if (load_sound(folder + std::string(path_view(def.path)), sound, error)) {
      entry.id = mixer.add(std::move(sound));
    } else {
      errors += error + "\n";
    }
    m_entries.push_back(entry);
  }
  return errors.empty();
}

void SoundBank::play(std::string_view name, Mixer& mixer, float volume) const {
  for (const Entry& entry : m_entries) {
    if (name_view(entry.name) == name) {
      if (entry.id != kNoSound) mixer.play(entry.id, entry.volume * volume);
      return;
    }
  }
}

}  // namespace scraps
