// The UI layer's prerequisites (docs/specs/2026-11-14-offline-essentials-
// and-ui.md, part 6a): the input event list and text input, the JSON bridge,
// the UI model, GameApi version 3's UI commands, and the client as a
// one-frame function driven by a headless UI.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "backends/headless/headless_backend.h"
#include "client/client_app.h"
#include "common/game_module.h"
#include "common/pause.h"
#include "common/ui_json.h"
#include "common/ui_model.h"
#include "core/fixed_step.h"
#include "core/mixer.h"
#include "core/scene.h"
#include "harness.h"
#include "net/client.h"
#include "net/cookie.h"
#include "net/loopback.h"
#include "net/server.h"
#include "sandbox/sandbox.h"
#include "selected_game.h"
#include "views/view_factory.h"

namespace {

using namespace scraps;
using namespace scraps_apps;

// ------------------------------------------------------------ event list

void event_list_keeps_order_and_counts_overflow(test::Context& ctx) {
  headless::HeadlessBackend backend;
  headless::HeadlessInput& input = backend.input();
  input.start_text_input();
  input.connect_gamepad(true);
  backend.window().pump_events();
  CHECK(input.events().empty());

  // Everything in one frame, in this order.
  input.press(Key::kA);
  input.move_pointer(10.0f, 20.0f);
  input.move_pointer(11.0f, 21.0f);  // merged into the move before
  input.press(MouseButton::kLeft);
  input.move_pointer(12.0f, 22.0f);
  CHECK(input.type_text("hi"));
  input.scroll(-2.0f);
  input.press(GamepadButton::kSouth);
  input.release(Key::kA);
  input.release(MouseButton::kLeft);
  input.release(GamepadButton::kSouth);
  input.press(Key::kA);
  input.press(Key::kA);  // already down: nothing
  CHECK(input.events().empty());  // nothing before the frame starts
  backend.window().pump_events();
  const InputEventList& events = input.events();
  const InputEventType expected[] = {
      InputEventType::kKeyDown,     InputEventType::kPointerMove, InputEventType::kPointerDown,
      InputEventType::kPointerMove, InputEventType::kText,        InputEventType::kWheel,
      InputEventType::kPadDown,     InputEventType::kKeyUp,       InputEventType::kPointerUp,
      InputEventType::kPadUp,       InputEventType::kKeyDown,
  };
  CHECK(events.size() == sizeof(expected) / sizeof(expected[0]));
  for (std::size_t i = 0; i < events.size() && i < sizeof(expected) / sizeof(expected[0]); ++i) {
    CHECK(events[i].type == expected[i]);
  }
  CHECK(events.dropped() == 0);
  if (events.size() == sizeof(expected) / sizeof(expected[0])) {
    CHECK(events[0].key == Key::kA);
    CHECK(events[1].x == 11.0f && events[1].y == 21.0f);  // the last of two moves in a row
    CHECK(events[2].button == MouseButton::kLeft && events[2].x == 11.0f);
    CHECK(std::string(events[4].text.data()) == "hi");
    CHECK(events[5].wheel == -2.0f);
    CHECK(events[6].pad == GamepadButton::kSouth);
    CHECK(events[7].key == Key::kA && !events[7].repeat);
  }
  // The polled state is unchanged by the list.
  CHECK(input.was_pressed(Key::kA) && input.was_pressed(MouseButton::kLeft));
  CHECK(input.wheel() == -2.0f);

  backend.window().pump_events();
  CHECK(input.events().empty());  // one frame only

  // More than fit: the first kCapacity stay, in order; the rest are counted.
  const std::size_t extra = 50;
  for (std::size_t i = 0; i < InputEventList::kCapacity + extra; ++i) input.scroll(static_cast<float>(i));
  backend.window().pump_events();
  CHECK(input.events().size() == InputEventList::kCapacity);
  CHECK(input.events().dropped() == extra);
  CHECK(input.events()[InputEventList::kCapacity - 1].wheel == static_cast<float>(InputEventList::kCapacity - 1));
  CHECK(input.wheel() == static_cast<float>((InputEventList::kCapacity + extra) *
                                            (InputEventList::kCapacity + extra - 1) / 2));  // polling still sees all
  backend.window().pump_events();
  CHECK(input.events().empty() && input.events().dropped() == 0);
  // Out of range reads stay inside the list.
  CHECK(input.events()[1000].type == input.events()[0].type);
}

void text_input_is_split_and_checked(test::Context& ctx) {
  headless::HeadlessBackend backend;
  headless::HeadlessInput& input = backend.input();
  CHECK(!input.text_input_active());
  CHECK(!input.type_text("ignored"));  // off: like a real keyboard, no text
  input.start_text_input();
  CHECK(input.text_input_active());
  // 40 characters of mixed widths: 1, 2, 3 and 4 bytes.
  std::string typed;
  const char* pieces[] = {"a", "\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80"};
  for (int i = 0; i < 40; ++i) typed += pieces[i % 4];
  CHECK(input.type_text(typed.c_str()));
  backend.window().pump_events();
  std::string joined;
  for (const InputEvent& event : input.events()) {
    CHECK(event.type == InputEventType::kText);
    const std::size_t length = std::strlen(event.text.data());
    CHECK(length > 0 && length <= InputEvent::kTextCapacity - 1);
    // Every piece is whole UTF-8.
    std::size_t at = 0;
    while (at < length) {
      const std::size_t n = InputEventList::utf8_sequence_length(event.text.data() + at, length - at);
      CHECK(n > 0);
      if (n == 0) break;
      at += n;
    }
    joined += event.text.data();
  }
  CHECK(input.events().size() > 1);
  CHECK(joined == typed);

  // Bytes that are not UTF-8 are skipped: a lone continuation, an overlong
  // '/', a surrogate, a value past U+10FFFF, a cut sequence.
  CHECK(input.type_text("x\x80y\xC0\xAFz\xED\xA0\x80w\xF4\x90\x80\x80v\xE2\x82"));
  backend.window().pump_events();
  CHECK(input.events().size() == 1 && std::string(input.events()[0].text.data()) == "xyzwv");

  input.stop_text_input();
  CHECK(!input.type_text("more"));
  backend.window().pump_events();
  CHECK(input.events().empty());

  // push_text on a full list counts what did not fit.
  InputEventList list;
  InputEvent key;
  for (std::size_t i = 0; i < InputEventList::kCapacity; ++i) list.push(key);
  CHECK(!list.push_text("abc", 3));
  CHECK(list.dropped() == 1 && list.size() == InputEventList::kCapacity);
}

// ------------------------------------------------------------------- JSON

bool parses(const char* text) {
  JsonObject object;
  return object.parse(text, std::strlen(text));
}

void json_reader_takes_flat_objects(test::Context& ctx) {
  JsonObject object;
  const char* text =
      " {\"type\" : \"action\",\n\t\"name\":\"set_music_volume\", \"value\": -0.5e2, \"on\": true, \"off\": false,"
      "\"none\": null, \"esc\": \"q\\\"b\\\\s\\/n\\nt\\tu\\u00e9\\ud83d\\ude00\", \"utf8\": \"\xC3\xA9\"} ";
  CHECK(object.parse(text, std::strlen(text)));
  CHECK(object.size() == 8);
  CHECK(std::string(object.string("type")) == "action");
  const JsonField* value = object.find("value");
  CHECK(value != nullptr && value->type == JsonType::kNumber && value->number == -50.0);
  CHECK(object.find("on")->type == JsonType::kBool && object.find("on")->boolean);
  CHECK(object.find("off")->type == JsonType::kBool && !object.find("off")->boolean);
  CHECK(object.find("none")->type == JsonType::kNull);
  CHECK(std::string(object.string("esc")) == "q\"b\\s/n\nt\tu\xC3\xA9\xF0\x9F\x98\x80");
  CHECK(object.find("esc")->text_size == std::strlen("q\"b\\s/n\nt\tu\xC3\xA9\xF0\x9F\x98\x80"));
  CHECK(std::string(object.string("utf8")) == "\xC3\xA9");
  CHECK(object.string("value") == nullptr);  // not a string
  CHECK(object.find("missing") == nullptr);
  CHECK(parses("{}"));
  CHECK(parses("{\"n\":0}") && parses("{\"n\":-0.25}") && parses("{\"n\":1E+3}"));
}

void json_reader_refuses_hostile_input(test::Context& ctx) {
  const char* bad[] = {
      "",
      " ",
      "{",
      "}",
      "[]",
      "null",
      "\"text\"",
      "{\"a\":1,}",
      "{,}",
      "{\"a\":1}x",
      "{\"a\":1}{}",
      "{\"a\":[1]}",
      "{\"a\":{}}",
      "{'a':1}",
      "{a:1}",
      "{\"a\" 1}",
      "{\"a\":}",
      "{\"a\":01}",
      "{\"a\":1.}",
      "{\"a\":.5}",
      "{\"a\":+1}",
      "{\"a\":1e}",
      "{\"a\":1e999}",
      "{\"a\":-1e999}",
      "{\"a\":-}",
      "{\"a\":NaN}",
      "{\"a\":Infinity}",
      "{\"a\":tru}",
      "{\"a\":nul}",
      "{\"a\":True}",
      "{\"a\":\"\\x\"}",
      "{\"a\":\"\\u12\"}",
      "{\"a\":\"\\u12G4\"}",
      "{\"a\":\"\\ud800\"}",
      "{\"a\":\"\\ud800\\u0041\"}",
      "{\"a\":\"\\udc00\"}",
      "{\"a\":\"\\u0000\"}",
      "{\"a\":\"tab\there\"}",
      "{\"a\":\"\xFF\"}",
      "{\"a\":\"\xC0\xAF\"}",
      "{\"a\":\"\xED\xA0\x80\"}",
      "{\"a\":\"open}",
      "{\"a\":1 /* comment */}",
      "{\"a\":1,\"a\":2}",
  };
  for (const char* text : bad) {
    const bool parsed = parses(text);
    CHECK(!parsed);
    if (parsed) std::printf("    accepted: %s\n", text);
  }
  // A '\0' inside the message.
  {
    const char text[] = "{\"a\":\"x\0y\"}";
    JsonObject object;
    CHECK(!object.parse(text, sizeof(text) - 1));
  }
  // Limits: a key of 32 bytes, a string of 256, 17 fields.
  const std::string long_key = "{\"" + std::string(32, 'k') + "\":1}";
  const std::string fine_key = "{\"" + std::string(31, 'k') + "\":1}";
  const std::string long_text = "{\"a\":\"" + std::string(256, 't') + "\"}";
  const std::string fine_text = "{\"a\":\"" + std::string(255, 't') + "\"}";
  CHECK(!parses(long_key.c_str()) && parses(fine_key.c_str()));
  CHECK(!parses(long_text.c_str()) && parses(fine_text.c_str()));
  std::string fields = "{";
  for (int i = 0; i < 17; ++i) fields += (i > 0 ? ",\"f" : "\"f") + std::to_string(i) + "\":" + std::to_string(i);
  CHECK(!parses((fields + "}").c_str()));
  fields = "{";
  for (int i = 0; i < 16; ++i) fields += (i > 0 ? ",\"f" : "\"f") + std::to_string(i) + "\":" + std::to_string(i);
  CHECK(parses((fields + "}").c_str()));
  // Null data and a refused parse leave nothing behind.
  JsonObject object;
  CHECK(object.parse("{\"a\":1}", 7) && object.size() == 1);
  CHECK(!object.parse(nullptr, 5) && object.size() == 0);

  // Random bytes and damaged good messages: never a crash, never an
  // out-of-range read (the sanitizers of other builds would see it), and
  // every accepted message holds only finite numbers.
  const std::string good =
      "{\"type\":\"action\",\"name\":\"set_music_volume\",\"value\":0.5,\"command\":\"drop_coin\",\"x\":\"\\u00e9\"}";
  std::uint32_t seed = 12345u;
  auto next = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
  };
  long accepted = 0;
  for (int round = 0; round < 20000; ++round) {
    std::string text = good;
    const int edits = 1 + static_cast<int>(next() % 4);
    for (int e = 0; e < edits; ++e) {
      const std::size_t at = next() % (text.size() + 1);
      switch (next() % 4) {
        case 0: if (at < text.size()) text[at] = static_cast<char>(next() & 0xFF); break;
        case 1: if (at < text.size()) text.erase(at, 1 + next() % 8); break;
        case 2: text.insert(at, 1, static_cast<char>(next() & 0xFF)); break;
        default: text.resize(at); break;
      }
    }
    if (round % 10 == 0) {
      text.clear();
      const std::size_t length = next() % 64;
      for (std::size_t i = 0; i < length; ++i) text += static_cast<char>(next() & 0xFF);
    }
    JsonObject parsed;
    if (parsed.parse(text.data(), text.size())) {
      ++accepted;
      for (std::size_t i = 0; i < parsed.size(); ++i) {
        const JsonField& field = parsed.field(i);
        if (field.type == JsonType::kNumber) CHECK(std::isfinite(field.number));
        CHECK(std::strlen(field.text.data()) == (field.type == JsonType::kString ? field.text_size : 0));
      }
    }
  }
  CHECK(accepted > 0);  // some edits keep it valid (inside a string)
}

