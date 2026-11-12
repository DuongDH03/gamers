#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace scraps {

// An IPv4 address and port, both in host byte order.
struct Address {
  std::uint32_t ip = 0;
  std::uint16_t port = 0;

  constexpr Address() = default;
  constexpr Address(std::uint32_t ip_value, std::uint16_t port_value) : ip(ip_value), port(port_value) {}
};

inline bool operator==(const Address& lhs, const Address& rhs) {
  return lhs.ip == rhs.ip && lhs.port == rhs.port;
}
inline bool operator!=(const Address& lhs, const Address& rhs) { return !(lhs == rhs); }

constexpr std::uint32_t kLocalhostIp = 0x7F000001u;  // 127.0.0.1

// Parses "a.b.c.d:port" or "localhost:port".
bool parse_address(std::string_view text, Address& out);
std::string to_string(const Address& address);

// Datagrams larger than this are never sent: it stays under typical internet MTUs.
constexpr std::size_t kMaxDatagramSize = 1200;

// Unreliable, unordered datagrams (UDP semantics). The engine builds its own
// protocol on top (engine/net). Implementations: backends/udp (real sockets)
// and net/loopback (in-memory, for offline play and tests).
class Net {
 public:
  virtual ~Net() = default;

  // Returns false if the datagram could not be handed to the OS.
  virtual bool send(const Address& to, const std::uint8_t* data, std::size_t size) = 0;
  // Copies one pending datagram into `buffer` and returns its size, or 0 when
  // nothing is pending. Never blocks.
  virtual std::size_t receive(Address& from, std::uint8_t* buffer, std::size_t capacity) = 0;
  virtual Address local_address() const = 0;
};

}  // namespace scraps
