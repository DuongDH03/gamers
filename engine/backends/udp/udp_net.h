#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "platform/net.h"

namespace scraps::udp {

// Net over real UDP sockets (Winsock on Windows, BSD sockets elsewhere).
// Non-blocking, IPv4 only.
class UdpNet final : public Net {
  struct Passkey {
    explicit Passkey() = default;
  };

 public:
  // Binds to `bind_ip:port`. Use port 0 for "any free port" (clients) and
  // bind_ip 0 for all interfaces (a server reachable on the LAN), or
  // kLocalhostIp to stay on this machine. Returns nullptr and sets `error`
  // on failure.
  static std::unique_ptr<UdpNet> open(std::uint32_t bind_ip, std::uint16_t port, std::string& error);

  explicit UdpNet(Passkey);
  ~UdpNet() override;
  UdpNet(const UdpNet&) = delete;
  UdpNet& operator=(const UdpNet&) = delete;

  bool send(const Address& to, const std::uint8_t* data, std::size_t size) override;
  std::size_t receive(Address& from, std::uint8_t* buffer, std::size_t capacity) override;
  Address local_address() const override { return m_local; }

 private:
  std::uintptr_t m_socket;  // SOCKET on Windows, int elsewhere
  bool m_valid = false;
  bool m_wsa_started = false;
  Address m_local;
};

}  // namespace scraps::udp