void json_writer_escapes_and_reads_back(test::Context& ctx) {
  char buffer[512];
  JsonWriter json(buffer, sizeof(buffer));
  json.begin_object();
  json.key("s");
  json.string("quote\" back\\ nl\n tab\t bell\x07 del\x7F \xC3\xA9");
  json.key("cut");
  json.string("ok\xE2\x82");  // a character cut in half
  json.key("n");
  json.number(0.8f);
  json.key("nan");
  json.number(std::numeric_limits<float>::quiet_NaN());
  json.key("b");
  json.boolean(true);
  json.key("list");
  json.begin_array();
  json.string("a");
  json.string("b");
  json.end_array();
  json.end_object();
  CHECK(json.ok());
  const std::string text(buffer, json.size());
  CHECK(text.find("\"n\":0.8,") != std::string::npos);
  CHECK(text.find("\"nan\":0,") != std::string::npos);
  CHECK(text.find("\"list\":[\"a\",\"b\"]}") != std::string::npos);
  CHECK(text.find("\\u0007") != std::string::npos && text.find("\\u007f") != std::string::npos);
  CHECK(text.find("\"ok??\"") != std::string::npos);

  // Reads back with the reader (without the array, which the reader refuses).
  JsonWriter flat_writer(buffer, sizeof(buffer));
  flat_writer.begin_object();
  flat_writer.key("s");
  flat_writer.string("quote\" back\\ nl\n tab\t \xC3\xA9");
  flat_writer.key("n");
  flat_writer.number(0.3f);
  flat_writer.end_object();
  CHECK(flat_writer.ok());
  JsonObject object;
  CHECK(object.parse(buffer, flat_writer.size()));
  CHECK(object.string("s") != nullptr && std::string(object.string("s")) == "quote\" back\\ nl\n tab\t \xC3\xA9");
  CHECK(object.find("n") != nullptr && static_cast<float>(object.find("n")->number) == 0.3f);

  // Out of room: not ok, and nothing written past the end.
  char small[10];
  std::memset(small, 'X', sizeof(small));
  JsonWriter tight(small, 8);
  tight.begin_object();
  tight.key("long-key");
  tight.string("value");
  tight.end_object();
  CHECK(!tight.ok());
  CHECK(small[8] == 'X' && small[9] == 'X');
}

