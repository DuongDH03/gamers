#pragma once

// The window-independent half of the WebView2 backend's link to its page:
// the UiHost (what the app posts, what the page sends back) and the flow
// control of drawn frames. webview2_backend.cpp only moves bytes for them;
// tests drive them with a fake poster.

#include <array>
#include <cstddef>
#include <cstdint>

#include "backends/webview2/page_input.h"
#include "platform/ui.h"

namespace scraps::webview2 {

// Sends a view message to the page (WebView2: PostWebMessageAsJson).
class ViewPoster {
 public:
  virtual ~ViewPoster() = default;
  virtual bool post_view(const char* json, std::size_t size) = 0;
};

// The UiHost of a WebView2 page.
//   - post() keeps the last view and sends it now if the page is ready;
//     page_ready() (after every NavigationCompleted: first load, F5, Vite)
//     sends it again, since a page that has just loaded has seen nothing.
//   - from_page() queues what the page sent as a JSON object (UI actions);
//     receive() hands it over. Too long or too many: dropped and counted
//     (UiMessageList), never cut. Nothing allocates.
class PageUi final : public UiHost {
 public:
  static constexpr std::size_t kMaxView = 4096;

  explicit PageUi(ViewPoster* poster) : m_poster(poster) {}

  bool present() const override { return true; }
  bool post(const char* json, std::size_t size) override;
  void receive(UiMessageList& out) override;

  void page_ready();
  void page_gone() { m_ready = false; }
  void from_page(const char* json, std::size_t size) { m_inbox.push(json, size); }
  // Counts a message too long to even convert (dropped, like a full inbox).
  void drop_from_page() { m_inbox.push(nullptr, UiMessageList::kMaxMessageBytes + 1); }

  bool ready() const { return m_ready; }
  std::size_t views_sent() const { return m_sent; }

 private:
  ViewPoster* m_poster;
  std::array<char, kMaxView> m_last{};
  std::size_t m_last_size = 0;
  bool m_ready = false;
  std::size_t m_sent = 0;
  UiMessageList m_inbox;
};

// Which frames may go to the page, and into which slot of the shared buffer.
//
// The page draws in requestAnimationFrame and then asks for the next frame
// ("rf <generation> <acked> <w> <h>"). Native answers each request with at
// most one frame, written into slot seq % 3 and announced with a tiny text
// message. A frame is recorded and sent only to answer a request the app
// took (take_request) for it: a frame the app runs anyway, because no
// request came in time, still ticks, mixes and pumps, but draws nothing for
// the page, so the page is never sent more than it asked for. A slot is
// reused only when the page cannot still read it: frame n+1 is sent only
// once the page drew n-1 (it draws the newest frame it was told about, so it
// never goes back to an older one).
//
// A generation is one posting of the shared buffer. A new page (hello) or a
// bigger buffer starts a new one; frames and acks of another generation do
// not count, and the page's textures are sent again.
class FrameFlow {
 public:
  // The page asked for a frame (drew `request.acked`, holds
  // `request.generation`). True if this is a new request to answer.
  bool on_request(const FrameRequest& request);
  // A new posting of the buffer: returns its generation.
  std::uint32_t new_generation();
  // The page reloaded: no requests count until it says hello again.
  void page_gone();

  // True if a frame can be sent now: the page holds this generation, this
  // frame answers a request it took, and the slot for the next one is free.
  bool can_send() const;
  // The slot for frame `seq`.
  static std::uint32_t slot_for(std::uint32_t seq) { return seq % 3; }
  // A frame was sent (copied into its slot and announced): the request is answered.
  void sent(std::uint32_t seq);
  // Takes the pending request (true if there was one): the caller runs one
  // frame for it, and that frame may send.
  bool take_request();
  // The frame the caller ran is over: a later frame needs a request of its own.
  void frame_done() { m_answering = false; }

  std::uint32_t generation() const { return m_generation; }
  std::uint32_t page_generation() const { return m_page_generation; }
  std::uint32_t last_acked() const { return m_acked; }
  int page_width() const { return m_width; }
  int page_height() const { return m_height; }
  // Frames the page reported drawn, in every generation.
  std::uint64_t frames_drawn() const { return m_frames_drawn; }

 private:
  std::uint32_t m_generation = 0;
  std::uint32_t m_page_generation = 0;
  std::uint32_t m_first_sent = 0;  // in this generation; 0 = none
  std::uint32_t m_last_sent = 0;
  std::uint32_t m_acked = 0;
  bool m_requested = false;  // the page asked, and no frame has been run for it yet
  bool m_answering = false;  // the frame now running answers a request
  int m_width = 0;
  int m_height = 0;
  std::uint64_t m_frames_drawn = 0;
};

}  // namespace scraps::webview2
