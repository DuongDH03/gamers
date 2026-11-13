#include "views/scene_music.h"

#include <utility>

#include "core/music.h"

namespace scraps {

bool SceneMusic::update(const Level& level, const std::string& scene_directory, Mixer& mixer, std::string& error) {
  error.clear();
  const std::string path(path_view(level.music.path));
  if (!path.empty() && path == m_path && mixer.music_playing()) {
    mixer.set_music_track_volume(level.music.volume);  // a hot reload: the music goes on
    return true;
  }
  if (path.empty()) {
    if (!m_path.empty()) mixer.stop_music(kFadeOutSeconds);
    m_path.clear();
    return true;
  }

  std::string folder = scene_directory;
  if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') folder += '/';
  MusicStream stream;
  if (!open_music(folder + path, stream, error)) {
    mixer.stop_music(kFadeOutSeconds);
    m_path.clear();
    return false;
  }
  const double fade = mixer.music_playing() ? kCrossfadeSeconds : kFadeInSeconds;
  mixer.play_music(std::move(stream), level.music.volume, fade);
  m_path = path;
  return true;
}

}  // namespace scraps
