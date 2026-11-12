#include "net/loopback.h"

#include <cstring>
#include <utility>

namespace scraps {

double LoopbackHub::next_roll() {
  // xorshift64: deterministic, so a lossy test fails the same way every run.
  m_random ^= m_random << 13;
  m_random ^= m_random >> 7;
  m_random ^= m_random << 17;
  return static_cast<double>(m_random >> 11) * (1.0 / 9007199254740992.0);
}

bool LoopbackHub::send(const Address& from, const Address& to, const std::uint8_t* data, std::size_t size) {
  if (size > kMaxDatagramSize) return false;

  if (m_drop_rate > 0.0) {
    if (next_roll() < m_drop_rate) {
      ++m_dropped;
      return true;  // like UDP: the sender cannot tell
    }
  }

  Packet packet;
  packet.from = from;
  packet.to = to;
  packet.deliver_at = m_now + m_latency;
  if (m_jitter > 0.0) packet.deliver_at += next_roll() * m_jitter;
  if (size > 0) packet.data.assign(data, data + size);
  m_packets.push_back(std::move(packet));
  return true;
}

std::size_t LoopbackHub::receive(const Address& at, Address& from, std::uint8_t* buffer, std::size_t capacity) {
  auto it = m_packets.begin();
  while (it != m_packets.end()) {
    if (it->to != at || it->deliver_at > m_now) {
      ++it;
      continue;
    }
    const std::size_t size = it->data.size();
    if (size == 0 || size > capacity) {
      it = m_packets.erase(it);  // empty or oversized: dropped, as with real sockets
      continue;
    }
    std::memcpy(buffer, it->data.data(), size);
    from = it->from;
    m_packets.erase(it);
    return size;
  }
  return 0;
}

}  // namespace scraps