// ---------------------------------------------------------------- actions

UiParse parse(const char* text, UiAction& action) {
  JsonObject scratch;
  return parse_ui_action(text, std::strlen(text), scratch, action);
}

void actions_parse_and_odd_ones_are_refused(test::Context& ctx) {
  UiAction action;
  const char* names[] = {"play",  "continue",      "quit",           "pause",
                         "resume", "save",         "load",           "open_settings",
                         "close_settings"};
  for (const char* name : names) {
    const std::string text = std::string("{\"type\":\"action\",\"name\":\"") + name + "\"}";
    CHECK(parse(text.c_str(), action) == UiParse::kAction);
    CHECK(std::string(ui_action_name(action.type)) == name);
  }
  CHECK(parse("{\"type\":\"action\",\"name\":\"set_music_volume\",\"value\":0.25}", action) == UiParse::kAction);
  CHECK(action.type == UiActionType::kSetMusicVolume && action.value == 0.25f);
  CHECK(parse("{\"type\":\"action\",\"name\":\"set_effects_volume\",\"value\":7}", action) == UiParse::kAction);
  CHECK(action.type == UiActionType::kSetEffectsVolume && action.value == 1.0f);  // clamped
  CHECK(parse("{\"type\":\"action\",\"name\":\"set_music_volume\",\"value\":-1}", action) == UiParse::kAction);
  CHECK(action.value == 0.0f);
  CHECK(parse("{\"name\":\"command\",\"command\":\"drop_coin\",\"type\":\"action\",\"extra\":null}", action) ==
        UiParse::kAction);
  CHECK(action.type == UiActionType::kCommand && std::string(action.command.data()) == "drop_coin");

  // Malformed: not JSON, fields missing or of the wrong type.
  CHECK(parse("not json", action) == UiParse::kMalformed);
  CHECK(parse("{\"name\":\"play\"}", action) == UiParse::kMalformed);
  CHECK(parse("{\"type\":\"action\"}", action) == UiParse::kMalformed);
  CHECK(parse("{\"type\":1,\"name\":\"play\"}", action) == UiParse::kMalformed);
  CHECK(parse("{\"type\":\"action\",\"name\":\"set_music_volume\"}", action) == UiParse::kMalformed);
  CHECK(parse("{\"type\":\"action\",\"name\":\"set_music_volume\",\"value\":\"0.5\"}", action) ==
        UiParse::kMalformed);
  CHECK(parse("{\"type\":\"action\",\"name\":\"command\"}", action) == UiParse::kMalformed);
  CHECK(parse("{\"type\":\"action\",\"name\":\"command\",\"command\":3}", action) == UiParse::kMalformed);
  // Unknown: another message type, action or command name.
  CHECK(parse("{\"type\":\"view\",\"name\":\"play\"}", action) == UiParse::kUnknown);
  CHECK(parse("{\"type\":\"action\",\"name\":\"fly\"}", action) == UiParse::kUnknown);
  CHECK(parse("{\"type\":\"action\",\"name\":\"PLAY\"}", action) == UiParse::kUnknown);
  CHECK(parse("{\"type\":\"action\",\"name\":\"command\",\"command\":\"\"}", action) == UiParse::kUnknown);
  const std::string long_command =
      "{\"type\":\"action\",\"name\":\"command\",\"command\":\"" + std::string(32, 'c') + "\"}";
  CHECK(parse(long_command.c_str(), action) == UiParse::kUnknown);
}

