#pragma once

#include <filesystem>
#include <string>

namespace scraps {

// Notices when a file on disk changes, by comparing its last-modified time
// (a property of the file, not a clock read: Rule 2's "only the loop reads
// the clock" still holds). The caller decides how often to ask; twice a
// second is plenty for hot reload.
class FileWatcher {
 public:
  FileWatcher() = default;
  // `path` is UTF-8, like every path in the engine (core/file_path.h).
  explicit FileWatcher(std::string path);
  // The same for a path already converted (e.g. with std::filesystem::u8path,
  // so non-ASCII names work on Windows); path() is then its UTF-8 form.
  explicit FileWatcher(const std::filesystem::path& file);

  // True once per change: the first call after the file was modified (or
  // appeared, or disappeared). Never throws; an unreadable file counts as
  // "no change".
  bool changed();

  const std::string& path() const { return m_path; }

 private:
  bool read_time(std::filesystem::file_time_type& out) const;

  std::string m_path;
  // The same path, converted once: a check then allocates nothing (hot
  // reload of game code polls this from the frame loop).
  std::filesystem::path m_file;
  std::filesystem::file_time_type m_last_time{};
  bool m_had_file = false;
};

}  // namespace scraps
