#pragma once

// The webview2 backend (UI layout A, docs/decisions/2026-11-14-ui-layout.md):
// a Win32 window holding a WebView2 control that shows the React UI, with the
// world drawn under it by WebGL2 in the same page.
//
//   native                                   page (https://scraps.ui/index.html)
//   ──────                                   ────
//   BatchRenderer ─ frame bytes ─► shared buffer slot ─► renderer.js: WebGL2 into <canvas id="world">
//                 ─ "\x01f seq slot gen size" (text) ─►
//   FrameFlow    ◄─ "rf gen acked w h" after each requestAnimationFrame ─
//   PageInput    ◄─ "kd KeyW 0", "pm x y", ... (only while playing; never Esc) ─
//   PageUi       ─ view JSON (PostWebMessageAsJson) ─► React
//                ◄─ action JSON objects (postMessage(obj)) ─
//
// Window, Input, Renderer2D and UiHost come from here. Audio and the gamepad
// do not: the app pairs this window with a service that has them (the sdl3
// devices), so this backend needs no SDL. renderer.js is compiled in and
// injected with AddScriptToExecuteOnDocumentCreated.
//
// Only this backend's .cpp includes <windows.h> and WebView2.h (Rule 3).
// Everything runs on the thread that creates it (WebView2 is single-threaded).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "backends/webview2/page_input.h"
#include "platform/input.h"
#include "platform/renderer2d.h"
#include "platform/services.h"
#include "platform/ui.h"
#include "platform/window.h"

namespace scraps::webview2 {

// The host name the UI folder is served from: https://scraps.ui/.
constexpr const char* kUiHostName = "scraps.ui";

struct WebViewOptions {
  // What to load: https://scraps.ui/index.html (with `folder`), or a dev
  // server such as http://localhost:5173/ (then `folder` is empty).
  std::string url;
  // UTF-8 path of the built UI (ui/dist), served as https://scraps.ui/.
  std::string folder;
  // DevTools (F12), browser keys (F5 reloads the page) and the browser's
  // context menu. Development builds only.
  bool developer = false;
  // UTF-8; where WebView2 keeps its profile. Empty: %LOCALAPPDATA%\Scraps\WebView2.
  std::string user_data_dir;
};

enum class CreateFailure : std::uint8_t {
  kNone,
  kNoRuntime,  // the WebView2 runtime is not installed
  kOther,
};

class WebView2Platform final : public Window {
  struct Impl;
  struct Passkey {
    explicit Passkey() = default;
  };

 public:
  // Only create() can make a Passkey, so only create() can construct.
  WebView2Platform(Passkey, std::unique_ptr<Impl> impl);

  // Opens the window and loads the page. Blocks until the WebView2 control
  // exists (not until the page has loaded). nullptr, `error` and `failure`
  // when it cannot.
  static std::unique_ptr<WebView2Platform> create(const WindowDesc& desc, const WebViewOptions& options,
                                                  std::string& error, CreateFailure& failure);
  ~WebView2Platform() override;
  WebView2Platform(const WebView2Platform&) = delete;
  WebView2Platform& operator=(const WebView2Platform&) = delete;

  // Window, Input, Renderer2D and UiHost. `audio` is left null: the app pairs one.
  PlatformServices services();
  // The gamepad's source (not owned), read only while this window is in front.
  void set_gamepad(const Input* pad);

  // Handles window messages until the page asks for a frame (after its
  // requestAnimationFrame: true) or `max_seconds` pass (false; a minimised
  // or hidden page asks for none, and the game must run on).
  bool wait_for_frame(double max_seconds);

  // Window
  void pump_events() override;
  bool close_requested() const override;
  void set_title(const char* title) override;
  void request_close();

  // ---- for the self-test and screenshots (scraps_web --self-test, --screenshot)
  // Asks the page to press its Play button (as a player would).
  void click_play();
  // The page samples its canvas right after its next draw and reports it.
  void request_probe();
  bool has_probe() const;
  const ProbeResult& probe() const;
  // Saves the canvas (the world only) as a PNG at `path` (UTF-8), and the
  // whole window, UI included, next to it as <name>-window.png.
  void request_screenshot(const std::string& path);
  // Files written so far (each request writes two).
  int screenshots_written() const;
  bool page_ready() const;               // the page's WebGL said hello
  std::uint64_t frames_drawn() const;    // frames the page reported drawn
  std::size_t views_sent() const;        // view messages posted to the page
  std::size_t rejected_messages() const; // page messages refused (input and unknown)

 private:
  std::unique_ptr<Impl> m;
};

// The program's arguments in UTF-8, read from the wide command line (main's
// argv is in the ANSI code page and loses characters).
std::vector<std::string> command_line_utf8();

// A program built for the Windows GUI subsystem has no console. When started
// from one, write stdout and stderr there; when they already go somewhere (a
// pipe, as under ctest), leave them.
void attach_parent_console();

}  // namespace scraps::webview2
