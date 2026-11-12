#include "net/cookie.h"

namespace scraps {
namespace {

std::uint64_t rotl(std::uint64_t x, int b) { return (x << b) | (x >> (64 - b)); }

struct SipState {
  std::uint64_t v0, v1, v2, v3;

  void round() {
    v0 += v1;
    v1 = rotl(v1, 13);
    v1 ^= v0;
    v0 = rotl(v0, 32);
    v2 += v3;
    v3 = rotl(v3, 16);
    v3 ^= v2;
    v0 += v3;
    v3 = rotl(v3, 21);
    v3 ^= v0;
    v2 += v1;
    v1 = rotl(v1, 17);
    v1 ^= v2;
    v2 = rotl(v2, 32);
  }
};

std::uint64_t load_le64(const std::uint8_t* p) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  return value;
}

}  // namespace

std::uint64_t siphash24(std::uint64_t k0, std::uint64_t k1, const std::uint8_t* data, std::size_t size) {
  SipState s{k0 ^ 0x736f6d6570736575ull, k1 ^ 0x646f72616e646f6dull, k0 ^ 0x6c7967656e657261ull,
             k1 ^ 0x7465646279746573ull};
  const std::size_t full = size - size % 8;
  for (std::size_t i = 0; i < full; i += 8) {
    const std::uint64_t m = load_le64(data + i);
    s.v3 ^= m;
    s.round();
    s.round();
    s.v0 ^= m;
  }
  std::uint64_t last = static_cast<std::uint64_t>(size & 0xFF) << 56;
  for (std::size_t i = 0; i < size % 8; ++i) last |= static_cast<std::uint64_t>(data[full + i]) << (8 * i);
  s.v3 ^= last;
  s.round();
  s.round();
  s.v0 ^= last;
  s.v2 ^= 0xFF;
  for (int i = 0; i < 4; ++i) s.round();
  return s.v0 ^ s.v1 ^ s.v2 ^ s.v3;
}

std::uint64_t make_cookie(const CookieSecret& secret, const Address& address, std::uint32_t window) {
  const std::uint8_t message[10] = {
      static_cast<std::uint8_t>(address.ip),         static_cast<std::uint8_t>(address.ip >> 8),
      static_cast<std::uint8_t>(address.ip >> 16),   static_cast<std::uint8_t>(address.ip >> 24),
      static_cast<std::uint8_t>(address.port),       static_cast<std::uint8_t>(address.port >> 8),
      static_cast<std::uint8_t>(window),             static_cast<std::uint8_t>(window >> 8),
      static_cast<std::uint8_t>(window >> 16),       static_cast<std::uint8_t>(window >> 24)};
  const std::uint64_t cookie = siphash24(secret.k0, secret.k1, message, sizeof(message));
  return cookie != 0 ? cookie : 1;
}

std::uint64_t make_session_token(const CookieSecret& secret, const Address& address) {
  // A different key from the cookies' (the same secret, other bits flipped),
  // so a session token can never be mistaken for a cookie or the reverse.
  const std::uint8_t message[6] = {
      static_cast<std::uint8_t>(address.ip),       static_cast<std::uint8_t>(address.ip >> 8),
      static_cast<std::uint8_t>(address.ip >> 16), static_cast<std::uint8_t>(address.ip >> 24),
      static_cast<std::uint8_t>(address.port),     static_cast<std::uint8_t>(address.port >> 8)};
  const std::uint64_t token =
      siphash24(secret.k0 ^ 0x53455353494F4E31ull, secret.k1 ^ 0x746F6B656E2D6B31ull, message, sizeof(message));
  return token != 0 ? token : 1;
}

std::uint32_t cookie_window(double now) {
  if (now <= 0.0) return 0;
  return static_cast<std::uint32_t>(static_cast<std::uint64_t>(now / kCookieWindowSeconds));
}

}  // namespace scraps
