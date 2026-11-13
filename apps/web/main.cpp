// scraps_web: the player's client with the React UI (UI layout A,
// docs/decisions/2026-11-14-ui-layout.md). The same ClientApp as
// scraps_client (apps/client/client_app.h), on other backends:
//
//   webview2 window  ─ Window, Input (keys, mouse, text from the page),
//                      Renderer2D (WebGL2 in the page), UiHost (menus, HUD)
//   sdl3 devices     ─ Audio and the gamepad, with no SDL window
//
// It starts on the main menu: nothing ticks until Play or Continue.
//
// Pacing: the page asks for a frame after each requestAnimationFrame, so
// ClientApp::frame runs once per displayed frame, at the display's rate, and
// never faster than the page can draw. When the page asks for none
// (minimised, hidden, still loading), a 20 ms timeout keeps the game, the
// network and the sound going (Rule 2: the fixed-step clock catches up).
//
// This file names the concrete backends (Rule 3), as apps may.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "backends/dynlib/library.h"
#include "backends/sdl3/sdl3_devices.h"
#include "backends/webview2/webview2_backend.h"
#include "client/client_app.h"
#include "common/scene_dir.h"
#include "common/ui_model.h"
#include "selected_game.h"
#include "views/view_factory.h"

namespace {

using namespace scraps;

// How long to wait for the page to ask for a frame before running one anyway.
constexpr double kFallbackFrameSeconds = 0.020;
// Self-test: frames the page must draw after Play before it is checked.
constexpr std::uint64_t kSelfTestFrames = 120;
constexpr double kSelfTestTimeoutSeconds = 90.0;
// ctest's "skipped" exit code (the WebView2 runtime is not installed).
constexpr int kSkipped = 77;

struct Options {
  std::string scene = scraps_apps::selected_game().default_scene;
  std::string scene_dir;
  std::string connect;
  std::string view;
  long frames = 0;
  long threads = -1;
  bool hot_game = false;
  std::string ui_dir;   // --ui-dir: the built UI (ui/dist)
  std::string ui_dev;   // --ui-dev: a dev server URL instead
  bool debug_hud = false;
  bool self_test = false;
  std::string screenshot;
  std::string save_dir;
  bool help = false;
};

void print_usage() {
  std::printf(
      "usage: scraps_web [options]          (game: %s)\n"
      "  --scene <name>        scene to play offline (default: %s)\n"
      "  --scene-dir <dir>     where .scene files are (default: scenes/ next to this program)\n"
      "  --connect <ip:port>   join a scraps_server instead of playing offline\n"
      "  --view <name>         start in top-down, side or isometric view\n"
      "  --frames <n>          quit after n frames\n"
      "  --threads <n>         worker threads for the simulation (default: cores - 1; 0 = none)\n"
      "  --hot-game            offline only: run the game from its module and reload it when rebuilt\n"
      "  --ui-dir <dir>        the built UI (default: ui/ next to this program, else the build's ui/dist)\n"
      "  --ui-dev <url>        load the UI from a dev server (npm run dev: http://localhost:5173/)\n"
      "  --debug-hud           also draw the engine's debug text (scene, tick, fps)\n"
      "  --save-dir <dir>      where quick saves go (default: %%APPDATA%%\\Scraps\\<game>)\n"
      "  --self-test           press Play, let the page draw %d frames, check the canvas, quit\n"
      "                        (exit code 0 if it drew, 77 if WebView2 is not installed)\n"
      "  --screenshot <file>   with --self-test: save the canvas as a PNG, and the whole window\n"
      "                        as <file>-window.png\n"
      "keys: WASD/arrows move, Space jump, E drop coin, click place coin, V view, -/= or wheel zoom,\n"
      "      Esc menu, P pause, F5 quick save, F9 quick load; gamepad as scraps_client\n",
      scraps_apps::selected_game().name, scraps_apps::selected_game().default_scene,
      static_cast<int>(kSelfTestFrames));
}

bool parse_options(const std::vector<std::string>& args, Options& options) {
  for (std::size_t i = 1; i < args.size(); ++i) {
    const std::string& arg = args[i];
    auto value = [&](std::string& out) {
      if (i + 1 >= args.size()) {
        std::fprintf(stderr, "missing value after %s\n", arg.c_str());
        return false;
      }
      out = args[++i];
      return true;
    };
    auto number = [&](long& out, long low, long high) {
      std::string text;
      if (!value(text)) return false;
      char* end = nullptr;
      out = std::strtol(text.c_str(), &end, 10);
      if (end == text.c_str() || *end != '\0' || out < low || out > high) {
        std::fprintf(stderr, "%s needs a number from %ld to %ld\n", arg.c_str(), low, high);
        return false;
      }
      return true;
    };
    if (arg == "--scene") {
      if (!value(options.scene)) return false;
    } else if (arg == "--scene-dir") {
      if (!value(options.scene_dir)) return false;
    } else if (arg == "--connect") {
      if (!value(options.connect)) return false;
    } else if (arg == "--view") {
      if (!value(options.view)) return false;
      Perspective unused = Perspective::kTopDown;
      if (!parse_perspective(options.view, unused)) {
        std::fprintf(stderr, "unknown view '%s'\n", options.view.c_str());
        return false;
      }
    } else if (arg == "--frames") {
      if (!number(options.frames, 1, 1000000000L)) return false;
    } else if (arg == "--threads") {
      if (!number(options.threads, 0, 64)) return false;
    } else if (arg == "--hot-game") {
      if (!scraps_apps::selected_game().hot_reload) {
        std::fprintf(stderr, "--hot-game is not available in this build\n");
        return false;
      }
      options.hot_game = true;
    } else if (arg == "--ui-dir") {
      if (!value(options.ui_dir)) return false;
    } else if (arg == "--ui-dev") {
      if (!value(options.ui_dev)) return false;
      if (options.ui_dev.compare(0, 7, "http://") != 0 && options.ui_dev.compare(0, 8, "https://") != 0) {
        std::fprintf(stderr, "--ui-dev needs an http:// or https:// URL\n");
        return false;
      }
    } else if (arg == "--debug-hud") {
      options.debug_hud = true;
    } else if (arg == "--self-test") {
      options.self_test = true;
    } else if (arg == "--screenshot") {
      if (!value(options.screenshot)) return false;
    } else if (arg == "--save-dir") {
      if (!value(options.save_dir)) return false;
      if (!options.save_dir.empty() && options.save_dir.back() != '/' && options.save_dir.back() != '\\') {
        options.save_dir += '/';
      }
    } else if (arg == "--help" || arg == "-h") {
      options.help = true;
    } else {
      std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
      return false;
    }
  }
  if (options.hot_game && !options.connect.empty()) {
    std::fprintf(stderr, "--hot-game works offline only; run scraps_server --hot-game instead\n");
    return false;
  }
  if (!options.screenshot.empty() && !options.self_test) {
    std::fprintf(stderr, "--screenshot works with --self-test\n");
    return false;
  }
  return true;
}

bool has_index(const std::string& dir) {
  if (dir.empty()) return false;
  std::error_code error;
  return std::filesystem::is_regular_file(std::filesystem::u8path(dir) / "index.html", error);
}

// Where the built UI is: --ui-dir, ui/ next to the exe (a release), or the
// build's ui/dist (compiled in; development builds run from the build folder).
std::string find_ui_dir(const Options& options) {
  if (!options.ui_dir.empty()) {
    // WebView2 serves a folder by its full path.
    std::error_code error;
    const std::filesystem::path full = std::filesystem::absolute(std::filesystem::u8path(options.ui_dir), error);
    return error ? options.ui_dir : full.u8string();
  }
  const std::string exe_dir = scraps::dynlib::executable_directory();
  if (!exe_dir.empty()) {
    const std::string next_to_exe = (std::filesystem::u8path(exe_dir) / "ui").u8string();
    if (has_index(next_to_exe)) return next_to_exe;
  }
  return SCRAPS_WEB_UI_BUILD_DIR;
}

// --self-test: the steps, one per frame, until it passes, fails or times out.
class SelfTest {
 public:
  SelfTest(const Options& options, double start) : m_screenshot(options.screenshot), m_start(start) {}

