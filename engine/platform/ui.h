#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// The UI layer's platform interface (docs/specs/2026-11-14-offline-
// essentials-and-ui.md, part 6 and 6a). A UI backend (the WebView2 one, or
// the headless one in tests) shows menus and a HUD written in HTML/JS and
// talks to the app with small JSON messages:
//
//   app  --post("{"type":"view",...}")-->  UI     when what it shows changed
//   app  <--receive(["{"type":"action",...}"])--  UI   what the player did
//
// The platform layer only moves the bytes. It does not parse JSON and knows
// nothing about screens or actions; the app does (apps/common/ui_model.h).
// Every message is one UTF-8 JSON text. Received messages are hostile data
// to whoever reads them: a page can send anything.

namespace scraps {

// Received messages in fixed storage, so polling the UI allocates nothing:
// at most kMaxMessages messages and kMaxBytes bytes in all, each at most
// kMaxMessageBytes. What does not fit is dropped and counted, never cut.
class UiMessageList {
 public:
  static constexpr std::size_t kMaxMessages = 32;
  static constexpr std::size_t kMaxMessageBytes = 2048;
  static constexpr std::size_t kMaxBytes = 16384;  // including a '\0' after each message

  // Empties the list and its dropped count.
  void clear() {
    m_count = 0;
    m_used = 0;
    m_dropped = 0;
  }

  // Appends a copy of `size` bytes. Returns false, and counts it as dropped,
  // when it is longer than kMaxMessageBytes or the list is full.
  bool push(const char* data, std::size_t size) {
    if ((data == nullptr && size > 0) || size > kMaxMessageBytes || m_count >= kMaxMessages ||
        size + 1 > kMaxBytes - m_used) {
      ++m_dropped;
      return false;
    }
    for (std::size_t i = 0; i < size; ++i) m_bytes[m_used + i] = data[i];
    m_bytes[m_used + size] = '\0';
    m_offsets[m_count] = static_cast<std::uint32_t>(m_used);
    m_lengths[m_count] = static_cast<std::uint32_t>(size);
    ++m_count;
    m_used += size + 1;
    return true;
  }

  // Appends every message of `other` in order and adds its dropped count.
  void append(const UiMessageList& other) {
    for (std::size_t i = 0; i < other.m_count; ++i) push(other.data(i), other.size(i));
    m_dropped += other.m_dropped;
  }

  std::size_t count() const { return m_count; }
  // Message `i` (below count()), followed by a '\0' that is not part of it.
  // It may contain '\0' bytes of its own: size(i) is its length.
  const char* data(std::size_t i) const { return i < m_count ? m_bytes.data() + m_offsets[i] : ""; }
  std::size_t size(std::size_t i) const { return i < m_count ? m_lengths[i] : 0; }
  // Messages that were received but did not fit.
  std::size_t dropped() const { return m_dropped; }

 private:
  std::array<char, kMaxBytes> m_bytes{};
  std::array<std::uint32_t, kMaxMessages> m_offsets{};
  std::array<std::uint32_t, kMaxMessages> m_lengths{};
  std::size_t m_count = 0;
  std::size_t m_used = 0;
  std::size_t m_dropped = 0;
};

// What a UI backend implements. One per window; the app owns none of it
// (PlatformServices::ui points at it, like the other interfaces). Called
// only from the thread that calls Window::pump_events().
class UiHost {
 public:
  virtual ~UiHost() = default;

  // True if this host shows a UI. Fixed for the host's life: an app decides
  // once, at start, whether it runs with menus (a UI) or plays at once
  // (none). The page may still be loading; see post().
  virtual bool present() const = 0;

  // Sends one JSON message (`size` bytes of UTF-8, no '\0' needed) to the
  // UI. The host copies it before returning. The app posts only when what it
  // shows changed, so the host must keep the last message and send it again
  // whenever the page (re)loads (first load, a developer reload, Vite hot
  // reload): a page that has just loaded has seen nothing. Returns false if
  // the message could not be sent or kept (no UI, out of memory).
  virtual bool post(const char* json, std::size_t size) = 0;

  // Moves every message the UI sent since the last call into `out`, in the
  // order sent; `out` is cleared first. Messages the host could not keep
  // are counted in out.dropped(). Must not allocate when nothing arrived.
  virtual void receive(UiMessageList& out) = 0;
};

}  // namespace scraps
