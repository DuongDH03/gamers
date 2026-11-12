#include "net/rate_limit.h"

namespace scraps {

void TokenBucket::reset(double now) {
  m_tokens = m_limit.burst;
  m_last = now;
  m_started = true;
}

bool TokenBucket::take(double now) {
  if (!m_started) reset(now);
  if (now > m_last) {
    m_tokens += (now - m_last) * m_limit.per_second;
    if (m_tokens > m_limit.burst) m_tokens = m_limit.burst;
    m_last = now;
  }
  if (m_tokens < 1.0) return false;
  m_tokens -= 1.0;
  return true;
}

bool AddressRateLimiter::allow(std::uint32_t ip, double now) {
  Entry* found = nullptr;
  Entry* oldest = &m_entries[0];
  for (Entry& entry : m_entries) {
    if (entry.used && entry.ip == ip) {
      found = &entry;
      break;
    }
    if (!entry.used) {
      if (oldest->used) oldest = &entry;  // an empty entry beats any used one
    } else if (oldest->used && entry.last_seen < oldest->last_seen) {
      oldest = &entry;
    }
  }
  if (found == nullptr) {
    found = oldest;
    found->ip = ip;
    found->used = true;
    found->bucket = TokenBucket(m_limit);
    found->bucket.reset(now);
  }
  found->last_seen = now;
  return found->bucket.take(now);
}

}  // namespace scraps