void ui_commands_are_checked_and_copied(test::Context& ctx) {
  GameApi api = *sandbox::game_api();
  std::string error;
  CHECK(check_game_api(&api, error));
  CHECK(api.ui_command_count == 1 && std::string(api.ui_commands[0].name) == "drop_coin");

  const GameUiCommand good[] = {{"drop_coin", sandbox::kDropCoin}, {"Jump_2", CommandType::kJump}};
  api.ui_commands = good;
  api.ui_command_count = 2;
  CHECK(check_game_api(&api, error));

  const std::string too_long(32, 'n');
  const GameUiCommand bad[][2] = {
      {{"drop_coin", sandbox::kDropCoin}, {"drop_coin", CommandType::kJump}},  // same name twice
      {{"", sandbox::kDropCoin}, {"b", CommandType::kJump}},
      {{nullptr, sandbox::kDropCoin}, {"b", CommandType::kJump}},
      {{"has space", sandbox::kDropCoin}, {"b", CommandType::kJump}},
      {{"Jump-2", sandbox::kDropCoin}, {"b", CommandType::kJump}},  // the React app refuses '-'
      {{"2jump", sandbox::kDropCoin}, {"b", CommandType::kJump}},   // and a digit first
      {{"quote\"", sandbox::kDropCoin}, {"b", CommandType::kJump}},
      {{"\xC3\xA9", sandbox::kDropCoin}, {"b", CommandType::kJump}},
      {{too_long.c_str(), sandbox::kDropCoin}, {"b", CommandType::kJump}},
      {{"none", CommandType::kNone}, {"b", CommandType::kJump}},
      {{"join", CommandType::kPlayerJoin}, {"b", CommandType::kJump}},
      {{"leave", CommandType::kPlayerLeave}, {"b", CommandType::kJump}},
  };
  for (const auto& pair : bad) {
    api.ui_commands = pair;
    api.ui_command_count = 2;
    error.clear();
    CHECK(!check_game_api(&api, error));
    CHECK(error == "UI command 0 of the game is not valid" || error == "UI command 1 of the game is not valid");
  }
  api.ui_commands = nullptr;
  api.ui_command_count = 1;
  CHECK(!check_game_api(&api, error) && error == "the game has too many UI commands");
  api.ui_commands = good;
  api.ui_command_count = kMaxGameUiCommands + 1;
  CHECK(!check_game_api(&api, error) && error == "the game has too many UI commands");
  const std::string thirty_one(31, 'n');
  const GameUiCommand longest[] = {{thirty_one.c_str(), CommandType::kJump}};
  api.ui_commands = longest;
  api.ui_command_count = 1;
  CHECK(check_game_api(&api, error));

  // The module copies them: the game's strings may go away.
  std::string name = "drop_coin";
  const GameUiCommand from_game[] = {{name.c_str(), sandbox::kDropCoin}};
  api.ui_commands = from_game;
  GameModule game;
  CHECK(game.use_static(&api, error));
  name[0] = 'x';
  CHECK(game.ui_command_count() == 1 && std::string(game.ui_command(0).name) == "drop_coin");
  CommandType type = CommandType::kNone;
  CHECK(game.find_ui_command("drop_coin", 9, type) && type == sandbox::kDropCoin);
  CHECK(!game.find_ui_command("drop_coi", 8, type));
  CHECK(!game.find_ui_command("drop_coin_", 10, type));
}

// ------------------------------------------------------------- UI model

// An embedded server and a client, as the offline app has them, so the
// app's pause can really pause.
struct OfflinePair {
  OfflinePair() : server_net(hub, std::uint16_t{1}), client_net(hub, std::uint16_t{2}) {
    Scene scene;
    std::string error;
    load_scene(SCRAPS_SANDBOX_SCENE_DIR, "top-down", scene, error);
    ServerConfig config;
    config.cookie_secret = CookieSecret{0x0123456789ABCDEFull, 0x1122334455667788ull};
    server = std::make_unique<Server>(server_net, make_world(scene, movement_rules_for(scene.level.perspective)),
                                      &sandbox::simulate, scene.name, scene.content_hash, config);
    ClientConfig client_config;
    client_config.scene_directory = SCRAPS_SANDBOX_SCENE_DIR;
    client = std::make_unique<Client>(client_net, server_net.local_address(), &sandbox::simulate, client_config);
  }
  LoopbackHub hub;
  LoopbackNet server_net;
  LoopbackNet client_net;
  std::unique_ptr<Server> server;
  std::unique_ptr<Client> client;
  FixedStepClock clock;
  OfflinePause pause;
};

void handle(UiModel& model, const GameModule& game, const char* text, UiRequests& out) {
  JsonObject scratch;
  UiAction action;
  if (parse_ui_action(text, std::strlen(text), scratch, action) == UiParse::kAction) model.handle(action, game, out);
}

std::string action(const char* name) { return std::string("{\"type\":\"action\",\"name\":\"") + name + "\"}"; }

