#include "core/file_watch.h"

#include <system_error>
#include <utility>

namespace scraps {

FileWatcher::FileWatcher(std::string path) : m_path(std::move(path)), m_file(m_path) {
  m_had_file = read_time(m_last_time);
}

FileWatcher::FileWatcher(const std::filesystem::path& file) : m_path(file.u8string()), m_file(file) {
  m_had_file = read_time(m_last_time);
}

bool FileWatcher::read_time(std::filesystem::file_time_type& out) const {
  // The std::error_code overload reports failure through `error` instead of
  // throwing (no exceptions in core).
  std::error_code error;
  const std::filesystem::file_time_type time = std::filesystem::last_write_time(m_file, error);
  if (error) return false;
  out = time;
  return true;
}

bool FileWatcher::changed() {
  if (m_path.empty()) return false;
  std::filesystem::file_time_type time{};
  const bool has_file = read_time(time);
  if (has_file == m_had_file && (!has_file || time == m_last_time)) return false;
  m_had_file = has_file;
  if (has_file) m_last_time = time;
  return true;
}

}  // namespace scraps
