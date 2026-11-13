#include "common/ui_model.h"

#include <cmath>
#include <cstring>

namespace scraps_apps {
namespace {

struct ActionName {
  const char* name;
  UiActionType type;
};

constexpr ActionName kActionNames[] = {
    {"play", UiActionType::kPlay},
    {"continue", UiActionType::kContinue},
    {"quit", UiActionType::kQuit},
    {"pause", UiActionType::kPause},
    {"resume", UiActionType::kResume},
    {"save", UiActionType::kSave},
    {"load", UiActionType::kLoad},
    {"open_settings", UiActionType::kOpenSettings},
    {"close_settings", UiActionType::kCloseSettings},
    {"set_music_volume", UiActionType::kSetMusicVolume},
    {"set_effects_volume", UiActionType::kSetEffectsVolume},
    {"command", UiActionType::kCommand},
};

float clamp_volume(double value) {
  if (value < 0.0) return 0.0f;
  if (value > 1.0) return 1.0f;
  return static_cast<float>(value);
}

}  // namespace

const char* ui_screen_name(UiScreen screen) {
  switch (screen) {
    case UiScreen::kMain: return "main";
    case UiScreen::kPlaying: return "playing";
    case UiScreen::kPaused: return "paused";
    case UiScreen::kSettings: return "settings";
  }
  return "main";
}

const char* ui_action_name(UiActionType type) {
  for (const ActionName& entry : kActionNames) {
    if (entry.type == type) return entry.name;
  }
  return "";
}

UiParse parse_ui_action(const char* json, std::size_t size, JsonObject& scratch, UiAction& out) {
  out = UiAction();
  if (!scratch.parse(json, size)) return UiParse::kMalformed;
  const char* type = scratch.string("type");
  const char* name = scratch.string("name");
  if (type == nullptr || name == nullptr) return UiParse::kMalformed;
  if (std::strcmp(type, "action") != 0) return UiParse::kUnknown;
  bool known = false;
  for (const ActionName& entry : kActionNames) {
    if (std::strcmp(entry.name, name) == 0) {
      out.type = entry.type;
      known = true;
      break;
    }
  }
  if (!known) return UiParse::kUnknown;
  if (out.type == UiActionType::kSetMusicVolume || out.type == UiActionType::kSetEffectsVolume) {
    const JsonField* value = scratch.find("value");
    if (value == nullptr || value->type != JsonType::kNumber) return UiParse::kMalformed;
    out.value = clamp_volume(value->number);  // the reader only gives finite numbers
  } else if (out.type == UiActionType::kCommand) {
    const JsonField* command = scratch.find("command");
    if (command == nullptr || command->type != JsonType::kString) return UiParse::kMalformed;
    // No game command has a longer name (or an empty one).
    if (command->text_size == 0 || command->text_size >= out.command.size()) return UiParse::kUnknown;
    std::memcpy(out.command.data(), command->text.data(), command->text_size);
    out.command[command->text_size] = '\0';
  }
  return UiParse::kAction;
}

UiModel::UiModel(const OfflinePause& pause, bool online, bool start_at_menu)
    : m_pause(pause), m_online(online), m_started(!start_at_menu) {}

UiScreen UiModel::screen() const {
  if (m_settings_open) return UiScreen::kSettings;
  if (!m_started) return UiScreen::kMain;
  if (m_pause.paused() || m_menu_open) return UiScreen::kPaused;
  return UiScreen::kPlaying;
}

void UiModel::apply_volumes(scraps::Mixer& mixer) const {
  mixer.set_music_gain(m_music_volume * (m_pause.paused() ? kPauseMusicDuck : 1.0f));
  mixer.set_effects_gain(m_effects_volume);
}

void UiModel::receive(scraps::UiHost& host, const GameModule& game, UiRequests& out) {
  host.receive(m_inbox);
  m_dropped += m_inbox.dropped();
  for (std::size_t i = 0; i < m_inbox.count(); ++i) {
    UiAction action;
    switch (parse_ui_action(m_inbox.data(i), m_inbox.size(i), m_json, action)) {
      case UiParse::kAction: handle(action, game, out); break;
      case UiParse::kMalformed: ++m_malformed; break;
      case UiParse::kUnknown: ++m_unknown; break;
    }
  }
}

void UiModel::push(UiRequests& out, UiRequest request) {
  if (!out.push(request)) ++m_dropped;
}

void UiModel::request_pause(UiRequests& out) {
  if (m_online) {
    m_menu_open = true;  // the menu opens; the game runs on
  } else {
    push(out, UiRequest::kPause);
  }
}

void UiModel::request_resume(UiRequests& out) {
  if (m_online) {
    m_menu_open = false;
  } else {
    push(out, UiRequest::kResume);
  }
}

void UiModel::handle(const UiAction& action, const GameModule& game, UiRequests& out) {
  const UiScreen now = screen();
  switch (action.type) {
    case UiActionType::kPlay:
    case UiActionType::kContinue:
      if (m_started) {
        ++m_ignored;
      } else {
        push(out, action.type == UiActionType::kPlay ? UiRequest::kPlay : UiRequest::kContinue);
      }
      break;
    case UiActionType::kQuit:
      push(out, UiRequest::kQuit);
      break;
    case UiActionType::kPause:
      if (now == UiScreen::kPlaying) {
        request_pause(out);
      } else {
        ++m_ignored;
      }
      break;
    case UiActionType::kResume:
      if (now == UiScreen::kPaused) {
        request_resume(out);
      } else {
        ++m_ignored;
      }
      break;
    case UiActionType::kSave:
    case UiActionType::kLoad:
      if (m_started) {
        push(out, action.type == UiActionType::kSave ? UiRequest::kSave : UiRequest::kLoad);
      } else {
        ++m_ignored;
      }
      break;
    case UiActionType::kOpenSettings:
      m_settings_open = true;
      break;
    case UiActionType::kCloseSettings:
      m_settings_open = false;
      break;
    case UiActionType::kSetMusicVolume:
      m_music_volume = clamp_volume(action.value);
      break;
    case UiActionType::kSetEffectsVolume:
      m_effects_volume = clamp_volume(action.value);
      break;
    case UiActionType::kCommand: {
      scraps::CommandType type = scraps::CommandType::kNone;
      if (!game.find_ui_command(action.command.data(), std::strlen(action.command.data()), type)) {
        ++m_unknown;
      } else if (now != UiScreen::kPlaying) {
        ++m_ignored;  // a game button pressed in a menu does nothing, now or later
      } else if (m_queued >= m_commands.size()) {
        ++m_dropped;
      } else {
        m_commands[m_queued++] = type;
      }
      break;
    }
  }
}

void UiModel::escape(UiRequests& out) {
  switch (screen()) {
    case UiScreen::kMain: break;
    case UiScreen::kSettings: m_settings_open = false; break;
    case UiScreen::kPaused: request_resume(out); break;
    case UiScreen::kPlaying: request_pause(out); break;
  }
}

void UiModel::toggle_pause(UiRequests& out) {
  const UiScreen now = screen();
  if (now == UiScreen::kPlaying) {
    request_pause(out);
  } else if (now == UiScreen::kPaused) {
    request_resume(out);
  }
}

void UiModel::take_commands(scraps::PlayerId player, scraps::CommandList& out) {
  for (std::size_t i = 0; i < m_queued; ++i) {
    if (!out.push(scraps::make_command(m_commands[i], player))) ++m_dropped;
  }
  m_queued = 0;
}

std::size_t UiModel::write_view(const UiAppState& state, const GameModule& game, char* out,
                                std::size_t capacity) const {
  JsonWriter json(out, capacity);
  json.begin_object();
  json.key("type");
  json.string("view");
  json.key("screen");
  json.string(ui_screen_name(screen()));
  json.key("game");
  json.string(state.game);
  json.key("hud");
  json.string(state.hud);
  json.key("paused");
  json.boolean(m_pause.paused());
  json.key("has_save");
  json.boolean(state.has_save);
  json.key("online");
  json.boolean(m_online);
  json.key("music_volume");
  json.number(m_music_volume);
  json.key("effects_volume");
  json.number(m_effects_volume);
  json.key("notice");
  json.string(state.notice);
  json.key("commands");
  json.begin_array();
  for (std::size_t i = 0; i < game.ui_command_count(); ++i) json.string(game.ui_command(i).name);
  json.end_array();
  json.end_object();
  return json.ok() ? json.size() : 0;
}

bool UiModel::publish(scraps::UiHost& host, const UiAppState& state, const GameModule& game) {
  const std::size_t size = write_view(state, game, m_view.data(), m_view.size());
  if (size == 0) {
    ++m_dropped;  // cannot happen with the limits above; never post a cut message
    return false;
  }
  if (m_posted_once && size == m_last_view_size && std::memcmp(m_view.data(), m_last_view.data(), size) == 0) {
    return false;
  }
  if (!host.post(m_view.data(), size)) return false;  // tried again next frame
  std::memcpy(m_last_view.data(), m_view.data(), size);
  m_last_view_size = size;
  m_posted_once = true;
  ++m_posts;
  return true;
}

}  // namespace scraps_apps