  // False when the test is over (then result() says how).
  bool step(webview2::WebView2Platform& web, const scraps_apps::ClientApp& app, double now) {
    if (now - m_start > kSelfTestTimeoutSeconds) {
      std::printf("self-test: timed out in step %d (page ready %d, views %zu, frames drawn %llu)\n", m_step,
                  web.page_ready() ? 1 : 0, web.views_sent(), static_cast<unsigned long long>(web.frames_drawn()));
      m_result = 1;
      return false;
    }
    switch (m_step) {
      case 0:  // the page is up and has been told what to show: press Play as a player would
        if (web.page_ready() && web.views_sent() > 0) {
          web.click_play();
          m_step = 1;
        }
        break;
      case 1:
        if (app.ui().started()) {
          m_frames_at_play = web.frames_drawn();
          m_step = 2;
        }
        break;
      case 2:
        if (web.frames_drawn() >= m_frames_at_play + kSelfTestFrames) {
          web.request_probe();
          if (!m_screenshot.empty()) web.request_screenshot(m_screenshot);
          m_probe_time = now;
          m_step = 3;
        }
        break;
      case 3: {
        // Both files, or give the window capture a few seconds.
        const bool shots_done = m_screenshot.empty() || web.screenshots_written() >= 2 ||
                                (web.screenshots_written() >= 1 && now - m_probe_time > 5.0);
        if (web.has_probe() && shots_done) {
          evaluate(web, app);
          return false;
        }
        break;
      }
      default:
        return false;
    }
    return true;
  }

  int result() const { return m_result; }

