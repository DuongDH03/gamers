#pragma once

#include <cstddef>
#include <cstdint>

#include "platform/net.h"

namespace scraps {

// The server's secret for handshake cookies: 128 random bits chosen by the
// app at start (tests use a fixed one). Never sent anywhere.
struct CookieSecret {
  std::uint64_t k0 = 0;
  std::uint64_t k1 = 0;
};

// How long one cookie time window lasts. A cookie is accepted in its own
// window and the next one, so it lives 2 to 4 seconds.
constexpr double kCookieWindowSeconds = 2.0;

// SipHash-2-4: a fast keyed hash for short inputs, made for exactly this
// (nobody without the key can predict or forge its output).
std::uint64_t siphash24(std::uint64_t k0, std::uint64_t k1, const std::uint8_t* data, std::size_t size);

// The cookie for one address in one time window. Never 0 (0 means "no cookie").
std::uint64_t make_cookie(const CookieSecret& secret, const Address& address, std::uint32_t window);

// A player's session token: sent once, in the Welcome, and echoed in every
// Input and Bye. Only the address that received the Welcome knows it, so an
// attacker who fakes a player's address cannot act as that player, end their
// session, or keep a vanished player's slot (and its snapshots) alive.
// Never 0.
std::uint64_t make_session_token(const CookieSecret& secret, const Address& address);

// True if `secret` was set (a zero secret makes every cookie forgeable).
inline bool is_set(const CookieSecret& secret) { return secret.k0 != 0 || secret.k1 != 0; }

// The time window that `now` (seconds) falls in.
std::uint32_t cookie_window(double now);

}  // namespace scraps