void each_action_does_what_it_says(test::Context& ctx) {
  GameModule game;
  std::string error;
  CHECK(game.use_static(sandbox::game_api(), error));
  OfflinePair offline;
  UiModel model(offline.pause, false, true);
  UiRequests out;
  CHECK(model.screen() == UiScreen::kMain && !model.started() && !model.game_has_input());

  // Main menu: only play, continue, quit, settings and volumes make sense.
  for (const char* name : {"pause", "resume", "save", "load"}) handle(model, game, action(name).c_str(), out);
  handle(model, game, "{\"type\":\"action\",\"name\":\"command\",\"command\":\"drop_coin\"}", out);
  CHECK(out.count == 0 && model.ignored() == 5 && model.queued_commands() == 0);
  handle(model, game, action("play").c_str(), out);
  handle(model, game, action("continue").c_str(), out);
  CHECK(out.count == 2 && out.items[0] == UiRequest::kPlay && out.items[1] == UiRequest::kContinue);
  CHECK(model.screen() == UiScreen::kMain);  // until the app says it started
  model.mark_started();
  CHECK(model.screen() == UiScreen::kPlaying && model.game_has_input());
  out.count = 0;
  handle(model, game, action("play").c_str(), out);
  CHECK(out.count == 0 && model.ignored() == 6);

  // Playing: save, load, quit go to the app; pause too, offline.
  for (const char* name : {"save", "load", "pause", "quit"}) handle(model, game, action(name).c_str(), out);
  CHECK(out.count == 4 && out.items[0] == UiRequest::kSave && out.items[1] == UiRequest::kLoad &&
        out.items[2] == UiRequest::kPause && out.items[3] == UiRequest::kQuit);
  // The app pauses through its OfflinePause: now the screen is the pause menu.
  CHECK(offline.pause.set(true, offline.server.get(), *offline.client, offline.clock));
  CHECK(model.screen() == UiScreen::kPaused && !model.game_has_input());
  out.count = 0;
  handle(model, game, action("pause").c_str(), out);
  CHECK(out.count == 0);
  handle(model, game, action("resume").c_str(), out);
  CHECK(out.count == 1 && out.items[0] == UiRequest::kResume);
  offline.pause.set(false, offline.server.get(), *offline.client, offline.clock);

  // Settings: over any screen, and the game gets no input there.
  handle(model, game, action("open_settings").c_str(), out);
  CHECK(model.screen() == UiScreen::kSettings && !model.game_has_input());
  handle(model, game, "{\"type\":\"action\",\"name\":\"command\",\"command\":\"drop_coin\"}", out);
  CHECK(model.queued_commands() == 0);
  handle(model, game, action("close_settings").c_str(), out);
  CHECK(model.screen() == UiScreen::kPlaying);

  // Volumes, clamped.
  handle(model, game, "{\"type\":\"action\",\"name\":\"set_music_volume\",\"value\":0.3}", out);
  handle(model, game, "{\"type\":\"action\",\"name\":\"set_effects_volume\",\"value\":1.5}", out);
  CHECK(model.music_volume() == 0.3f && model.effects_volume() == 1.0f);

  // Game commands: queued, then taken once, for the player, at a tick.
  const std::size_t unknown_before = model.unknown();
  handle(model, game, "{\"type\":\"action\",\"name\":\"command\",\"command\":\"fly\"}", out);
  CHECK(model.unknown() == unknown_before + 1 && model.queued_commands() == 0);
  handle(model, game, "{\"type\":\"action\",\"name\":\"command\",\"command\":\"drop_coin\"}", out);
  CHECK(model.queued_commands() == 1);
  CommandList commands;
  model.take_commands(3, commands);
  CHECK(commands.size() == 1 && commands[0].type == sandbox::kDropCoin && commands[0].player == 3);
  model.take_commands(3, commands);
  CHECK(commands.size() == 1);
  for (std::size_t i = 0; i < UiModel::kMaxQueuedCommands + 3; ++i) {
    handle(model, game, "{\"type\":\"action\",\"name\":\"command\",\"command\":\"drop_coin\"}", out);
  }
  CHECK(model.queued_commands() == UiModel::kMaxQueuedCommands && model.dropped() == 3);

  // Native keys: Esc and P / start.
  UiModel keys(offline.pause, false, false);
  out.count = 0;
  keys.escape(out);
  CHECK(out.count == 1 && out.items[0] == UiRequest::kPause);
  offline.pause.set(true, offline.server.get(), *offline.client, offline.clock);
  out.count = 0;
  keys.escape(out);
  keys.toggle_pause(out);
  CHECK(out.count == 2 && out.items[0] == UiRequest::kResume && out.items[1] == UiRequest::kResume);
  offline.pause.set(false, offline.server.get(), *offline.client, offline.clock);
  handle(keys, game, action("open_settings").c_str(), out);
  out.count = 0;
  keys.toggle_pause(out);  // P does nothing in settings
  CHECK(out.count == 0 && keys.screen() == UiScreen::kSettings);
  keys.escape(out);  // Esc goes back
  CHECK(out.count == 0 && keys.screen() == UiScreen::kPlaying);
  UiModel menu(offline.pause, false, true);
  menu.escape(out);
  menu.toggle_pause(out);
  CHECK(out.count == 0 && menu.screen() == UiScreen::kMain);

  // Online: the pause menu opens over a running game; nothing pauses.
  UiModel online(offline.pause, true, true);
  out.count = 0;
  handle(online, game, action("play").c_str(), out);
  online.mark_started();
  handle(online, game, action("pause").c_str(), out);
  CHECK(out.count == 1 && online.screen() == UiScreen::kPaused && !offline.pause.paused());
  handle(online, game, "{\"type\":\"action\",\"name\":\"command\",\"command\":\"drop_coin\"}", out);
  CHECK(online.queued_commands() == 0);  // in a menu
  handle(online, game, action("resume").c_str(), out);
  CHECK(out.count == 1 && online.screen() == UiScreen::kPlaying);
  online.escape(out);
  CHECK(out.count == 1 && online.screen() == UiScreen::kPaused);
}

void volumes_are_not_clobbered_by_the_duck(test::Context& ctx) {
  OfflinePair offline;
  UiModel model(offline.pause, false, false);
  GameModule game;
  std::string error;
  CHECK(game.use_static(sandbox::game_api(), error));
  Mixer mixer;
  model.apply_volumes(mixer);
  CHECK(mixer.music_gain() == 1.0f && mixer.effects_gain() == 1.0f);  // as without a UI
  UiRequests out;
  handle(model, game, "{\"type\":\"action\",\"name\":\"set_music_volume\",\"value\":0.5}", out);
  handle(model, game, "{\"type\":\"action\",\"name\":\"set_effects_volume\",\"value\":0.25}", out);
  model.apply_volumes(mixer);
  CHECK(mixer.music_gain() == 0.5f && mixer.effects_gain() == 0.25f);
  offline.pause.set(true, offline.server.get(), *offline.client, offline.clock);
  model.apply_volumes(mixer);
  CHECK(mixer.music_gain() == 0.5f * kPauseMusicDuck && mixer.effects_gain() == 0.25f);
  // Changing the volume while paused keeps the duck, and the duck keeps it.
  handle(model, game, "{\"type\":\"action\",\"name\":\"set_music_volume\",\"value\":0.8}", out);
  model.apply_volumes(mixer);
  CHECK(mixer.music_gain() == 0.8f * kPauseMusicDuck);
  offline.pause.set(false, offline.server.get(), *offline.client, offline.clock);
  model.apply_volumes(mixer);
  CHECK(mixer.music_gain() == 0.8f && model.music_volume() == 0.8f);
}

