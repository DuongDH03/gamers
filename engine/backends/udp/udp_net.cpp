#include "backends/udp/udp_net.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <climits>
#include <cstring>

namespace scraps::udp {
namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
const NativeSocket kInvalidSocket = INVALID_SOCKET;
void close_socket(NativeSocket s) { closesocket(s); }
std::string last_error() { return "socket error " + std::to_string(WSAGetLastError()); }
#else
using NativeSocket = int;
const NativeSocket kInvalidSocket = -1;
void close_socket(NativeSocket s) { close(s); }
std::string last_error() { return std::string("socket error: ") + std::strerror(errno); }
#endif

NativeSocket native(std::uintptr_t handle) { return static_cast<NativeSocket>(handle); }

sockaddr_in to_sockaddr(const Address& address) {
  sockaddr_in result;
  std::memset(&result, 0, sizeof(result));
  result.sin_family = AF_INET;
  result.sin_addr.s_addr = htonl(address.ip);
  result.sin_port = htons(address.port);
  return result;
}

Address from_sockaddr(const sockaddr_in& address) {
  return Address(ntohl(address.sin_addr.s_addr), ntohs(address.sin_port));
}

}  // namespace

UdpNet::UdpNet(Passkey) : m_socket(static_cast<std::uintptr_t>(kInvalidSocket)) {}

std::unique_ptr<UdpNet> UdpNet::open(std::uint32_t bind_ip, std::uint16_t port, std::string& error) {
  auto net = std::make_unique<UdpNet>(Passkey());

#ifdef _WIN32
  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    error = "WSAStartup failed";
    return nullptr;
  }
  net->m_wsa_started = true;
#endif

  const NativeSocket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == kInvalidSocket) {
    error = "cannot create UDP socket (" + last_error() + ")";
    return nullptr;
  }
  net->m_socket = static_cast<std::uintptr_t>(s);
  net->m_valid = true;

#ifdef _WIN32
  u_long non_blocking = 1;
  if (ioctlsocket(s, FIONBIO, &non_blocking) != 0) {
    error = "cannot make socket non-blocking (" + last_error() + ")";
    return nullptr;
  }
#else
  const int flags = fcntl(s, F_GETFL, 0);
  if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) != 0) {
    error = "cannot make socket non-blocking (" + last_error() + ")";
    return nullptr;
  }
#endif

  // A snapshot of a big scene is dozens of datagrams sent at once. The OS
  // default buffers (as small as 64 KiB on Windows) would drop the tail of
  // such a burst before the next poll, so ask for 1 MiB each way. Failure is
  // not fatal: the OS may cap the size.
  const int buffer_bytes = 1 << 20;
  setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buffer_bytes), sizeof(buffer_bytes));
  setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buffer_bytes), sizeof(buffer_bytes));

  const sockaddr_in bind_address = to_sockaddr(Address(bind_ip, port));
  if (bind(s, reinterpret_cast<const sockaddr*>(&bind_address), sizeof(bind_address)) != 0) {
    error = "cannot bind UDP port " + std::to_string(port) + " (" + last_error() + ")";
    return nullptr;
  }

  sockaddr_in local;
  std::memset(&local, 0, sizeof(local));
#ifdef _WIN32
  int length = static_cast<int>(sizeof(local));
#else
  socklen_t length = sizeof(local);
#endif
  if (getsockname(s, reinterpret_cast<sockaddr*>(&local), &length) == 0) {
    net->m_local = from_sockaddr(local);
    // Bound to "any interface": report loopback so the address is usable locally.
    if (net->m_local.ip == 0) net->m_local.ip = kLocalhostIp;
  }
  return net;
}

UdpNet::~UdpNet() {
  if (m_valid) close_socket(native(m_socket));
#ifdef _WIN32
  if (m_wsa_started) WSACleanup();
#endif
}

bool UdpNet::send(const Address& to, const std::uint8_t* data, std::size_t size) {
  if (!m_valid || size > kMaxDatagramSize) return false;
  const sockaddr_in target = to_sockaddr(to);
#ifdef _WIN32
  const int sent = sendto(native(m_socket), reinterpret_cast<const char*>(data), static_cast<int>(size), 0,
                          reinterpret_cast<const sockaddr*>(&target), static_cast<int>(sizeof(target)));
  return sent == static_cast<int>(size);
#else
  const ssize_t sent = sendto(native(m_socket), data, size, 0, reinterpret_cast<const sockaddr*>(&target),
                              sizeof(target));
  return sent == static_cast<ssize_t>(size);
#endif
}

std::size_t UdpNet::receive(Address& from, std::uint8_t* buffer, std::size_t capacity) {
  if (!m_valid || capacity == 0) return 0;
  // A few retries skip errors that only concern one datagram.
  for (int attempt = 0; attempt < 16; ++attempt) {
    sockaddr_in source;
    std::memset(&source, 0, sizeof(source));
#ifdef _WIN32
    int length = static_cast<int>(sizeof(source));
    const int capped = capacity > static_cast<std::size_t>(INT_MAX) ? INT_MAX : static_cast<int>(capacity);
    const int received = recvfrom(native(m_socket), reinterpret_cast<char*>(buffer), capped, 0,
                                  reinterpret_cast<sockaddr*>(&source), &length);
    if (received == SOCKET_ERROR) {
      const int code = WSAGetLastError();
      // WSAECONNRESET: an earlier send hit a closed port (an ICMP reply), a
      // Windows quirk for UDP. WSAEMSGSIZE: oversized datagram, dropped.
      if (code == WSAECONNRESET || code == WSAEMSGSIZE) continue;
      return 0;  // WSAEWOULDBLOCK (nothing pending) or a real error
    }
#else
    socklen_t length = sizeof(source);
    const ssize_t received =
        recvfrom(native(m_socket), buffer, capacity, 0, reinterpret_cast<sockaddr*>(&source), &length);
    if (received < 0) {
      if (errno == EINTR || errno == ECONNREFUSED) continue;
      return 0;  // EAGAIN / EWOULDBLOCK (nothing pending) or a real error
    }
#endif
    from = from_sockaddr(source);
    return static_cast<std::size_t>(received);
  }
  return 0;
}

}  // namespace scraps::udp
