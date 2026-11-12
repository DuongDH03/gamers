#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/entity.h"
#include "core/math.h"

namespace scraps {

// What a command asks for. Values from kFirstGameCommand up belong to the game
// (see game_command()); the engine ignores them and the game's simulate
// handles them.
enum class CommandType : std::uint16_t {
  kNone = 0,
  kPlayerJoin = 1,   // server-only: spawn the player's entity
  kPlayerLeave = 2,  // server-only: remove the player's entity
  kMove = 3,         // vec.x, vec.y: world ground direction, length <= 1
  kJump = 4,
  kFirstGameCommand = 256,
};

constexpr CommandType game_command(std::uint16_t n) {
  return static_cast<CommandType>(static_cast<std::uint16_t>(256u + n));
}

// Commands that only the server may issue. A client sending one is ignored.
constexpr bool is_server_only(CommandType type) {
  return type == CommandType::kPlayerJoin || type == CommandType::kPlayerLeave;
}

// A request to change the world (Rule 1). Input, UI and the network produce
// these; only simulate() turns them into state changes.
struct Command {
  CommandType type = CommandType::kNone;
  PlayerId player = kNoPlayer;
  Vec3 vec;
  std::int32_t value = 0;
};

inline Command make_command(CommandType type, PlayerId player, Vec3 vec = Vec3(), std::int32_t value = 0) {
  Command command;
  command.type = type;
  command.player = player;
  command.vec = vec;
  command.value = value;
  return command;
}

// Enough for one tick on a full server: 16 players x (a join, a leave and
// their per-tick command budget, see net/server.cpp).
constexpr std::size_t kMaxCommandsPerList = 128;

// A fixed-capacity list of commands: plain data, no heap allocation.
class CommandList {
 public:
  // Returns false (and drops the command) when the list is full.
  bool push(const Command& command) {
    if (m_count >= m_items.size()) return false;
    m_items[m_count++] = command;
    return true;
  }
  void clear() { m_count = 0; }

  std::size_t size() const { return m_count; }
  bool empty() const { return m_count == 0; }
  bool full() const { return m_count >= m_items.size(); }
  const Command& operator[](std::size_t i) const { return m_items[i]; }

  const Command* begin() const { return m_items.data(); }
  const Command* end() const { return m_items.data() + m_count; }

 private:
  std::array<Command, kMaxCommandsPerList> m_items{};
  std::size_t m_count = 0;
};

}  // namespace scraps