void view_is_posted_only_when_it_changes(test::Context& ctx) {
  GameModule game;
  std::string error;
  CHECK(game.use_static(sandbox::game_api(), error));
  OfflinePair offline;
  UiModel model(offline.pause, false, true);
  headless::HeadlessUi host;
  host.set_present(true);
  UiAppState state;
  state.game = "sandbox";
  state.hud = "coins 0";
  CHECK(model.publish(host, state, game));
  CHECK(host.posts().size() == 1);
  CHECK(host.last_post() ==
        "{\"type\":\"view\",\"screen\":\"main\",\"game\":\"sandbox\",\"hud\":\"coins 0\",\"paused\":false,"
        "\"has_save\":false,\"online\":false,\"music_volume\":1,\"effects_volume\":1,\"notice\":\"\","
        "\"commands\":[\"drop_coin\"]}");
  for (int i = 0; i < 10; ++i) CHECK(!model.publish(host, state, game));
  CHECK(host.posts().size() == 1);

  // Each change posts once.
  state.hud = "coins 1";
  CHECK(model.publish(host, state, game) && !model.publish(host, state, game));
  state.notice = "saved to \"C:\\saves\\quick.sav\"";
  CHECK(model.publish(host, state, game));
  CHECK(host.last_post().find("\"notice\":\"saved to \\\"C:\\\\saves\\\\quick.sav\\\"\"") != std::string::npos);
  state.has_save = true;
  CHECK(model.publish(host, state, game));
  UiRequests out;
  handle(model, game, "{\"type\":\"action\",\"name\":\"set_music_volume\",\"value\":0.75}", out);
  CHECK(model.publish(host, state, game));
  CHECK(host.last_post().find("\"music_volume\":0.75") != std::string::npos);
  model.mark_started();
  CHECK(model.publish(host, state, game));
  CHECK(host.last_post().find("\"screen\":\"playing\"") != std::string::npos);
  offline.pause.set(true, offline.server.get(), *offline.client, offline.clock);
  CHECK(model.publish(host, state, game));
  CHECK(host.last_post().find("\"screen\":\"paused\",") != std::string::npos);
  CHECK(host.last_post().find("\"paused\":true") != std::string::npos);
  CHECK(host.posts().size() == 7 && model.posts() == 7);
  CHECK(!model.publish(host, state, game));

  // Every view the model posts is valid JSON; the "commands" array aside,
  // the reader takes it back.
  std::string flat = host.last_post();
  const std::size_t array = flat.find(",\"commands\":");
  CHECK(array != std::string::npos);
  flat = flat.substr(0, array) + "}";
  JsonObject object;
  CHECK(object.parse(flat.data(), flat.size()));
  CHECK(object.string("notice") != nullptr && std::string(object.string("notice")) == state.notice);

  // A host that cannot post now is asked again next frame.
  headless::HeadlessUi gone;
  UiModel other(offline.pause, false, true);
  CHECK(!other.publish(gone, state, game));
  gone.set_present(true);
  CHECK(other.publish(gone, state, game));
}

void malformed_messages_are_counted_not_trusted(test::Context& ctx) {
  GameModule game;
  std::string error;
  CHECK(game.use_static(sandbox::game_api(), error));
  OfflinePair offline;
  UiModel model(offline.pause, false, true);
  headless::HeadlessUi host;
  host.set_present(true);
  CHECK(host.inject("garbage"));
  CHECK(host.inject("{\"type\":\"action\",\"name\":\"play\",}"));
  CHECK(host.inject("{\"type\":\"action\",\"name\":\"teleport\"}"));
  CHECK(host.inject("{\"type\":\"cheat\",\"name\":\"play\"}"));
  CHECK(!host.inject(std::string(UiMessageList::kMaxMessageBytes + 1, ' ').c_str()));  // too long: dropped
  CHECK(host.inject(action("play").c_str()));
  UiRequests out;
  model.receive(host, game, out);
  CHECK(model.malformed() == 2 && model.unknown() == 2 && model.dropped() == 1);
  CHECK(out.count == 1 && out.items[0] == UiRequest::kPlay);  // the good one still counts
  out.count = 0;
  model.receive(host, game, out);  // nothing new
  CHECK(out.count == 0 && model.malformed() == 2);
  // More messages in one frame than fit: the first ones are kept.
  for (std::size_t i = 0; i < UiMessageList::kMaxMessages + 4; ++i) host.inject("x");
  model.receive(host, game, out);
  CHECK(model.malformed() == 2 + UiMessageList::kMaxMessages && model.dropped() == 5);
}

// ------------------------------------------------------- the client app

std::string work_dir(const char* name) {
  const std::filesystem::path dir = std::filesystem::u8path(SCRAPS_TEST_WORK_DIR) / "ui" / name;
  std::error_code error;
  std::filesystem::remove_all(dir, error);
  std::filesystem::create_directories(dir, error);
  return dir.u8string() + "/";
}

// The client app on the headless backend, with or without a UI, driven at
// 60 simulated frames per second.
struct App {
  App(bool with_ui, const std::string& save_dir, const char* scene = "top-down") {
    backend.ui().set_present(with_ui);
    ClientOptions options;
    options.scene = scene;
    options.scene_dir = SCRAPS_SANDBOX_SCENE_DIR;
    options.headless = true;
    options.threads = 0;
    options.save_dir = save_dir;
    app = std::make_unique<ClientApp>(options, backend.services(), nullptr);
  }
  bool start() {
    std::string error;
    const bool ok = app->start(error);
    if (!ok) std::printf("    start: %s\n", error.c_str());
    return ok;
  }
  bool frames(int count) {
    for (int i = 0; i < count; ++i) {
      now += 1.0 / 60.0;
      if (!app->frame(now)) return false;
    }
    return true;
  }
  bool action(const char* text) { return backend.ui().inject(text); }
  bool playing() const {
    return find_entity(app->client().current_world(), app->client().local_entity()) != nullptr;
  }
  const std::string& view() { return backend.ui().last_post(); }

  headless::HeadlessBackend backend;
  std::unique_ptr<ClientApp> app;
  double now = 0.0;
};

bool sandbox_is_the_selected_game(test::Context& ctx) {
  // ClientApp runs the game this build selected (SCRAPS_GAME); these tests
  // speak the sandbox's commands.
  const bool sandbox = std::string(selected_game().name) == "sandbox";
  CHECK(true);
  if (!sandbox) std::printf("    skipped: SCRAPS_GAME is not sandbox\n");
  return sandbox;
}

