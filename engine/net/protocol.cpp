#include "net/protocol.h"

#include <cstring>

namespace scraps {
namespace {

void write_header(std::vector<std::uint8_t>& out, MessageType type) {
  out.clear();
  ByteWriter writer(out);
  writer.write_u32(kProtocolMagic);
  writer.write_u8(static_cast<std::uint8_t>(type));
}

void write_command(ByteWriter& writer, const Command& command) {
  writer.write_u16(static_cast<std::uint16_t>(command.type));
  writer.write_u8(command.player);
  writer.write_vec3(command.vec);
  writer.write_i32(command.value);
}

bool read_command(ByteReader& reader, Command& command) {
  command.type = static_cast<CommandType>(reader.read_u16());
  command.player = reader.read_u8();
  command.vec = reader.read_vec3();
  command.value = reader.read_i32();
  return reader.ok();
}

}  // namespace

const char* reject_reason_text(RejectReason reason) {
  switch (reason) {
    case RejectReason::kServerFull: return "the server is full";
    case RejectReason::kVersionMismatch: return "client and server protocol versions differ";
    default: return "rejected by the server";
  }
}

void write_hello(std::vector<std::uint8_t>& out, std::uint64_t cookie) {
  write_header(out, MessageType::kHello);
  ByteWriter writer(out);
  writer.write_u16(kProtocolVersion);
  writer.write_u64(cookie);
  while (out.size() < kMinHelloSize) writer.write_u8(0);
}

void write_challenge(std::vector<std::uint8_t>& out, std::uint64_t cookie) {
  write_header(out, MessageType::kChallenge);
  ByteWriter(out).write_u64(cookie);
}

void write_welcome(std::vector<std::uint8_t>& out, const WelcomeMessage& message) {
  write_header(out, MessageType::kWelcome);
  ByteWriter writer(out);
  writer.write_u8(message.player);
  writer.write_u64(message.session);
  writer.write_u32(message.server_tick);
  writer.write_u32(message.scene_hash);
  const std::size_t length =
      message.scene_name.size() < kMaxSceneNameLength ? message.scene_name.size() : kMaxSceneNameLength;
  writer.write_u8(static_cast<std::uint8_t>(length));
  writer.write_bytes(reinterpret_cast<const std::uint8_t*>(message.scene_name.data()), length);
  write_rules(writer, message.rules);
}

void write_reject(std::vector<std::uint8_t>& out, RejectReason reason) {
  write_header(out, MessageType::kReject);
  ByteWriter(out).write_u8(static_cast<std::uint8_t>(reason));
}

void write_input(std::vector<std::uint8_t>& out, std::uint64_t session, std::uint32_t acked_snapshot,
                 const InputFrame* const* frames, std::size_t count) {
  write_header(out, MessageType::kInput);
  ByteWriter writer(out);
  writer.write_u64(session);
  writer.write_u32(acked_snapshot);
  if (count > kMaxInputFramesPerPacket) count = kMaxInputFramesPerPacket;
  writer.write_u8(static_cast<std::uint8_t>(count));
  for (std::size_t i = 0; i < count; ++i) {
    const InputFrame& frame = *frames[i];
    const std::size_t commands =
        frame.commands.size() < kMaxCommandsPerInputFrame ? frame.commands.size() : kMaxCommandsPerInputFrame;
    writer.write_u32(frame.sequence);
    writer.write_u8(static_cast<std::uint8_t>(commands));
    for (std::size_t c = 0; c < commands; ++c) write_command(writer, frame.commands[c]);
  }
}

void write_bye(std::vector<std::uint8_t>& out, std::uint64_t session) {
  write_header(out, MessageType::kBye);
  ByteWriter(out).write_u64(session);
}

bool read_bye(ByteReader& reader, std::uint64_t& session) {
  session = reader.read_u64();
  return reader.ok();
}

void write_fragment(std::vector<std::uint8_t>& out, const FragmentHeader& header, const std::uint8_t* payload,
                    std::size_t size) {
  write_header(out, MessageType::kSnapshotFragment);
  ByteWriter writer(out);
  writer.write_u32(header.snapshot_id);
  writer.write_u16(header.index);
  writer.write_u16(header.count);
  writer.write_u32(header.ack_sequence);
  writer.write_u32(header.scene_hash);
  writer.write_u16(static_cast<std::uint16_t>(size));
  writer.write_bytes(payload, size);
}

bool read_header(ByteReader& reader, MessageType& type) {
  if (reader.read_u32() != kProtocolMagic) return false;
  const std::uint8_t value = reader.read_u8();
  if (!reader.ok() || value < static_cast<std::uint8_t>(MessageType::kHello) ||
      value > static_cast<std::uint8_t>(MessageType::kChallenge)) {
    return false;
  }
  type = static_cast<MessageType>(value);
  return true;
}

bool read_hello(ByteReader& reader, std::uint16_t& version, std::uint64_t& cookie) {
  // The 5-byte header is already read; the rest must make up the minimum.
  constexpr std::size_t kHeaderSize = 5;
  if (reader.remaining() + kHeaderSize < kMinHelloSize) return false;
  version = reader.read_u16();
  cookie = reader.read_u64();
  return reader.ok();
}

bool read_challenge(ByteReader& reader, std::uint64_t& cookie) {
  cookie = reader.read_u64();
  return reader.ok() && cookie != 0;
}

bool read_welcome(ByteReader& reader, WelcomeMessage& message) {
  message.player = reader.read_u8();
  message.session = reader.read_u64();
  message.server_tick = reader.read_u32();
  message.scene_hash = reader.read_u32();
  const std::uint8_t length = reader.read_u8();
  if (!reader.ok() || length > kMaxSceneNameLength || message.player >= kMaxPlayers) return false;
  const std::uint8_t* name = reader.skip(length);
  if (name == nullptr) return false;
  message.scene_name.assign(reinterpret_cast<const char*>(name), length);
  return read_rules(reader, message.rules);
}

bool read_reject(ByteReader& reader, RejectReason& reason) {
  reason = static_cast<RejectReason>(reader.read_u8());
  return reader.ok();
}

bool read_input(ByteReader& reader, InputMessage& message) {
  message.session = reader.read_u64();
  message.acked_snapshot = reader.read_u32();
  const std::uint8_t count = reader.read_u8();
  if (!reader.ok() || count > kMaxInputFramesPerPacket) return false;
  message.count = count;
  for (std::size_t i = 0; i < count; ++i) {
    InputFrame& frame = message.frames[i];
    frame.sequence = reader.read_u32();
    const std::uint8_t commands = reader.read_u8();
    if (!reader.ok() || commands > kMaxCommandsPerInputFrame) return false;
    frame.commands.clear();
    for (std::size_t c = 0; c < commands; ++c) {
      Command command;
      if (!read_command(reader, command)) return false;
      frame.commands.push(command);
    }
  }
  return reader.ok();
}

bool read_fragment(ByteReader& reader, FragmentHeader& header, const std::uint8_t*& payload, std::size_t& size) {
  header.snapshot_id = reader.read_u32();
  header.index = reader.read_u16();
  header.count = reader.read_u16();
  header.ack_sequence = reader.read_u32();
  header.scene_hash = reader.read_u32();
  size = reader.read_u16();
  if (!reader.ok() || size == 0 || size > kFragmentPayloadSize) return false;
  if (header.count == 0 || header.count > kMaxFragments || header.index >= header.count) return false;
  payload = reader.skip(size);
  return payload != nullptr;
}

// ---------------------------------------------------------------- SnapshotAssembler

void SnapshotAssembler::start(const FragmentHeader& header) {
  m_active = true;
  m_id = header.snapshot_id;
  m_count = header.count;
  m_received_count = 0;
  m_ack = header.ack_sequence;
  m_scene_hash = header.scene_hash;
  m_size = 0;
  m_received.fill(false);
  if (m_buffer.size() < kMaxSnapshotSize) m_buffer.resize(kMaxSnapshotSize);  // once
}

bool SnapshotAssembler::add(const FragmentHeader& header, const std::uint8_t* payload, std::size_t size) {
  if (header.count == 0 || header.count > kMaxFragments || header.index >= header.count) return false;
  if (size == 0 || size > kFragmentPayloadSize) return false;
  // Every fragment but the last is full-sized; the last one holds the rest.
  const bool last = header.index + 1 == header.count;
  if (!last && size != kFragmentPayloadSize) return false;

  if (m_have_last && !sequence_newer(header.snapshot_id, m_last_completed)) return false;  // old or done
  if (!m_active || sequence_newer(header.snapshot_id, m_id)) {
    start(header);
  } else if (header.snapshot_id != m_id) {
    return false;  // fragment of an older, abandoned snapshot
  }
  if (header.count != m_count || header.ack_sequence != m_ack || header.scene_hash != m_scene_hash) {
    return false;  // inconsistent: ignore
  }
  if (m_received[header.index]) return false;                                 // duplicate

  const std::size_t offset = static_cast<std::size_t>(header.index) * kFragmentPayloadSize;
  std::memcpy(m_buffer.data() + offset, payload, size);
  m_received[header.index] = true;
  ++m_received_count;
  if (last) m_size = offset + size;

  if (m_received_count != m_count) return false;
  m_active = false;
  m_have_last = true;
  m_last_completed = m_id;
  return true;
}

}  // namespace scraps
