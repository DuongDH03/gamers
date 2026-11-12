#pragma once

// The server's handshake-cookie secret, from the operating system's random
// source. Apps only: the engine never picks randomness on its own.

#include <cstdint>
#include <random>

#include "net/cookie.h"

namespace scraps_apps {

inline scraps::CookieSecret random_cookie_secret() {
  std::random_device device;
  auto next64 = [&device]() {
    return (static_cast<std::uint64_t>(device()) << 32) ^ static_cast<std::uint64_t>(device());
  };
  scraps::CookieSecret secret;
  secret.k0 = next64();
  secret.k1 = next64();
  return secret;
}

}  // namespace scraps_apps
