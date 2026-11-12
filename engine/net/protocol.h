#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/command.h"
#include "core/movement.h"
#include "core/serialize.h"
#include "core/tick.h"

// The wire protocol between client and server (Rule 5). Every datagram starts
// with a 4-byte magic and a 1-byte message type.
//
//   client                                server
//     │── Hello(version, no cookie) ───────►│  padded to kMinHelloSize
//     │◄──────────── Challenge(cookie)       │  keeps nothing (net/cookie.h)
//     │── Hello(version, cookie) ──────────►│  repeated until welcomed
//     │◄──────────── Welcome(player, session token, scene, rules)
//     │── Input(acked snapshot, last few ticks of commands) ►│  every tick;
//     │                                      │  redundant, so a lost packet is
//     │                                      │  covered by the next
//     │◄─ SnapshotFragment × N (world state, ack of the last input applied)
//     │── Bye ──────────────────────────────►│
//
// A snapshot's payload is `u32 baseline_snapshot` followed by either the full
// world state (baseline 0, core write_world_state) or the changes since the
// snapshot with that id (core write_world_delta). The baseline is the newest
// snapshot the client has said, in an Input, that it holds.
namespace scraps {

constexpr std::uint32_t kProtocolMagic = 0x50524353u;  // "SCRP" in little-endian bytes
constexpr std::uint16_t kProtocolVersion = 4;  // 4: challenge cookie in the handshake

constexpr std::size_t kMaxCommandsPerInputFrame = 6;
constexpr std::size_t kMaxInputFramesPerPacket = 8;
constexpr std::size_t kFragmentPayloadSize = 1024;
constexpr std::size_t kMaxFragments = 512;
constexpr std::size_t kMaxSnapshotSize = kFragmentPayloadSize * kMaxFragments;  // 512 KiB
constexpr std::size_t kMaxSceneNameLength = 31;
// A Hello must be at least this big (zero padding), so the server never
// answers a stranger with more bytes than it received: a faked source
// address cannot turn the server into an amplifier.
constexpr std::size_t kMinHelloSize = 32;
// How many recent states server and client keep as delta baselines (~0.5 s
// at 60 Hz). A client whose acked snapshot is older gets a full snapshot.
constexpr std::size_t kSnapshotWindow = 32;
constexpr std::uint32_t kFullSnapshot = 0;  // baseline id meaning "no baseline" (ids start at 1)

enum class MessageType : std::uint8_t {
  kHello = 1,
  kWelcome = 2,
  kReject = 3,
  kInput = 4,
  kSnapshotFragment = 5,
  kBye = 6,
  kChallenge = 7,
};

enum class RejectReason : std::uint8_t {
  kServerFull = 1,
  kVersionMismatch = 2,
};

const char* reject_reason_text(RejectReason reason);

// The commands a client produced in one of its ticks. Sequence numbers start
// at 1 and grow by one per client tick.
struct InputFrame {
  std::uint32_t sequence = 0;
  CommandList commands;
};

struct WelcomeMessage {
  PlayerId player = kNoPlayer;
  std::uint64_t session = 0;  // echoed in every Input and Bye (net/cookie.h)
  Tick server_tick = 0;
  std::uint32_t scene_hash = 0;
  std::string scene_name;
  MovementRules rules;  // the server's rules are the rules (server authority)
};

struct InputMessage {
  std::uint64_t session = 0;                     // from the Welcome
  std::uint32_t acked_snapshot = kFullSnapshot;  // newest snapshot the client holds
  std::array<InputFrame, kMaxInputFramesPerPacket> frames{};
  std::size_t count = 0;
};

// A snapshot (serialized world state) is split into fragments that each fit
// in one datagram. Every fragment carries the snapshot id, its position, the
// sequence of the receiving client's last applied input, and the hash of the
// scene the state belongs to (so a client notices a hot reload even if it
// missed the Welcome that announced it).
struct FragmentHeader {
  std::uint32_t snapshot_id = 0;
  std::uint16_t index = 0;
  std::uint16_t count = 0;
  std::uint32_t ack_sequence = 0;
  std::uint32_t scene_hash = 0;
};

// Writers replace the contents of `out` with one complete datagram.
// `cookie` is 0 until the server has sent a Challenge.
void write_hello(std::vector<std::uint8_t>& out, std::uint64_t cookie);
void write_challenge(std::vector<std::uint8_t>& out, std::uint64_t cookie);
void write_welcome(std::vector<std::uint8_t>& out, const WelcomeMessage& message);
void write_reject(std::vector<std::uint8_t>& out, RejectReason reason);
// At most kMaxInputFramesPerPacket frames, each with at most
// kMaxCommandsPerInputFrame commands (extra ones are not written).
// Input and Bye start with the session token from the Welcome.
void write_input(std::vector<std::uint8_t>& out, std::uint64_t session, std::uint32_t acked_snapshot,
                 const InputFrame* const* frames, std::size_t count);
void write_bye(std::vector<std::uint8_t>& out, std::uint64_t session);
void write_fragment(std::vector<std::uint8_t>& out, const FragmentHeader& header, const std::uint8_t* payload,
                    std::size_t size);

// Readers. read_header checks the magic and returns the type; then call the
// reader for that type. All of them validate untrusted input.
bool read_header(ByteReader& reader, MessageType& type);
// Fails on a Hello shorter than kMinHelloSize.
bool read_hello(ByteReader& reader, std::uint16_t& version, std::uint64_t& cookie);
bool read_challenge(ByteReader& reader, std::uint64_t& cookie);
bool read_welcome(ByteReader& reader, WelcomeMessage& message);
bool read_reject(ByteReader& reader, RejectReason& reason);
bool read_input(ByteReader& reader, InputMessage& message);
bool read_bye(ByteReader& reader, std::uint64_t& session);
bool read_fragment(ByteReader& reader, FragmentHeader& header, const std::uint8_t*& payload, std::size_t& size);

// Sequence numbers wrap around; compares them as a ring (RFC 1982 style).
inline bool sequence_newer(std::uint32_t a, std::uint32_t b) {
  return a != b && static_cast<std::uint32_t>(a - b) < 0x80000000u;
}

// The server's record of a client's acked snapshot after an Input reports
// `reported`. Inputs can arrive out of order, so a newer id wins; an explicit
// kFullSnapshot ("I hold nothing", after a reload or a restart) always wins;
// and any id replaces kFullSnapshot, so ids past 2^31 still get accepted.
inline std::uint32_t updated_snapshot_ack(std::uint32_t current, std::uint32_t reported) {
  if (reported == kFullSnapshot || current == kFullSnapshot) return reported;
  return sequence_newer(reported, current) ? reported : current;
}

// Reassembles one snapshot at a time from its fragments. A fragment of a
// newer snapshot abandons an incomplete older one; older fragments are ignored.
class SnapshotAssembler {
 public:
  // Returns true when this fragment completed a snapshot; then data(), size()
  // and ack_sequence() describe it until the next call.
  bool add(const FragmentHeader& header, const std::uint8_t* payload, std::size_t size);

  const std::uint8_t* data() const { return m_buffer.data(); }
  std::size_t size() const { return m_size; }
  std::uint32_t ack_sequence() const { return m_ack; }
  std::uint32_t snapshot_id() const { return m_last_completed; }
  std::uint32_t scene_hash() const { return m_scene_hash; }

 private:
  void start(const FragmentHeader& header);

  std::vector<std::uint8_t> m_buffer;
  std::array<bool, kMaxFragments> m_received{};
  std::uint32_t m_id = 0;
  std::uint16_t m_count = 0;
  std::uint16_t m_received_count = 0;
  std::uint32_t m_ack = 0;
  std::uint32_t m_scene_hash = 0;
  std::size_t m_size = 0;
  bool m_active = false;     // assembling m_id
  bool m_have_last = false;  // m_last_completed is meaningful
  std::uint32_t m_last_completed = 0;
};

}  // namespace scraps