void client_app_main_menu_play_pause_save_resume_quit(test::Context& ctx) {
  if (!sandbox_is_the_selected_game(ctx)) return;
  const auto run = std::make_unique<App>(true, work_dir("flow"));
  App& app = *run;
  CHECK(app.start());
  CHECK(app.app->has_ui());

  // The main menu: nothing runs, not even the network.
  CHECK(app.frames(30));
  CHECK(app.app->ui().screen() == UiScreen::kMain);
  CHECK(app.app->stats().ticks == 0 && app.app->client().state() == ClientState::kConnecting);
  CHECK(!app.app->client().has_world());
  CHECK(app.backend.ui().posts().size() == 1);  // the first view, then nothing changed
  CHECK(app.view().find("\"screen\":\"main\"") != std::string::npos);
  CHECK(app.view().find("\"game\":\"sandbox\"") != std::string::npos);
  CHECK(app.view().find("\"has_save\":false") != std::string::npos);
  CHECK(app.view().find("\"commands\":[\"drop_coin\"]") != std::string::npos);

  // Play: ticks start and the player joins.
  CHECK(app.action("{\"type\":\"action\",\"name\":\"play\"}"));
  CHECK(app.frames(60));
  CHECK(app.app->ui().screen() == UiScreen::kPlaying);
  CHECK(app.app->stats().ticks > 30);
  CHECK(app.playing());
  CHECK(app.view().find("\"screen\":\"playing\"") != std::string::npos);
  CHECK(app.view().find("\"hud\":\"coins 0\"") != std::string::npos);

  // A game command from the UI is sent at the next tick.
  CHECK(app.app->stats().ui_commands == 0);
  CHECK(app.action("{\"type\":\"action\",\"name\":\"command\",\"command\":\"drop_coin\"}"));
  CHECK(app.frames(2));  // at least one tick
  CHECK(app.app->stats().ui_commands == 1);
  CHECK(app.frames(5));
  CHECK(app.app->stats().ui_commands == 1);

  // Pause: the pause menu, no ticks.
  CHECK(app.action("{\"type\":\"action\",\"name\":\"pause\"}"));
  CHECK(app.frames(1));
  CHECK(app.app->paused() && app.app->ui().screen() == UiScreen::kPaused);
  CHECK(app.view().find("\"screen\":\"paused\"") != std::string::npos);
  CHECK(app.view().find("\"paused\":true") != std::string::npos);
  const long ticks = app.app->stats().ticks;
  CHECK(app.frames(30));
  CHECK(app.app->stats().ticks == ticks);

  // Save, from the pause menu: the same code as F5.
  CHECK(app.action("{\"type\":\"action\",\"name\":\"save\"}"));
  CHECK(app.frames(1));
  CHECK(app.app->stats().saves == 1 && app.app->stats().save_errors == 0);
  CHECK(app.view().find("\"has_save\":true") != std::string::npos);
  CHECK(app.view().find("\"notice\":\"saved to ") != std::string::npos);
  CHECK(app.app->paused());  // saving does not resume

  // F5 in a menu does nothing: the menus own the keyboard.
  app.backend.input().press(Key::kF5);
  CHECK(app.frames(1));
  app.backend.input().release(Key::kF5);
  CHECK(app.app->stats().saves == 1);

  // Resume: ticks again.
  CHECK(app.action("{\"type\":\"action\",\"name\":\"resume\"}"));
  CHECK(app.frames(30));
  CHECK(!app.app->paused() && app.app->ui().screen() == UiScreen::kPlaying);
  CHECK(app.app->stats().ticks > ticks + 20);

  // Esc opens the pause menu, Esc again closes it (it no longer quits).
  app.backend.input().press(Key::kEscape);
  CHECK(app.frames(1));
  app.backend.input().release(Key::kEscape);
  CHECK(app.app->paused());
  app.backend.input().press(Key::kEscape);
  CHECK(app.frames(1));
  app.backend.input().release(Key::kEscape);
  CHECK(!app.app->paused());

  // Quit.
  CHECK(app.action("{\"type\":\"action\",\"name\":\"quit\"}"));
  CHECK(!app.frames(1));
  CHECK(app.app->finish() == 0);
  CHECK(app.app->ui().malformed() == 0 && app.app->ui().unknown() == 0);
}

void client_app_continue_loads_the_quick_save(test::Context& ctx) {
  if (!sandbox_is_the_selected_game(ctx)) return;
  const std::string saves = work_dir("continue");
  {
    // No save yet: "continue" fails and the main menu stays.
    const auto run = std::make_unique<App>(true, saves);
    CHECK(run->start());
    CHECK(run->action("{\"type\":\"action\",\"name\":\"continue\"}"));
    CHECK(run->frames(5));
    CHECK(run->app->ui().screen() == UiScreen::kMain && run->app->stats().save_errors == 1);
    CHECK(run->view().find("\"notice\":\"load failed: ") != std::string::npos);
    // Play, then save with F5 while playing.
    CHECK(run->action("{\"type\":\"action\",\"name\":\"play\"}"));
    CHECK(run->frames(60));
    run->backend.input().press(Key::kF5);
    CHECK(run->frames(1));
    run->backend.input().release(Key::kF5);
    CHECK(run->app->stats().saves == 1);
    CHECK(run->app->finish() == 0);
  }
  const auto run = std::make_unique<App>(true, saves);
  CHECK(run->start());
  CHECK(run->frames(1));
  CHECK(run->view().find("\"has_save\":true") != std::string::npos);
  CHECK(run->action("{\"type\":\"action\",\"name\":\"continue\"}"));
  CHECK(run->frames(60));
  CHECK(run->app->stats().loads == 1 && run->app->ui().screen() == UiScreen::kPlaying);
  CHECK(run->playing());
  CHECK(run->app->client().current_world().tick > 60);  // the saved tick, and on from there
}

void client_app_volumes_follow_the_ui(test::Context& ctx) {
  if (!sandbox_is_the_selected_game(ctx)) return;
  const auto run = std::make_unique<App>(true, work_dir("volumes"));
  App& app = *run;
  CHECK(app.start());
  CHECK(app.action("{\"type\":\"action\",\"name\":\"play\"}"));
  CHECK(app.action("{\"type\":\"action\",\"name\":\"set_music_volume\",\"value\":0.5}"));
  CHECK(app.action("{\"type\":\"action\",\"name\":\"set_effects_volume\",\"value\":0.25}"));
  CHECK(app.frames(10));
  CHECK(app.app->mixer().music_gain() == 0.5f && app.app->mixer().effects_gain() == 0.25f);
  app.backend.input().press(Key::kP);  // P opens the pause menu, which pauses
  CHECK(app.frames(1));
  app.backend.input().release(Key::kP);
  CHECK(app.app->paused());
  CHECK(app.app->mixer().music_gain() == 0.5f * kPauseMusicDuck && app.app->mixer().effects_gain() == 0.25f);
  CHECK(app.action("{\"type\":\"action\",\"name\":\"resume\"}"));
  CHECK(app.frames(1));
  CHECK(app.app->mixer().music_gain() == 0.5f);
  CHECK(app.view().find("\"music_volume\":0.5,\"effects_volume\":0.25") != std::string::npos);
}

// Presses, clicks and pad buttons while the game cannot act on them.
void press_everything(headless::HeadlessInput& input) {
  input.connect_gamepad(true);
  input.move_pointer(700.0f, 400.0f);
  input.press(Key::kSpace);
  input.press(Key::kE);
  input.press(MouseButton::kLeft);
  input.press(GamepadButton::kSouth);
  input.press(GamepadButton::kWest);
}
void release_everything(headless::HeadlessInput& input) {
  input.release(Key::kSpace);
  input.release(Key::kE);
  input.release(MouseButton::kLeft);
  input.release(GamepadButton::kSouth);
  input.release(GamepadButton::kWest);
}

