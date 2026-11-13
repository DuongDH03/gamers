// The webview2 backend's window-independent half (docs/specs/2026-11-14-
// offline-essentials-and-ui.md, part 6, layout A): the shared-buffer frame
// format and the batch renderer that writes it, the page's input messages,
// the UiHost link and the frame flow control; and the rule that Esc belongs
// to the page while P and the gamepad's start stay native, so no single press
// toggles the pause menu twice.
//
// The frame written by webview2.frame_round_trips is also decoded by
// renderer.js in Node (tests/webview2_renderer.test.js, ctest
// webview2_renderer_js): the same bytes, both decoders.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "backends/headless/headless_backend.h"
#include "backends/webview2/batch_renderer.h"
#include "backends/webview2/frame_format.h"
#include "backends/webview2/page_input.h"
#include "backends/webview2/page_link.h"
#include "client/client_app.h"
#include "common/ui_model.h"
#include "harness.h"
#include "selected_game.h"

namespace {

using namespace scraps;
using namespace scraps::webview2;

// Takes every frame (or none), keeping a copy of each.
class RecordingSink final : public FrameSink {
 public:
  bool can_take_frame() const override { return accept; }
  bool take_frame(const std::uint8_t* bytes, std::size_t size, std::uint32_t seq) override {
    if (!accept) return false;
    frames.emplace_back(bytes, bytes + size);
    seqs.push_back(seq);
    return true;
  }
  bool accept = true;
  std::vector<std::vector<std::uint8_t>> frames;
  std::vector<std::uint32_t> seqs;
};

bool same_vertex(const Vertex2D& a, const Vertex2D& b) {
  return a.x == b.x && a.y == b.y && a.color == b.color && a.u == b.u && a.v == b.v;
}

std::string fixture_dir() {
  const std::filesystem::path dir = std::filesystem::u8path(SCRAPS_TEST_WORK_DIR) / "webview2";
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  return dir.u8string();
}

// ------------------------------------------------------------ the frame format

void frame_round_trips(test::Context& ctx) {
  RecordingSink sink;
  BatchRenderer renderer(&sink);
  renderer.set_output_size(640, 360);
  const std::uint8_t pixels[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128};
  const TextureId texture = renderer.create_texture(2, 2, pixels);
  CHECK(texture == 1);
  CHECK(renderer.create_texture(0, 2, pixels) == kNoTexture);
  CHECK(renderer.create_texture(2, 2, nullptr) == kNoTexture);
  CHECK(renderer.create_texture(20000, 2, pixels) == kNoTexture);

  const Vertex2D a(10.5f, 20.25f, Color(1, 2, 3, 4), 0.0f, 0.0f);
  const Vertex2D b(-3.0f, 1e6f, Color(255, 128, 0, 255), 1.0f, 0.5f);
  const Vertex2D c(100.0f, 200.0f, Color(9, 8, 7, 6), 0.25f, 1.0f);
  const Vertex2D tri[3] = {a, b, c};

  renderer.begin_frame(Color(10, 20, 30, 255));
  RectF rect;
  rect.x = 1.0f;
  rect.y = 2.0f;
  rect.w = 30.0f;
  rect.h = 40.0f;
  renderer.fill_rect(rect, Color(0, 0, 0, 150));            // 6 flat vertices
  renderer.fill_triangles(tri, 3, texture);                  // a textured batch...
  renderer.fill_triangles(tri, 3, texture);                  // ...merged with this one
  renderer.fill_triangles(tri, 4, kNoTexture);               // 4 -> 3 vertices, flat
  renderer.fill_triangles(tri, 3, 99);                       // no such texture: flat, merged
  renderer.draw_line(0.0f, 0.0f, 5.0f, 5.0f, Color(1, 1, 1));
  renderer.draw_line(5.0f, 5.0f, 9.0f, 0.0f, Color(2, 2, 2));  // merged: 4 vertices
  renderer.draw_debug_text(10.0f, 10.0f, "Hi ", Color(240, 240, 240));  // the font texture; ' ' is blank
  renderer.end_frame();

  CHECK(sink.frames.size() == 1);
  CHECK(renderer.frames_sent() == 1 && renderer.queued_resource_bytes() == 0);
  if (sink.frames.size() != 1) return;
  const std::vector<std::uint8_t>& bytes = sink.frames[0];
  DecodedFrame frame;
  CHECK(decode_frame(bytes.data(), bytes.size(), frame));
  CHECK(frame.seq == 1 && sink.seqs[0] == 1);
  CHECK(frame.total == bytes.size() && frame.width == 640 && frame.height == 360);
  CHECK(frame.commands.size() == 8);
  if (frame.commands.size() != 8) return;
  // Resources first: both textures (the font was made by draw_debug_text).
  CHECK(frame.commands[0].op == FrameOp::kTexture && frame.commands[0].texture == 1);
  CHECK(frame.commands[0].width == 2 && frame.commands[0].height == 2);
  CHECK(std::memcmp(bytes.data() + frame.commands[0].offset, pixels, sizeof(pixels)) == 0);
  CHECK(frame.commands[1].op == FrameOp::kTexture && frame.commands[1].texture == 2);
  CHECK(frame.commands[1].width == 140 && frame.commands[1].height == 140);  // SDL's atlas layout
  CHECK(frame.commands[1].offset + 140 * 140 * 4 == frame.draw_offset);
  // Then the draws, in painter's order.
  CHECK(frame.commands[2].op == FrameOp::kClear && unpack_rgba(frame.commands[2].rgba) == Color(10, 20, 30, 255));
  CHECK(frame.commands[3].op == FrameOp::kTriangles && frame.commands[3].texture == 0 && frame.commands[3].count == 6);
  CHECK(frame.commands[4].op == FrameOp::kTriangles && frame.commands[4].texture == 1 && frame.commands[4].count == 6);
  CHECK(frame.commands[5].op == FrameOp::kTriangles && frame.commands[5].texture == 0 && frame.commands[5].count == 6);
  CHECK(frame.commands[6].op == FrameOp::kLines && frame.commands[6].count == 4);
  CHECK(frame.commands[7].op == FrameOp::kTriangles && frame.commands[7].texture == 2 && frame.commands[7].count == 12);

  // The vertices come back exactly.
  const std::uint8_t* rect_vertices = bytes.data() + frame.commands[3].offset;
  CHECK(same_vertex(read_vertex(rect_vertices), Vertex2D(1.0f, 2.0f, Color(0, 0, 0, 150))));
  CHECK(same_vertex(read_vertex(rect_vertices + 2 * kVertexBytes), Vertex2D(31.0f, 42.0f, Color(0, 0, 0, 150))));
  const std::uint8_t* textured = bytes.data() + frame.commands[4].offset;
  for (int i = 0; i < 6; ++i) CHECK(same_vertex(read_vertex(textured + i * kVertexBytes), tri[i % 3]));
  const std::uint8_t* lines = bytes.data() + frame.commands[6].offset;
  CHECK(same_vertex(read_vertex(lines + 3 * kVertexBytes), Vertex2D(9.0f, 0.0f, Color(2, 2, 2))));
  // 'H' is glyph 'H' - 33 = 39: column 11, row 2 of the atlas, from (111, 21).
  const Vertex2D h = read_vertex(bytes.data() + frame.commands[7].offset);
  CHECK(h.x == 10.0f && h.y == 10.0f && h.color == Color(240, 240, 240));
  CHECK_NEAR(h.u, 111.0 / 140.0, 1e-6);
  CHECK_NEAR(h.v, 21.0 / 140.0, 1e-6);
  const Vertex2D i_glyph = read_vertex(bytes.data() + frame.commands[7].offset + 6 * kVertexBytes);
  CHECK(i_glyph.x == 18.0f);  // 8 pixels on

  // For renderer.js's test: the same bytes, and what they must decode to.
  const std::string dir = fixture_dir();
  std::ofstream bin(std::filesystem::u8path(dir) / "frame.bin", std::ios::binary | std::ios::trunc);
  bin.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  std::ofstream json(std::filesystem::u8path(dir) / "frame.json", std::ios::trunc);
  json << "{\"seq\":" << frame.seq << ",\"total\":" << frame.total << ",\"width\":" << frame.width
       << ",\"height\":" << frame.height << ",\"drawOffset\":" << frame.draw_offset << ",\"commands\":[";
  for (std::size_t k = 0; k < frame.commands.size(); ++k) {
    const DecodedCommand& command = frame.commands[k];
    json << (k > 0 ? "," : "") << "{\"op\":" << static_cast<unsigned>(command.op)
         << ",\"texture\":" << command.texture << ",\"width\":" << command.width << ",\"height\":" << command.height
         << ",\"rgba\":" << command.rgba << ",\"count\":" << command.count << ",\"offset\":" << command.offset << "}";
  }
  json << "],\"firstTexturedVertex\":{\"x\":" << tri[0].x << ",\"y\":" << tri[0].y << ",\"r\":"
       << static_cast<int>(tri[0].color.r) << ",\"g\":" << static_cast<int>(tri[0].color.g)
       << ",\"b\":" << static_cast<int>(tri[0].color.b) << ",\"a\":" << static_cast<int>(tri[0].color.a)
       << ",\"u\":" << tri[0].u << ",\"v\":" << tri[0].v << "}}\n";
  CHECK(bin.good() && json.good());
}

void textures_travel_once_and_again_after_a_reload(test::Context& ctx) {
  RecordingSink sink;
  BatchRenderer renderer(&sink);
  const std::uint8_t pixels[4] = {1, 2, 3, 4};
  const TextureId first = renderer.create_texture(1, 1, pixels);
  const TextureId second = renderer.create_texture(1, 1, pixels);
  DecodedFrame frame;

  // The page cannot take a frame: nothing is recorded, the textures wait.
  sink.accept = false;
  renderer.begin_frame(Color());
  renderer.fill_rect(RectF(), Color());
  renderer.end_frame();
  CHECK(sink.frames.empty() && renderer.frames_dropped() == 1 && renderer.queued_resource_bytes() > 0);

  // They go with the first frame the page takes, and only with it.
  sink.accept = true;
  renderer.begin_frame(Color());
  renderer.end_frame();
  renderer.begin_frame(Color());
  renderer.end_frame();
  CHECK(sink.frames.size() == 2);
  if (sink.frames.size() != 2) return;
  CHECK(decode_frame(sink.frames[0].data(), sink.frames[0].size(), frame) && frame.commands.size() == 3);
  CHECK(frame.seq == 1);
  CHECK(decode_frame(sink.frames[1].data(), sink.frames[1].size(), frame) && frame.commands.size() == 1);
  CHECK(frame.seq == 2 && frame.draw_offset == kFrameHeaderBytes);

  // Destroyed: a destroy command; drawing with it afterwards draws flat.
  renderer.destroy_texture(first);
  renderer.destroy_texture(first);  // twice: once is enough
  renderer.begin_frame(Color());
  const Vertex2D tri[3];
  renderer.fill_triangles(tri, 3, first);
  renderer.end_frame();
  CHECK(decode_frame(sink.frames[2].data(), sink.frames[2].size(), frame) && frame.commands.size() == 3);
  CHECK(frame.commands[0].op == FrameOp::kDestroyTexture && frame.commands[0].texture == first);
  CHECK(frame.commands[2].op == FrameOp::kTriangles && frame.commands[2].texture == kNoTexture);

  // A reloaded page has nothing: every live texture again, and the dead id.
  renderer.resend_textures();
  renderer.begin_frame(Color());
  renderer.end_frame();
  CHECK(decode_frame(sink.frames[3].data(), sink.frames[3].size(), frame) && frame.commands.size() == 3);
  CHECK(frame.commands[0].op == FrameOp::kDestroyTexture && frame.commands[0].texture == first);
  CHECK(frame.commands[1].op == FrameOp::kTexture && frame.commands[1].texture == second);

  // Without a sink nothing breaks.
  BatchRenderer alone(nullptr);
  alone.begin_frame(Color());
  alone.fill_rect(RectF(), Color());
  alone.draw_debug_text(0.0f, 0.0f, "x", Color());
  alone.end_frame();
  CHECK(alone.frames_dropped() == 1);
}

void decoder_refuses_hostile_frames(test::Context& ctx) {
  RecordingSink sink;
  BatchRenderer renderer(&sink);
  const std::uint8_t pixels[16] = {};
  renderer.create_texture(2, 2, pixels);
  renderer.begin_frame(Color(1, 2, 3));
  const Vertex2D tri[3];
  renderer.fill_triangles(tri, 3, 1);
  renderer.draw_line(0, 0, 1, 1, Color());
  renderer.end_frame();
  const std::vector<std::uint8_t> good = sink.frames.at(0);
  DecodedFrame frame;
  CHECK(decode_frame(good.data(), good.size(), frame));

  auto refused = [&](std::vector<std::uint8_t> bytes) { return !decode_frame(bytes.data(), bytes.size(), frame); };
  auto with_u32 = [&](std::size_t at, std::uint32_t value) {
    std::vector<std::uint8_t> bytes = good;
    store_u32(bytes.data() + at, value);
    return bytes;
  };
  CHECK(!decode_frame(nullptr, 100, frame));
  CHECK(!decode_frame(good.data(), 31, frame));
  CHECK(refused(with_u32(0, 0x12345678u)));            // magic
  CHECK(refused(with_u32(4, 2)));                      // version
  CHECK(refused(with_u32(12, static_cast<std::uint32_t>(good.size() + 4))));  // total past the bytes
  CHECK(refused(with_u32(12, static_cast<std::uint32_t>(good.size() - 2))));  // unaligned total
  CHECK(refused(with_u32(12, 8)));                     // total under the header
  CHECK(refused(with_u32(24, 4)));                     // draw offset in the header
  CHECK(refused(with_u32(24, static_cast<std::uint32_t>(good.size() + 4))));  // past the end
  CHECK(refused(with_u32(24, kFrameHeaderBytes)));     // the texture would be a draw
  CHECK(refused(with_u32(kFrameHeaderBytes, 77)));     // unknown op
  CHECK(refused(with_u32(kFrameHeaderBytes + 8, 0)));  // texture 0 wide
  CHECK(refused(with_u32(kFrameHeaderBytes + 8, 20000)));
  CHECK(refused(with_u32(kFrameHeaderBytes + 12, 0xFFFFFFFFu)));  // pixels past the end
  const std::size_t draws = kFrameHeaderBytes + 16 + 16;
  CHECK(refused(with_u32(draws + 8 + 8, 0xFFFFFFFFu)));  // vertex count overflow
  CHECK(refused(with_u32(draws + 8 + 8, 4)));            // not a multiple of 3
  std::vector<std::uint8_t> cut = good;
  cut.resize(good.size() - 4);
  CHECK(!decode_frame(cut.data(), cut.size(), frame));

  // Random damage: never a crash, never a read past the end (the sanitizer-
  // free check: every accepted frame's commands lie inside it).
  std::mt19937 random(1234);
  int accepted = 0;
  for (int round = 0; round < 20000; ++round) {
    std::vector<std::uint8_t> bytes = good;
    const int flips = 1 + static_cast<int>(random() % 4);
    for (int f = 0; f < flips; ++f) bytes[random() % bytes.size()] = static_cast<std::uint8_t>(random());
    if (random() % 8 == 0) bytes.resize(random() % (bytes.size() + 1));
    if (decode_frame(bytes.data(), bytes.size(), frame)) {
      ++accepted;
      bool inside = frame.total <= bytes.size();
      for (const DecodedCommand& command : frame.commands) {
        inside = inside && command.offset <= frame.total;
      }
      CHECK(inside);
    }
  }
  CHECK(accepted > 0);  // some flips land in vertex data, which is any float
}

// ------------------------------------------------------------ page input

void page_input_takes_messages_and_refuses_hostile_ones(test::Context& ctx) {
  PageInput input;
  auto queue = [&](const char* text) { return input.queue(text, std::strlen(text)); };
  CHECK(queue("kd KeyW 0"));
  CHECK(queue("kd KeyW 1"));  // the OS repeating it
  CHECK(queue("pm 10.5 20.25"));
  CHECK(queue("pm 11 21"));  // merged with the move before
  CHECK(queue("pd 0 12 22"));
  CHECK(queue("wh -1.5"));
  CHECK(queue("kd Space 0"));
  input.pump(true);
  CHECK(input.is_down(Key::kW) && input.was_pressed(Key::kW));
  CHECK(input.is_down(Key::kSpace) && input.was_pressed(Key::kSpace));
  CHECK(input.pointer().x == 12.0f && input.pointer().y == 22.0f && input.pointer_inside());
  CHECK(input.was_pressed(MouseButton::kLeft) && input.is_down(MouseButton::kLeft));
  CHECK(input.wheel() == -1.5f);
  CHECK(input.events().size() == 6);
  CHECK(input.events()[1].repeat && input.events()[2].type == InputEventType::kPointerMove);
  CHECK(input.events()[2].x == 11.0f);

  // Next frame: held, not pressed again.
  CHECK(queue("ku KeyW"));
  CHECK(queue("pu 0 13 23"));
  input.pump(true);
  CHECK(!input.is_down(Key::kW) && !input.was_pressed(Key::kSpace) && input.is_down(Key::kSpace));
  CHECK(input.was_released(MouseButton::kLeft) && !input.is_down(MouseButton::kLeft));
  CHECK(input.wheel() == 0.0f);

  // Text only while a text field asked for it, whole code points.
  CHECK(queue("tx a"));
  input.pump(true);
  CHECK(input.events().empty());
  input.start_text_input();
  CHECK(queue("tx \xC3\xA9\xE2\x82\xAC"));  // é€
  input.pump(true);
  CHECK(input.events().size() == 1 && std::strcmp(input.events()[0].text.data(), "\xC3\xA9\xE2\x82\xAC") == 0);
  std::string long_text = "tx ";
  for (int k = 0; k < 40; ++k) long_text += "\xE2\x82\xAC";  // 120 bytes: four events
  CHECK(input.queue(long_text.data(), long_text.size()));
  input.pump(true);
  CHECK(input.events().size() == 4);

  // Esc is the page's: refused like an unknown key, so the client's native
  // Esc path can never fire in scraps_web.
  const std::size_t before = input.rejected();
  CHECK(!queue("kd Escape 0"));
  CHECK(!queue("ku Escape"));
  CHECK(key_from_dom_code("Escape", 6) == Key::kUnknown);
  input.pump(true);
  CHECK(!input.is_down(Key::kEscape) && !input.was_pressed(Key::kEscape) && input.events().empty());
  CHECK(input.rejected() == before + 2);

  // Hostile messages: refused whole, counted, nothing changes.
  const char* hostile[] = {
      "",          "k",           "kd",          "kd ",         "kd KeyW",      "kd KeyW 2",   "kd KeyW 0 0",
      "kd  KeyW 0", "kd KeyW  0", " kd KeyW 0",  "kd KeyW 0 ",  "kd keyw 0",    "kd Key 0",    "kd KeyWW 0",
      "ku",        "ku KeyW 0",   "pm 1",        "pm 1 2 3",    "pm nan 1",     "pm 1 inf",    "pm 1e9 1",
      "pm -1e7 0", "pm 0x10 1",   "pm 1,5 2",    "pm +1 2",     "pd 3 1 1",     "pd -1 1 1",   "pd 0 1",
      "pd 0 nan 1", "pu 9 1 1",   "wh",          "wh 1000",     "wh nan",       "wh 1 2",      "pe",
      "pe 2",      "pe 1 1",      "bl 1",        "xx 1",        "tx ",          "tx \xC3",     "tx \xFF\xFE",
      "tx a\xED\xA0\x80",  // a surrogate in UTF-8
      "rf 1 2 3 4",  // not an input message
  };
  const std::size_t rejected = input.rejected();
  for (const char* text : hostile) CHECK(!queue(text));
  std::string huge(5000, '1');
  huge = "pm " + huge + " 1";
  CHECK(!input.queue(huge.data(), huge.size()));
  std::string long_tx = "tx " + std::string(300, 'a');
  CHECK(!input.queue(long_tx.data(), long_tx.size()));
  CHECK(!input.queue(nullptr, 5));
  CHECK(!input.queue("kd K\0yW 0", 9));  // a '\0' inside
  CHECK(input.rejected() == rejected + sizeof(hostile) / sizeof(hostile[0]) + 4);
  input.pump(true);
  CHECK(input.events().empty() && !input.was_pressed(MouseButton::kLeft));

  // Random bytes: never a crash.
  std::mt19937 random(77);
  for (int round = 0; round < 20000; ++round) {
    char text[24];
    const std::size_t size = random() % sizeof(text);
    for (std::size_t k = 0; k < size; ++k) text[k] = "kdutxpmwhebl 0123456789.-KeyWEscape"[random() % 36];
    input.queue(text, size);
    if (round % 100 == 0) input.pump(true);
  }
  input.pump(true);

  // Focus lost: everything let go of, without "up" events.
  CHECK(queue("kd KeyD 0"));
  CHECK(queue("pd 2 5 5"));
  input.pump(true);
  CHECK(input.is_down(Key::kD) && input.is_down(MouseButton::kRight));
  CHECK(queue("bl"));
  input.pump(true);
  CHECK(!input.is_down(Key::kD) && !input.is_down(MouseButton::kRight) && !input.pointer_inside());
  CHECK(input.events().empty());

  // A flood between two frames: the rest is dropped and counted.
  for (std::size_t k = 0; k < PageInput::kMaxPending + 10; ++k) queue("wh 1");
  CHECK(input.overflowed() == 10);
  input.pump(true);
  CHECK(input.wheel() == static_cast<float>(PageInput::kMaxPending));
}

void gamepad_counts_only_while_the_window_is_in_front(test::Context& ctx) {
  headless::HeadlessBackend pad_backend;
  headless::HeadlessInput& pad = pad_backend.input();
  PageInput input;
  input.set_gamepad(&pad);
  pad.connect_gamepad(true);
  pad.press(GamepadButton::kStart);
  pad.set_axis(GamepadAxis::kLeftX, 0.5f);
  pad_backend.window().pump_events();
  input.pump(true);
  CHECK(input.gamepad_connected() && input.was_pressed(GamepadButton::kStart) && input.axis(GamepadAxis::kLeftX) == 0.5f);
  CHECK(input.events().size() == 1 && input.events()[0].type == InputEventType::kPadDown);
  // Another window in front: the pad is not the game's.
  input.pump(false);
  CHECK(!input.gamepad_connected() && !input.is_down(GamepadButton::kStart) && input.axis(GamepadAxis::kLeftX) == 0.0f);
  CHECK(input.events().empty());
  input.set_gamepad(nullptr);
  input.pump(true);
  CHECK(!input.gamepad_connected());
}

void page_messages_are_told_apart(test::Context& ctx) {
  auto kind = [](const char* text) { return classify_page_message(text, std::strlen(text)); };
  CHECK(kind("kd KeyW 0") == PageMessage::kInput && kind("bl") == PageMessage::kInput);
  CHECK(kind("rf 1 2 3 4") == PageMessage::kFrame && kind("hi") == PageMessage::kHello);
  CHECK(kind("pr 1 1 2 2 3 4 0") == PageMessage::kProbe && kind("sc data:") == PageMessage::kScreenshot);
  CHECK(kind("{\"type\":\"action\",\"name\":\"play\"}") == PageMessage::kUnknown);
  CHECK(kind("hix") == PageMessage::kUnknown && kind("") == PageMessage::kUnknown && kind("rfx") == PageMessage::kUnknown);
  CHECK(classify_page_message(nullptr, 3) == PageMessage::kUnknown);

  FrameRequest request;
  CHECK(parse_frame_request("rf 3 17 1280 720", 16, request));
  CHECK(request.generation == 3 && request.acked == 17 && request.width == 1280 && request.height == 720);
  const char* bad[] = {"rf 3 17 1280", "rf 3 17 0 720", "rf 3 17 1280 99999", "rf -1 0 1 1", "rf 1 2 3 4 5",
                       "rf 4294967296 0 1 1", "rf 1.5 0 1 1", "rf a b c d"};
  for (const char* text : bad) CHECK(!parse_frame_request(text, std::strlen(text), request));

  ProbeResult probe;
  CHECK(parse_probe("pr 2 1 1280 720 300 990 0", 25, probe));
  CHECK(probe.views == 2 && probe.clicked == 1 && probe.width == 1280 && probe.colors == 300 && probe.not_clear == 990);
  CHECK(!parse_probe("pr 2 1 1280 720 300 1001 0", 26, probe));
  CHECK(!parse_probe("pr 2 1 1280 720 300", 19, probe));
}

// ------------------------------------------------------------ the UI link

class FakePoster final : public ViewPoster {
 public:
  bool post_view(const char* json, std::size_t size) override {
    if (fail) return false;
    posted.emplace_back(json, size);
    return true;
  }
  bool fail = false;
  std::vector<std::string> posted;
};

void page_ui_keeps_the_view_and_sends_it_again(test::Context& ctx) {
  FakePoster poster;
  PageUi ui(&poster);
  CHECK(ui.present());
  // Before the page is there: kept, not sent.
  const char* first = "{\"type\":\"view\",\"screen\":\"main\"}";
  CHECK(ui.post(first, std::strlen(first)));
  CHECK(poster.posted.empty());
  ui.page_ready();
  CHECK(poster.posted.size() == 1 && poster.posted[0] == first);
  const char* second = "{\"type\":\"view\",\"screen\":\"playing\"}";
  CHECK(ui.post(second, std::strlen(second)));
  CHECK(poster.posted.size() == 2 && poster.posted[1] == second);
  // A reload (F5, Vite): the new page gets the last view, not the first.
  ui.page_gone();
  ui.page_ready();
  CHECK(poster.posted.size() == 3 && poster.posted[2] == second);
  CHECK(ui.views_sent() == 3);
  // Too long, empty, or a host that cannot post: false.
  std::string huge(PageUi::kMaxView + 1, 'x');
  CHECK(!ui.post(huge.data(), huge.size()));
  CHECK(!ui.post(nullptr, 3) && !ui.post(second, 0));
  poster.fail = true;
  CHECK(!ui.post(second, std::strlen(second)));

  // From the page: in order; nothing arrived: an empty list.
  UiMessageList out;
  ui.receive(out);
  CHECK(out.count() == 0 && out.dropped() == 0);
  ui.from_page("{\"a\":1}", 7);
  ui.from_page("{\"b\":2}", 7);
  ui.drop_from_page();
  std::string too_long(UiMessageList::kMaxMessageBytes + 1, ' ');
  ui.from_page(too_long.data(), too_long.size());
  ui.receive(out);
  CHECK(out.count() == 2 && std::string(out.data(0), out.size(0)) == "{\"a\":1}" && out.dropped() == 2);
  ui.receive(out);
  CHECK(out.count() == 0 && out.dropped() == 0);
}

void frame_flow_never_writes_a_slot_the_page_may_read(test::Context& ctx) {
  FrameFlow flow;
  CHECK(!flow.can_send());  // no buffer posted yet
  const std::uint32_t generation = flow.new_generation();
  CHECK(generation == 1 && !flow.can_send());  // the page does not hold it yet
  FrameRequest request;
  request.generation = 0;
  request.width = 800;
  request.height = 600;
  CHECK(!flow.on_request(request));  // a page without the buffer
  CHECK(flow.page_width() == 800 && !flow.take_request());
  request.generation = 1;
  CHECK(flow.on_request(request));
  CHECK(!flow.can_send());  // asked, but no frame was run for the request yet
  CHECK(flow.take_request() && !flow.take_request());
  CHECK(flow.can_send());
  flow.sent(1);
  CHECK(!flow.can_send());  // answered: one frame per request
  flow.frame_done();
  // Each request answered with one frame: two may be out, not yet drawn.
  CHECK(flow.on_request(request) && flow.take_request() && flow.can_send());
  flow.sent(2);
  flow.frame_done();
  CHECK(flow.on_request(request) && flow.take_request());
  CHECK(!flow.can_send());  // two unacked: the page may read either slot; wait for an ack
  flow.frame_done();
  CHECK(FrameFlow::slot_for(1) != FrameFlow::slot_for(2) && FrameFlow::slot_for(1) == FrameFlow::slot_for(4));
  request.acked = 1;
  CHECK(flow.on_request(request) && flow.take_request() && flow.can_send() && flow.frames_drawn() == 1);
  flow.sent(3);
  flow.frame_done();
  CHECK(flow.on_request(request) && flow.take_request() && !flow.can_send());
  flow.frame_done();
  // A hostile or stale ack: not counted.
  request.acked = 99;
  flow.on_request(request);
  CHECK(flow.take_request() && !flow.can_send() && flow.last_acked() == 1);
  flow.frame_done();
  request.acked = 3;  // the page skipped 2 (it draws the newest)
  flow.on_request(request);
  CHECK(flow.take_request() && flow.can_send() && flow.last_acked() == 3);
  flow.frame_done();
  // A reload: nothing until the new page holds the new generation.
  flow.page_gone();
  CHECK(!flow.take_request() && !flow.can_send());
  CHECK(flow.new_generation() == 2);
  request.generation = 1;
  CHECK(!flow.on_request(request) && !flow.take_request() && !flow.can_send());
  request.generation = 2;
  request.acked = 0;
  CHECK(flow.on_request(request) && flow.take_request() && flow.can_send());
  // A new buffer while a request is being answered (a frame outgrew the
  // slots): that frame sends nothing; the page asks again for the new one.
  flow.new_generation();
  CHECK(!flow.can_send());
}

// A FrameSink that is the window's flow control, as in webview2_backend.cpp.
class FlowSink final : public FrameSink {
 public:
  bool can_take_frame() const override { return flow.can_send(); }
  bool take_frame(const std::uint8_t*, std::size_t, std::uint32_t seq) override {
    if (!flow.can_send()) return false;
    flow.sent(seq);
    seqs.push_back(seq);
    return true;
  }
  FrameFlow flow;
  std::vector<std::uint32_t> seqs;
};

// The pacing rule (scraps_web's loop): a frame is recorded and sent only to
// answer a request the page made; the frames the app runs anyway when no
// request comes (a hidden or minimised page) send nothing and record
// nothing, but the textures they create wait for the next answered frame.
void only_requested_frames_are_sent(test::Context& ctx) {
  FlowSink sink;
  BatchRenderer renderer(&sink);
  FrameRequest request;
  request.width = 640;
  request.height = 360;
  request.generation = sink.flow.new_generation();
  auto frame = [&renderer]() {
    renderer.begin_frame(Color());
    renderer.fill_rect(RectF(), Color());
    renderer.end_frame();
  };

  // The page holds the buffer and asked once: one frame answers it.
  CHECK(sink.flow.on_request(request));
  CHECK(sink.flow.take_request());
  frame();
  CHECK(sink.seqs.size() == 1 && renderer.frames_sent() == 1);
  sink.flow.frame_done();
  // No new request: fallback frames (the 20 ms timeout) tick, but send nothing,
  // even though a slot is free.
  const std::uint8_t pixel[4] = {9, 9, 9, 255};
  const TextureId made_meanwhile = renderer.create_texture(1, 1, pixel);
  for (int i = 0; i < 5; ++i) {
    frame();
    sink.flow.frame_done();
  }
  CHECK(sink.seqs.size() == 1 && renderer.frames_dropped() == 5);
  CHECK(renderer.queued_resource_bytes() > 0);  // the texture waits
  // A request that arrived while a frame ran is answered by the next frame.
  request.acked = 1;
  CHECK(sink.flow.on_request(request));
  frame();  // this frame did not take it: nothing
  CHECK(sink.seqs.size() == 1);
  sink.flow.frame_done();
  CHECK(sink.flow.take_request());
  frame();
  CHECK(sink.seqs.size() == 2 && sink.seqs[1] == 2);
  CHECK(renderer.queued_resource_bytes() == 0);  // the texture went with it
  DecodedFrame decoded;
  CHECK(decode_frame(renderer.last_frame().data(), renderer.last_frame().size(), decoded));
  CHECK(!decoded.commands.empty() && decoded.commands[0].op == FrameOp::kTexture &&
        decoded.commands[0].texture == made_meanwhile);
  // Two frames for one request: the second sends nothing.
  frame();
  CHECK(sink.seqs.size() == 2);
}

// Resources that do not fit in one frame (a burst of big textures) go over
// several, in order, as many whole ones as fit; every frame still carries
// its draws, so nothing stalls. Before, the whole queue had to fit in one
// frame, and a queue past the slot limit dropped every frame forever.
void resources_too_big_for_one_frame_go_over_several(test::Context& ctx) {
  constexpr std::size_t kLimit = 64 * 1024;  // a small frame limit, as a 64 MB slot is to real textures
  RecordingSink sink;
  BatchRenderer renderer(&sink, kLimit);
  // Each 64x64 texture is 16 KB, a quarter of a frame: the largest allowed.
  std::vector<std::uint8_t> too_big(64 * 65 * 4, 200);
  CHECK(renderer.create_texture(64, 65, too_big.data()) == kNoTexture);  // over a quarter of a frame
  std::vector<std::uint8_t> pixels(64 * 64 * 4, 200);
  std::vector<TextureId> made;
  for (int i = 0; i < 10; ++i) {
    pixels[0] = static_cast<std::uint8_t>(i);
    made.push_back(renderer.create_texture(64, 64, pixels.data()));
  }
  CHECK(made.size() == 10 && made.back() != kNoTexture);
  renderer.destroy_texture(made[3]);
  CHECK(renderer.queued_resource_bytes() > kLimit * 2);

  // Each frame also draws a lot: most of what the draws may use.
  std::vector<Vertex2D> vertices(3 * 600);
  std::vector<TextureId> uploaded;
  bool destroyed_after_upload = false;
  int frames = 0;
  while (renderer.queued_resource_bytes() > 0 && frames < 50) {
    renderer.begin_frame(Color());
    renderer.fill_triangles(vertices.data(), vertices.size(), made[0]);
    renderer.end_frame();
    ++frames;
    CHECK(sink.frames.size() == static_cast<std::size_t>(frames));  // never dropped
    if (sink.frames.size() != static_cast<std::size_t>(frames)) return;
    const std::vector<std::uint8_t>& bytes = sink.frames.back();
    CHECK(bytes.size() <= kLimit);
    DecodedFrame decoded;
    CHECK(decode_frame(bytes.data(), bytes.size(), decoded));
    bool has_draw = false;
    bool has_resource = false;
    for (const DecodedCommand& command : decoded.commands) {
      if (command.op == FrameOp::kTexture) {
        uploaded.push_back(command.texture);
        has_resource = true;
      } else if (command.op == FrameOp::kDestroyTexture) {
        destroyed_after_upload = command.texture == made[3] && uploaded.size() == 10;
        has_resource = true;
      } else if (command.op == FrameOp::kTriangles) {
        has_draw = command.count == vertices.size();
      }
    }
    CHECK(has_draw && has_resource);  // the queue drains every frame, beside full draws
  }
  CHECK(renderer.queued_resource_bytes() == 0 && frames > 2 && frames < 12);
  CHECK(uploaded == made);  // every texture, once, in order
  CHECK(destroyed_after_upload);  // the destroy after the uploads, as queued
  CHECK(renderer.frames_dropped() == 0);

  // A frame the page does not take keeps what it would have carried.
  renderer.resend_textures();
  const std::size_t queued = renderer.queued_resource_bytes();
  sink.accept = false;
  renderer.begin_frame(Color());
  renderer.end_frame();
  CHECK(renderer.queued_resource_bytes() == queued);
  sink.accept = true;
  for (int i = 0; i < 10 && renderer.queued_resource_bytes() > 0; ++i) {
    renderer.begin_frame(Color());
    renderer.end_frame();
  }
  CHECK(renderer.queued_resource_bytes() == 0);
}

// ------------------------------------------------------------ Esc, P and start

// A window for ClientApp whose input is the page's.
class PageWindow final : public Window {
 public:
  explicit PageWindow(PageInput& input) : m_input(input) {}
  void pump_events() override { m_input.pump(true); }
  bool close_requested() const override { return false; }
  void set_title(const char*) override {}

