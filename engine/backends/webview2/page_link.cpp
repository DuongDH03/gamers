#include "backends/webview2/page_link.h"

#include <cstring>

namespace scraps::webview2 {

bool PageUi::post(const char* json, std::size_t size) {
  if (json == nullptr || size == 0 || size > kMaxView) return false;
  std::memcpy(m_last.data(), json, size);
  m_last_size = size;
  if (!m_ready || m_poster == nullptr) return true;  // kept; sent by page_ready()
  if (!m_poster->post_view(m_last.data(), m_last_size)) return false;
  ++m_sent;
  return true;
}

void PageUi::receive(UiMessageList& out) {
  out.clear();
  if (m_inbox.count() == 0 && m_inbox.dropped() == 0) return;
  out.append(m_inbox);
  m_inbox.clear();
}

void PageUi::page_ready() {
  m_ready = true;
  if (m_last_size > 0 && m_poster != nullptr && m_poster->post_view(m_last.data(), m_last_size)) ++m_sent;
}

bool FrameFlow::on_request(const FrameRequest& request) {
  m_width = request.width;
  m_height = request.height;
  if (request.generation == 0 || request.generation != m_generation) return false;
  m_page_generation = request.generation;
  // An ack counts only for a frame sent in this generation and not yet acked.
  if (request.acked > m_acked && m_last_sent != 0 && request.acked >= m_first_sent && request.acked <= m_last_sent) {
    m_acked = request.acked;
    ++m_frames_drawn;
  }
  m_requested = true;
  return true;
}

std::uint32_t FrameFlow::new_generation() {
  ++m_generation;
  if (m_generation == 0) m_generation = 1;  // 0 means "none"
  m_first_sent = 0;
  m_last_sent = 0;
  m_acked = 0;
  m_requested = false;
  return m_generation;
}

void FrameFlow::page_gone() {
  m_page_generation = 0;
  m_requested = false;
}

bool FrameFlow::can_send() const {
  if (m_generation == 0 || m_page_generation != m_generation) return false;
  if (m_last_sent == 0) return true;
  // Frames sent and not drawn yet: at most one may be outstanding before
  // the next is written, so the slot written is never one the page reads.
  const std::uint32_t drawn_upto = m_acked != 0 ? m_acked : m_first_sent - 1;
  return m_last_sent - drawn_upto < 2;
}

void FrameFlow::sent(std::uint32_t seq) {
  if (m_first_sent == 0) m_first_sent = seq;
  m_last_sent = seq;
}

bool FrameFlow::take_request() {
  const bool requested = m_requested;
  m_requested = false;
  return requested;
}

}  // namespace scraps::webview2