void input_while_paused_or_in_a_menu_never_acts(test::Context& ctx) {
  if (!sandbox_is_the_selected_game(ctx)) return;
  // The control: playing, the same presses do send commands.
  {
    const auto run = std::make_unique<App>(false, work_dir("control"));
    CHECK(run->start() && run->frames(60) && run->playing());
    const long before = run->app->stats().game_commands;
    press_everything(run->backend.input());
    CHECK(run->frames(1));
    release_everything(run->backend.input());
    CHECK(run->frames(10));
    CHECK(run->app->stats().game_commands > before);
  }
  // Without a UI, paused with P: nothing after resuming.
  {
    const auto run = std::make_unique<App>(false, work_dir("paused"));
    App& app = *run;
    CHECK(app.start() && app.frames(60) && app.playing());
    app.backend.input().press(Key::kP);
    CHECK(app.frames(1));
    app.backend.input().release(Key::kP);
    CHECK(app.app->paused());
    const long before = app.app->stats().game_commands;
    press_everything(app.backend.input());
    CHECK(app.frames(1));
    release_everything(app.backend.input());
    CHECK(app.frames(5));
    app.backend.input().press(Key::kP);
    CHECK(app.frames(1));
    app.backend.input().release(Key::kP);
    CHECK(!app.app->paused());
    CHECK(app.frames(30));
    CHECK(app.app->stats().game_commands == before);
    // Pressed in the same frame as the pause: dropped at the resume too.
    app.backend.input().press(Key::kP);
    press_everything(app.backend.input());
    CHECK(app.frames(1));
    app.backend.input().release(Key::kP);
    release_everything(app.backend.input());
    CHECK(app.app->paused());
    app.backend.input().press(Key::kP);
    CHECK(app.frames(1));
    app.backend.input().release(Key::kP);
    CHECK(app.frames(30));
    CHECK(app.app->stats().game_commands == before);
  }
  // With a UI: on the main menu, in the pause menu and in settings.
  {
    const auto run = std::make_unique<App>(true, work_dir("menus"));
    App& app = *run;
    CHECK(app.start());
    press_everything(app.backend.input());
    CHECK(app.frames(1));
    release_everything(app.backend.input());
    CHECK(app.action("{\"type\":\"action\",\"name\":\"play\"}"));
    CHECK(app.frames(60) && app.playing());
    CHECK(app.app->stats().game_commands == 0);
    for (const char* menu : {"pause", "open_settings"}) {
      CHECK(app.action((std::string("{\"type\":\"action\",\"name\":\"") + menu + "\"}").c_str()));
      CHECK(app.frames(1));
      CHECK(app.app->ui().screen() != UiScreen::kPlaying);
      press_everything(app.backend.input());
      CHECK(app.frames(1));
      release_everything(app.backend.input());
      CHECK(app.frames(2));
      CHECK(app.action(std::strcmp(menu, "pause") == 0 ? "{\"type\":\"action\",\"name\":\"resume\"}"
                                                         : "{\"type\":\"action\",\"name\":\"close_settings\"}"));
      CHECK(app.frames(30));
      CHECK(app.app->ui().screen() == UiScreen::kPlaying);
    }
    CHECK(app.app->stats().game_commands == 0);
  }
}

void client_app_without_a_ui_plays_at_once(test::Context& ctx) {
  if (!sandbox_is_the_selected_game(ctx)) return;
  const auto run = std::make_unique<App>(false, work_dir("no_ui"));
  App& app = *run;
  CHECK(app.start());
  CHECK(!app.app->has_ui());
  CHECK(app.frames(60));
  CHECK(app.playing() && app.app->stats().ticks > 30);
  CHECK(app.backend.ui().posts().empty());
  CHECK(app.app->mixer().music_gain() == 1.0f && app.app->mixer().effects_gain() == 1.0f);
  // F9 resumes a paused game, as before.
  app.backend.input().press(Key::kF5);
  CHECK(app.frames(1));
  app.backend.input().release(Key::kF5);
  app.backend.input().press(Key::kP);
  CHECK(app.frames(1));
  app.backend.input().release(Key::kP);
  CHECK(app.app->paused());
  app.backend.input().press(Key::kF9);
  CHECK(app.frames(1));
  app.backend.input().release(Key::kF9);
  CHECK(!app.app->paused() && app.app->stats().loads == 1);
  // Esc quits.
  app.backend.input().press(Key::kEscape);
  CHECK(!app.frames(1));
  CHECK(app.app->finish() == 0);
}

}  // namespace

void register_ui_tests(test::Registry& registry) {
  registry.add("platform.event_list_keeps_order_and_counts_overflow", event_list_keeps_order_and_counts_overflow);
  registry.add("platform.text_input_is_split_and_checked", text_input_is_split_and_checked);
  registry.add("ui.json_reader_takes_flat_objects", json_reader_takes_flat_objects);
  registry.add("ui.json_reader_refuses_hostile_input", json_reader_refuses_hostile_input);
  registry.add("ui.json_writer_escapes_and_reads_back", json_writer_escapes_and_reads_back);
  registry.add("ui.actions_parse_and_odd_ones_are_refused", actions_parse_and_odd_ones_are_refused);
  registry.add("ui.commands_are_checked_and_copied", ui_commands_are_checked_and_copied);
  registry.add("ui.each_action_does_what_it_says", each_action_does_what_it_says);
  registry.add("ui.volumes_are_not_clobbered_by_the_duck", volumes_are_not_clobbered_by_the_duck);
  registry.add("ui.view_is_posted_only_when_it_changes", view_is_posted_only_when_it_changes);
  registry.add("ui.malformed_messages_are_counted_not_trusted", malformed_messages_are_counted_not_trusted);
  registry.add("ui.client_app_main_menu_play_pause_save_resume_quit", client_app_main_menu_play_pause_save_resume_quit);
  registry.add("ui.client_app_continue_loads_the_quick_save", client_app_continue_loads_the_quick_save);
  registry.add("ui.client_app_volumes_follow_the_ui", client_app_volumes_follow_the_ui);
  registry.add("ui.input_while_paused_or_in_a_menu_never_acts", input_while_paused_or_in_a_menu_never_acts);
  registry.add("ui.client_app_without_a_ui_plays_at_once", client_app_without_a_ui_plays_at_once);
}