 private:
  PageInput& m_input;
};

void esc_p_and_start_toggle_the_pause_menu_once(test::Context& ctx) {
  if (std::string(scraps_apps::selected_game().name) != "sandbox") {
    CHECK(true);
    std::printf("    skipped: SCRAPS_GAME is not sandbox\n");
    return;
  }
  headless::HeadlessBackend backend;  // renderer and audio
  headless::HeadlessBackend pad_backend;
  PageInput input;
  input.set_gamepad(&pad_backend.input());
  PageWindow window(input);
  FakePoster poster;
  PageUi ui(&poster);
  ui.page_ready();
  PlatformServices platform = backend.services();
  platform.window = &window;
  platform.input = &input;
  platform.ui = &ui;

  std::error_code error;
  const std::filesystem::path saves = std::filesystem::u8path(SCRAPS_TEST_WORK_DIR) / "webview2" / "saves";
  std::filesystem::create_directories(saves, error);
  scraps_apps::ClientOptions options;
  options.scene = "top-down";
  options.scene_dir = SCRAPS_SANDBOX_SCENE_DIR;
  options.threads = 0;
  options.save_dir = saves.u8string() + "/";
  options.debug_hud = false;
  const auto app = std::make_unique<scraps_apps::ClientApp>(options, platform, nullptr);
  std::string start_error;
  CHECK(app->start(start_error));
  double now = 0.0;
  auto frames = [&](int count) {
    for (int k = 0; k < count; ++k) {
      now += 1.0 / 60.0;
      if (!app->frame(now)) return false;
    }
    return true;
  };
  auto key = [&](const char* text) { return input.queue(text, std::strlen(text)); };
  auto action = [&](const char* name) {
    const std::string json = std::string("{\"type\":\"action\",\"name\":\"") + name + "\"}";
    ui.from_page(json.data(), json.size());
  };
  auto screen = [&]() { return app->ui().screen(); };

  CHECK(frames(5));
  CHECK(screen() == scraps_apps::UiScreen::kMain);
  CHECK(!poster.posted.empty() && poster.posted.back().find("\"screen\":\"main\"") != std::string::npos);
  CHECK(!app->has_ui() || app->ui().posts() >= 1);
  action("play");
  CHECK(frames(10));
  CHECK(screen() == scraps_apps::UiScreen::kPlaying);

  // P (forwarded by the page as a game key): one toggle per press.
  CHECK(key("kd KeyP 0"));
  CHECK(frames(1));
  CHECK(screen() == scraps_apps::UiScreen::kPaused && app->paused());
  CHECK(key("kd KeyP 1"));  // held: the OS repeats it; not a press
  CHECK(frames(3));
  CHECK(key("ku KeyP"));
  CHECK(frames(1));
  CHECK(screen() == scraps_apps::UiScreen::kPaused);

  // Esc reaching native (it never should) does nothing at all.
  CHECK(!key("kd Escape 0"));
  CHECK(frames(2));
  CHECK(screen() == scraps_apps::UiScreen::kPaused);

  // Esc in the page: "resume" from the pause menu, once.
  action("resume");
  CHECK(frames(1));
  CHECK(screen() == scraps_apps::UiScreen::kPlaying && !app->paused());

  // The race: P pauses natively, and the page, still showing "playing",
  // turns an Esc into "pause" a frame later. Pausing the paused game is
  // ignored: one toggle, not two.
  const std::size_t ignored = app->ui().ignored();
  CHECK(key("kd KeyP 0"));
  CHECK(frames(1));
  action("pause");
  CHECK(frames(1));
  CHECK(screen() == scraps_apps::UiScreen::kPaused && app->ui().ignored() == ignored + 1);
  CHECK(key("ku KeyP"));
  // And the other way: the page resumes for Esc, a stale "resume" after it is ignored too.
  action("resume");
  CHECK(frames(1));
  action("resume");
  CHECK(frames(1));
  CHECK(screen() == scraps_apps::UiScreen::kPlaying && app->ui().ignored() == ignored + 2);

  // The gamepad's start (native, from SDL; the page ignores start): once per press.
  pad_backend.input().connect_gamepad(true);
  pad_backend.input().press(GamepadButton::kStart);
  pad_backend.window().pump_events();
  CHECK(frames(1));
  CHECK(screen() == scraps_apps::UiScreen::kPaused);
  pad_backend.window().pump_events();  // still held: no new press
  CHECK(frames(1));
  CHECK(screen() == scraps_apps::UiScreen::kPaused);
  pad_backend.input().release(GamepadButton::kStart);
  pad_backend.input().press(GamepadButton::kStart);
  pad_backend.window().pump_events();
  CHECK(frames(1));
  CHECK(screen() == scraps_apps::UiScreen::kPlaying);

  action("quit");
  CHECK(!frames(1));
  CHECK(app->finish() == 0);
  CHECK(app->ui().malformed() == 0 && app->ui().unknown() == 0);
}

}  // namespace