 private:
  void evaluate(const webview2::WebView2Platform& web, const scraps_apps::ClientApp& app) {
    const webview2::ProbeResult& probe = web.probe();
    // Drawn: a canvas of the window's size with the world on it (many
    // colours, most pixels not the sky's clear colour), the view reached the
    // page, Play was a real button press, the world ticked, and React never
    // saw the backend's frame messages.
    const bool ok = probe.views > 0 && probe.clicked == 1 && probe.width > 0 && probe.height > 0 &&
                    probe.colors >= 8 && probe.not_clear >= 50 && probe.leaked == 0 && app.stats().ticks > 0;
    std::printf(
        "self-test: page views %u, play %s, canvas %dx%d, %u colours, %.1f%% not sky, leaked %u, "
        "frames drawn %llu, ticks %ld, page messages refused %zu: %s\n",
        probe.views, probe.clicked == 1 ? "clicked" : probe.clicked == 2 ? "posted (no button)" : "not pressed",
        probe.width, probe.height, probe.colors, probe.not_clear / 10.0, probe.leaked,
        static_cast<unsigned long long>(web.frames_drawn()), app.stats().ticks, web.rejected_messages(),
        ok ? "ok" : "FAILED");
    if (!m_screenshot.empty()) {
      std::printf("self-test: screenshots written %d (%s and its -window.png)\n", web.screenshots_written(),
                  m_screenshot.c_str());
    }
    m_result = ok && (m_screenshot.empty() || web.screenshots_written() >= 1) ? 0 : 1;
  }

  std::string m_screenshot;
  double m_start;
  int m_step = 0;
  std::uint64_t m_frames_at_play = 0;
  double m_probe_time = 0.0;
  int m_result = 1;
};

int run(const Options& options) {
  std::string notes;
  const std::unique_ptr<sdl3::Sdl3Devices> devices = sdl3::Sdl3Devices::create(notes);
  if (!notes.empty()) std::printf("sound and gamepad: %s\n", notes.c_str());

  webview2::WebViewOptions web_options;
  web_options.developer = scraps_apps::selected_game().hot_reload;  // development builds
  if (!options.ui_dev.empty()) {
    web_options.url = options.ui_dev;
    std::printf("UI from %s\n", options.ui_dev.c_str());
  } else {
    web_options.folder = find_ui_dir(options);
    if (!has_index(web_options.folder)) {
      std::fprintf(stderr,
                   "error: no built UI in %s (build it: cd ui && npm ci && npm run build, or configure with "
                   "SCRAPS_BUILD_UI=ON; or pass --ui-dir / --ui-dev)\n",
                   web_options.folder.c_str());
      return 1;
    }
    web_options.url = std::string("https://") + webview2::kUiHostName + "/index.html";
    std::printf("UI from %s\n", web_options.folder.c_str());
  }

  WindowDesc desc;
  desc.title = "Scraps";
  std::string error;
  webview2::CreateFailure failure = webview2::CreateFailure::kNone;
  const std::unique_ptr<webview2::WebView2Platform> web = webview2::WebView2Platform::create(desc, web_options, error,
                                                                                            failure);
  if (!web) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return failure == webview2::CreateFailure::kNoRuntime && options.self_test ? kSkipped : 1;
  }
  web->set_gamepad(devices.get());
  PlatformServices platform = web->services();
  platform.audio = devices.get();

  scraps_apps::ClientOptions client_options;
  client_options.scene = options.scene;
  client_options.scene_dir = options.scene_dir;
  client_options.connect = options.connect;
  client_options.view = options.view;
  client_options.frames = options.frames;
  client_options.threads = options.threads;
  client_options.hot_game = options.hot_game;
  client_options.save_dir = options.save_dir;
  client_options.debug_hud = options.debug_hud;
  // On the heap: the app keeps fixed buffers for the UI bridge.
  const auto app = std::make_unique<scraps_apps::ClientApp>(client_options, platform, nullptr);
  if (!app->start(error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }

  // The loop owns the clock ("only the loop reads the clock").
  const auto start = std::chrono::steady_clock::now();
  auto seconds = [&start]() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  };
  std::unique_ptr<SelfTest> self_test;
  if (options.self_test) self_test = std::make_unique<SelfTest>(options, seconds());
  while (true) {
    web->wait_for_frame(kFallbackFrameSeconds);
    devices->pump();  // before the window's pump_events(), which reads the pad
    const double now = seconds();
    if (!app->frame(now)) break;
    if (self_test && !self_test->step(*web, *app, now)) break;
  }
  const int code = app->finish();
  return self_test ? self_test->result() : code;
}

}  // namespace

int main(int, char**) {
  scraps::webview2::attach_parent_console();
  Options options;
  // UTF-8 arguments (paths with any characters), not main's ANSI argv.
  if (!parse_options(scraps::webview2::command_line_utf8(), options)) {
    print_usage();
    return 2;
  }
  if (options.help) {
    print_usage();
    return 0;
  }
  const scraps_apps::SceneDirChoice scenes = scraps_apps::choose_scene_dir(
      options.scene_dir, scraps::dynlib::executable_directory(), scraps_apps::selected_game().source_scene_dir);
  options.scene_dir = scenes.path;
  std::printf("game '%s', scenes from %s (%s)\n", scraps_apps::selected_game().name, scenes.path.c_str(),
              scraps_apps::scene_dir_source_name(scenes.source));
  std::fflush(stdout);
  const int code = run(options);
  std::fflush(stdout);
  return code;
}
