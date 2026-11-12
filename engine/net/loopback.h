#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "platform/net.h"

namespace scraps {

// An in-memory "network" that delivers datagrams between LoopbackNet
// endpoints in the same process. Offline play uses it to run the real server
// inside the client (Rule 5: offline is the client with the server embedded).
// Tests use its latency and loss settings to exercise the netcode.
//
// Time is given explicitly with set_now(): nothing here reads the clock.
class LoopbackHub {
 public:
  void set_now(double seconds) { m_now = seconds; }
  void set_latency(double seconds) { m_latency = seconds > 0.0 ? seconds : 0.0; }
  // Extra delay per datagram, uniformly random in [0, seconds): datagrams
  // then arrive out of order (deterministic pseudo-random).
  void set_jitter(double seconds) { m_jitter = seconds > 0.0 ? seconds : 0.0; }
  // Fraction of datagrams silently dropped, 0..1 (deterministic pseudo-random).
  void set_drop_rate(double rate) { m_drop_rate = rate < 0.0 ? 0.0 : (rate > 1.0 ? 1.0 : rate); }

  bool send(const Address& from, const Address& to, const std::uint8_t* data, std::size_t size);
  std::size_t receive(const Address& at, Address& from, std::uint8_t* buffer, std::size_t capacity);

  std::size_t in_flight() const { return m_packets.size(); }
  std::uint64_t dropped() const { return m_dropped; }

 private:
  struct Packet {
    Address from;
    Address to;
    double deliver_at = 0.0;
    std::vector<std::uint8_t> data;
  };

  double next_roll();  // [0, 1)

  std::deque<Packet> m_packets;
  double m_now = 0.0;
  double m_latency = 0.0;
  double m_drop_rate = 0.0;
  double m_jitter = 0.0;
  std::uint64_t m_random = 0x853C49E6748FEA9Bull;
  std::uint64_t m_dropped = 0;
};

// One endpoint on a LoopbackHub, at 127.0.0.1:<port> or a given address.
// The hub must outlive it.
class LoopbackNet final : public Net {
 public:
  LoopbackNet(LoopbackHub& hub, std::uint16_t port) : m_hub(hub), m_address(kLocalhostIp, port) {}
  // At any address, so tests can play several machines (rate limits are per IP).
  LoopbackNet(LoopbackHub& hub, Address address) : m_hub(hub), m_address(address) {}

  bool send(const Address& to, const std::uint8_t* data, std::size_t size) override {
    return m_hub.send(m_address, to, data, size);
  }
  std::size_t receive(Address& from, std::uint8_t* buffer, std::size_t capacity) override {
    return m_hub.receive(m_address, from, buffer, capacity);
  }
  Address local_address() const override { return m_address; }

 private:
  LoopbackHub& m_hub;
  Address m_address;
};

}  // namespace scraps