void register_webview2_tests(test::Registry& registry) {
  registry.add("webview2.frame_round_trips", frame_round_trips);
  registry.add("webview2.textures_travel_once_and_again_after_a_reload", textures_travel_once_and_again_after_a_reload);
  registry.add("webview2.decoder_refuses_hostile_frames", decoder_refuses_hostile_frames);
  registry.add("webview2.page_input_takes_messages_and_refuses_hostile_ones",
               page_input_takes_messages_and_refuses_hostile_ones);
  registry.add("webview2.gamepad_counts_only_while_the_window_is_in_front",
               gamepad_counts_only_while_the_window_is_in_front);
  registry.add("webview2.page_messages_are_told_apart", page_messages_are_told_apart);
  registry.add("webview2.page_ui_keeps_the_view_and_sends_it_again", page_ui_keeps_the_view_and_sends_it_again);
  registry.add("webview2.frame_flow_never_writes_a_slot_the_page_may_read",
               frame_flow_never_writes_a_slot_the_page_may_read);
  registry.add("webview2.only_requested_frames_are_sent", only_requested_frames_are_sent);
  registry.add("webview2.resources_too_big_for_one_frame_go_over_several",
               resources_too_big_for_one_frame_go_over_several);
  registry.add("webview2.esc_p_and_start_toggle_the_pause_menu_once", esc_p_and_start_toggle_the_pause_menu_once);
}
