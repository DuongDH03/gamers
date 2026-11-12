#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace scraps {

// A rate: `per_second` on average, with bursts of up to `burst` at once.
struct RateLimit {
  double per_second = 5.0;
  double burst = 10.0;
};

// A token bucket: it refills at `per_second` up to `burst` tokens, and each
// allowed event takes one. Time comes in explicitly (nothing reads the clock).
class TokenBucket {
 public:
  TokenBucket() = default;
  explicit TokenBucket(RateLimit limit) : m_limit(limit), m_tokens(limit.burst) {}

  // Takes a token if there is one.
  bool take(double now);
  void reset(double now);

 private:
  RateLimit m_limit;
  double m_tokens = 0.0;
  double m_last = 0.0;
  bool m_started = false;
};

// One token bucket per IP address, for a fixed number of addresses: the
// least recently seen one is forgotten to make room. Memory is fixed and
// nothing is allocated per packet, however many addresses send.
class AddressRateLimiter {
 public:
  static constexpr std::size_t kAddresses = 64;

  AddressRateLimiter() = default;
  explicit AddressRateLimiter(RateLimit limit) : m_limit(limit) {}

  bool allow(std::uint32_t ip, double now);

 private:
  struct Entry {
    std::uint32_t ip = 0;
    bool used = false;
    double last_seen = 0.0;
    TokenBucket bucket;
  };

  RateLimit m_limit;
  std::array<Entry, kAddresses> m_entries{};
};

}  // namespace scraps
