#include "backends/webview2/webview2_backend.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <WebView2.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "backends/webview2/batch_renderer.h"
#include "backends/webview2/frame_format.h"
#include "backends/webview2/page_link.h"
#include "backends/webview2/renderer_js.h"  // generated from renderer.js: kRendererJs

namespace scraps::webview2 {
namespace {

// ------------------------------------------------------------------ COM glue

// Interface ids, parsed at compile time from the uuids in WebView2.h, so the
// same code builds with MSVC and MinGW (GCC has no __uuidof for these).
constexpr int hex_value(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0;
}
constexpr unsigned long hex_at(const char* s, int at, int digits) {
  unsigned long value = 0;
  for (int i = 0; i < digits; ++i) value = value * 16 + static_cast<unsigned long>(hex_value(s[at + i]));
  return value;
}
constexpr GUID make_guid(const char (&s)[37]) {
  return GUID{hex_at(s, 0, 8),
              static_cast<unsigned short>(hex_at(s, 9, 4)),
              static_cast<unsigned short>(hex_at(s, 14, 4)),
              {static_cast<unsigned char>(hex_at(s, 19, 2)), static_cast<unsigned char>(hex_at(s, 21, 2)),
               static_cast<unsigned char>(hex_at(s, 24, 2)), static_cast<unsigned char>(hex_at(s, 26, 2)),
               static_cast<unsigned char>(hex_at(s, 28, 2)), static_cast<unsigned char>(hex_at(s, 30, 2)),
               static_cast<unsigned char>(hex_at(s, 32, 2)), static_cast<unsigned char>(hex_at(s, 34, 2))}};
}

constexpr GUID kIidUnknown = make_guid("00000000-0000-0000-c000-000000000046");
constexpr GUID kIidWebView3 = make_guid("a0d6df20-3b92-416d-aa0c-437a9c727857");
constexpr GUID kIidWebView17 = make_guid("702e75d4-fd44-434d-9d70-1a68a6b1192a");
constexpr GUID kIidEnvironment12 = make_guid("f503db9b-739f-48dd-b151-fdfcf253f54e");
constexpr GUID kIidSettings3 = make_guid("fdb5ab74-af33-4854-84f0-0a631deb5eba");
constexpr GUID kIidController2 = make_guid("c979903e-d4ca-4228-92eb-47ee3fa96eab");
constexpr GUID kIidEnvironmentCreated = make_guid("4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d");
constexpr GUID kIidControllerCreated = make_guid("6c4819f3-c9b7-4260-8127-c9f5bde7f68c");
constexpr GUID kIidMessageReceived = make_guid("57213f19-00e6-49fa-8e07-898ea01ecbd2");
constexpr GUID kIidNavigationStarting = make_guid("9adbe429-f36d-432b-9ddc-f8881fbd76e3");
constexpr GUID kIidNavigationCompleted = make_guid("d33a35bf-1c49-4f98-93ab-006e0533fe1c");
constexpr GUID kIidNewWindow = make_guid("d4c185fe-c81c-4989-97af-2d3fa7ab5651");
constexpr GUID kIidProcessFailed = make_guid("79e0aea4-990b-42d9-aa1d-0fcc2e5bc7f1");
constexpr GUID kIidScriptAdded = make_guid("b99369f3-9b11-47b5-bc6f-8e7895fcea17");
constexpr GUID kIidCaptureDone = make_guid("697e05e9-3d8f-45fa-96f4-8ffe1ededaf5");

// An owning interface pointer (WRL's ComPtr is not available with MinGW).
template <typename T>
class Com {
 public:
  Com() = default;
  ~Com() { reset(); }
  Com(const Com&) = delete;
  Com& operator=(const Com&) = delete;
  void reset() {
    if (m_p != nullptr) m_p->Release();
    m_p = nullptr;
  }
  T* get() const { return m_p; }
  T* operator->() const { return m_p; }
  explicit operator bool() const { return m_p != nullptr; }
  // For out parameters: releases what it holds first.
  T** put() {
    reset();
    return &m_p;
  }
  void take(T* p) {
    if (p != nullptr) p->AddRef();
    reset();
    m_p = p;
  }
  template <typename U>
  bool query(const GUID& iid, Com<U>& out) const {
    return m_p != nullptr && SUCCEEDED(m_p->QueryInterface(iid, reinterpret_cast<void**>(out.put())));
  }

 private:
  T* m_p = nullptr;
};

// A WebView2 callback object that forwards Invoke to a function. It lives
// inside the platform (no heap object, no `delete this`): the platform
// removes its event handlers and closes the controller before it goes away,
// so WebView2 holds no reference afterwards. The count is kept for COM's
// sake only.
template <typename Interface, typename Owner, typename... Args>
class Handler final : public Interface {
 public:
  using Function = HRESULT (*)(Owner&, Args...);
  Handler(const GUID& iid, Owner& owner, Function function) : m_iid(iid), m_owner(owner), m_function(function) {}
  Handler(const Handler&) = delete;
  Handler& operator=(const Handler&) = delete;
  virtual ~Handler() = default;

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
    if (object == nullptr) return E_POINTER;
    if (IsEqualGUID(iid, kIidUnknown) || IsEqualGUID(iid, m_iid)) {
      *object = static_cast<Interface*>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
  ULONG STDMETHODCALLTYPE Release() override { return m_refs > 0 ? --m_refs : 0; }
  HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return m_function(m_owner, args...); }

 private:
  GUID m_iid;
  Owner& m_owner;
  Function m_function;
  ULONG m_refs = 1;
};

// ------------------------------------------------------------------ text

// UTF-8 to UTF-16 into `out` (with a terminating 0). False if it does not fit or is not UTF-8.
bool to_wide(const char* utf8, std::size_t size, wchar_t* out, std::size_t capacity) {
  if (capacity == 0) return false;
  if (size == 0) {
    out[0] = L'\0';
    return true;
  }
  if (size > 0x7FFFFFFF || capacity > 0x7FFFFFFF) return false;
  const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, static_cast<int>(size), out,
                                          static_cast<int>(capacity - 1));
  if (written <= 0) return false;
  out[written] = L'\0';
  return true;
}

std::wstring to_wide(const std::string& utf8) {
  if (utf8.empty() || utf8.size() > 0x7FFFFFFF) return std::wstring();
  const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  if (length <= 0) return std::wstring();
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
  return wide;
}

// UTF-16 to UTF-8 into `out`. Returns the size, or -1 if it does not fit.
int to_utf8(const wchar_t* wide, std::size_t length, char* out, std::size_t capacity) {
  if (length == 0) return 0;
  if (length > 0x7FFFFFFF || capacity > 0x7FFFFFFF) return -1;
  const int written = WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(length), out,
                                          static_cast<int>(capacity), nullptr, nullptr);
  return written > 0 ? written : -1;
}

std::string to_utf8(const wchar_t* wide) {
  if (wide == nullptr) return std::string();
  const std::size_t length = std::wcslen(wide);
  if (length == 0 || length > 0x7FFFFFFF) return std::string();
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return std::string();
  std::string text(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(length), text.data(), size, nullptr, nullptr);
  return text;
}

bool starts_with(const wchar_t* text, const std::wstring& prefix) {
  return text != nullptr && !prefix.empty() && std::wcsncmp(text, prefix.c_str(), prefix.size()) == 0;
}

// "https://scraps.ui/index.html" -> "https://scraps.ui/": what the page's
// own URLs start with. Navigations elsewhere are refused, and messages from
// elsewhere ignored.
std::wstring origin_of(const std::wstring& url) {
  const std::size_t scheme = url.find(L"://");
  if (scheme == std::wstring::npos) return url;
  const std::size_t path = url.find(L'/', scheme + 3);
  return path == std::wstring::npos ? url + L"/" : url.substr(0, path + 1);
}

int base64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

bool decode_base64(const std::string& text, std::size_t from, std::vector<std::uint8_t>& out) {
  out.clear();
  std::uint32_t bits = 0;
  int count = 0;
  for (std::size_t i = from; i < text.size(); ++i) {
    if (text[i] == '=') break;
    const int value = base64_value(text[i]);
    if (value < 0) return false;
    bits = (bits << 6) | static_cast<std::uint32_t>(value);
    count += 6;
    if (count >= 8) {
      count -= 8;
      out.push_back(static_cast<std::uint8_t>((bits >> count) & 0xFFu));
    }
  }
  return !out.empty();
}

bool write_file(const std::wstring& path, const std::vector<std::uint8_t>& bytes) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
  CloseHandle(file);
  return ok && written == bytes.size();
}

// True if this session's input desktop can be opened: false for a service,
// and while the session is locked or disconnected (the sign-in screen's
// desktop gets the input then, and a page in a window here cannot draw).
bool has_input_desktop() {
  HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
  if (desktop == nullptr) return false;
  CloseDesktop(desktop);
  return true;
}

// Milliseconds for one MsgWaitForMultipleObjectsEx: `seconds`, rounded up,
// at least 1 and at most a minute (a bad deadline never hangs a wait).
DWORD wait_milliseconds(double seconds) {
  if (!(seconds > 0.0)) return 1;
  if (seconds > 60.0) return 60000;
  return static_cast<DWORD>(seconds * 1000.0) + 1;
}

constexpr wchar_t kWindowClass[] = L"ScrapsWebView2Window";
// The first slot size; a frame that does not fit grows the buffer.
constexpr std::size_t kInitialSlotBytes = std::size_t{1} << 20;
// The longest page text message read without allocating ("sc" screenshots
// are the only longer ones, and they allocate: a developer tool).
constexpr std::size_t kMaxPageText = 4096;
// The longest UI message converted: anything longer is dropped whole.
constexpr std::size_t kMaxUiText = UiMessageList::kMaxMessageBytes;

}  // namespace

// ------------------------------------------------------------------ Impl

struct WebView2Platform::Impl final : FrameSink, ViewPoster {
  Impl() : renderer(this), ui(this) {}
  ~Impl() override;

  // Window
  HWND hwnd = nullptr;
  bool close_requested = false;
  bool com_initialized = false;
  bool developer = false;
  std::wstring url;
  std::wstring origin;
  std::wstring folder;
  std::function<double()> clock;  // the app's (Rule 2); read only while create() waits

  // WebView2
  Com<ICoreWebView2Environment> environment;
  Com<ICoreWebView2Environment12> environment12;
  Com<ICoreWebView2Controller> controller;
  Com<ICoreWebView2> webview;
  Com<ICoreWebView2_17> webview17;
  Com<ICoreWebView2SharedBuffer> buffer;
  BYTE* buffer_bytes = nullptr;
  std::size_t slot_capacity = 0;
  EventRegistrationToken message_token{};
  EventRegistrationToken starting_token{};
  EventRegistrationToken frame_starting_token{};
  EventRegistrationToken completed_token{};
  EventRegistrationToken new_window_token{};
  EventRegistrationToken failed_token{};
  bool tokens_added = false;
  HRESULT create_result = S_OK;
  bool environment_done = false;
  bool controller_done = false;
  bool script_done = false;
  bool textures_lost = false;  // a new buffer: resend the textures at the next pump

  // The page
  PageInput input;
  BatchRenderer renderer;
  PageUi ui;
  FrameFlow flow;
  std::size_t rejected = 0;
  bool hello = false;
  std::array<char, kMaxPageText + 1> text{};
  std::array<char, kMaxUiText + 1> ui_text{};
  std::array<wchar_t, PageUi::kMaxView + 1> view_text{};
  std::array<wchar_t, 96> frame_text{};

  // Self-test and screenshots
  bool has_probe = false;
  ProbeResult probe;
  std::wstring screenshot_path;
  std::wstring window_shot_path;
  Com<IStream> capture_stream;
  int screenshots = 0;

  using EnvironmentCreated = Handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, Impl, HRESULT,
                                     ICoreWebView2Environment*>;
  using ControllerCreated =
      Handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, Impl, HRESULT, ICoreWebView2Controller*>;
  using MessageReceived = Handler<ICoreWebView2WebMessageReceivedEventHandler, Impl, ICoreWebView2*,
                                  ICoreWebView2WebMessageReceivedEventArgs*>;
  using NavigationStarting = Handler<ICoreWebView2NavigationStartingEventHandler, Impl, ICoreWebView2*,
                                     ICoreWebView2NavigationStartingEventArgs*>;
  using NavigationCompleted = Handler<ICoreWebView2NavigationCompletedEventHandler, Impl, ICoreWebView2*,
                                      ICoreWebView2NavigationCompletedEventArgs*>;
  using NewWindow =
      Handler<ICoreWebView2NewWindowRequestedEventHandler, Impl, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>;
  using ProcessFailed =
      Handler<ICoreWebView2ProcessFailedEventHandler, Impl, ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs*>;
  using ScriptAdded = Handler<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler, Impl, HRESULT, LPCWSTR>;
  using CaptureDone = Handler<ICoreWebView2CapturePreviewCompletedHandler, Impl, HRESULT>;

  EnvironmentCreated on_environment{kIidEnvironmentCreated, *this, &Impl::environment_created};
  ControllerCreated on_controller{kIidControllerCreated, *this, &Impl::controller_created};
  MessageReceived on_message{kIidMessageReceived, *this, &Impl::message_received};
  NavigationStarting on_starting{kIidNavigationStarting, *this, &Impl::navigation_starting};
  NavigationStarting on_frame_starting{kIidNavigationStarting, *this, &Impl::frame_navigation_starting};
  NavigationCompleted on_completed{kIidNavigationCompleted, *this, &Impl::navigation_completed};
  NewWindow on_new_window{kIidNewWindow, *this, &Impl::new_window};
  ProcessFailed on_failed{kIidProcessFailed, *this, &Impl::process_failed};
  ScriptAdded on_script{kIidScriptAdded, *this, &Impl::script_added};
  CaptureDone on_capture{kIidCaptureDone, *this, &Impl::capture_done};

  static HRESULT environment_created(Impl& self, HRESULT result, ICoreWebView2Environment* created);
  static HRESULT controller_created(Impl& self, HRESULT result, ICoreWebView2Controller* created);
  static HRESULT message_received(Impl& self, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args);
  static HRESULT navigation_starting(Impl& self, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args);
  static HRESULT frame_navigation_starting(Impl& self, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args);
  static HRESULT navigation_completed(Impl& self, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args);
  static HRESULT new_window(Impl& self, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args);
  static HRESULT process_failed(Impl& self, ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args);
  static HRESULT script_added(Impl& self, HRESULT result, LPCWSTR id);
  static HRESULT capture_done(Impl& self, HRESULT result);
  static LRESULT CALLBACK window_procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

  // Handles every waiting window message. False once WM_QUIT arrived.
  bool dispatch_messages();
  // Waits (handling messages) until `done`, or until the app's clock
  // reaches `deadline` (false).
  bool wait_until(const bool& done, double deadline);
  // True if a navigation may go ahead: only to the UI's own origin.
  bool allowed(ICoreWebView2NavigationStartingEventArgs* args) const;
  void fit_to_window();
  void handle_page_text(const wchar_t* message);
  void handle_screenshot(const wchar_t* message);
  bool make_buffer(std::size_t slot_bytes);
  void post_buffer();
  void post_internal(const wchar_t* message);

  // FrameSink
  bool can_take_frame() const override { return buffer_bytes != nullptr && flow.can_send(); }
  bool take_frame(const std::uint8_t* bytes, std::size_t size, std::uint32_t seq) override;
  // ViewPoster
  bool post_view(const char* json, std::size_t size) override;
};

WebView2Platform::Impl::~Impl() {
  if (webview && tokens_added) {
    webview->remove_WebMessageReceived(message_token);
    webview->remove_NavigationStarting(starting_token);
    webview->remove_FrameNavigationStarting(frame_starting_token);
    webview->remove_NavigationCompleted(completed_token);
    webview->remove_NewWindowRequested(new_window_token);
    webview->remove_ProcessFailed(failed_token);
  }
  if (controller) controller->Close();
  capture_stream.reset();
  buffer.reset();
  webview17.reset();
  webview.reset();
  controller.reset();
  environment12.reset();
  environment.reset();
  if (hwnd != nullptr) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    DestroyWindow(hwnd);
  }
  if (com_initialized) CoUninitialize();
}

HRESULT WebView2Platform::Impl::environment_created(Impl& self, HRESULT result, ICoreWebView2Environment* created) {
  self.create_result = result;
  if (SUCCEEDED(result) && created != nullptr) self.environment.take(created);
  self.environment_done = true;
  return S_OK;
}

HRESULT WebView2Platform::Impl::controller_created(Impl& self, HRESULT result, ICoreWebView2Controller* created) {
  self.create_result = result;
  if (SUCCEEDED(result) && created != nullptr) self.controller.take(created);
  self.controller_done = true;
  return S_OK;
}

HRESULT WebView2Platform::Impl::script_added(Impl& self, HRESULT result, LPCWSTR) {
  self.create_result = result;
  self.script_done = true;
  return S_OK;
}

HRESULT WebView2Platform::Impl::capture_done(Impl& self, HRESULT result) {
  self.capture_stream.reset();  // closes the file
  if (SUCCEEDED(result)) {
    ++self.screenshots;
  } else {
    std::fprintf(stderr, "webview2: capturing the window failed (0x%08lx)\n", static_cast<unsigned long>(result));
  }
  return S_OK;
}

HRESULT WebView2Platform::Impl::message_received(Impl& self, ICoreWebView2*,
                                                 ICoreWebView2WebMessageReceivedEventArgs* args) {
  if (args == nullptr) return S_OK;
  // Only the UI's own page may talk to the game.
  LPWSTR source = nullptr;
  const bool ours = SUCCEEDED(args->get_Source(&source)) && starts_with(source, self.origin);
  CoTaskMemFree(source);
  if (!ours) {
    ++self.rejected;
    return S_OK;
  }
  // Text messages are the backend's own (renderer.js: input, frames);
  // objects are the UI's actions.
  LPWSTR text = nullptr;
  if (SUCCEEDED(args->TryGetWebMessageAsString(&text)) && text != nullptr) {
    self.handle_page_text(text);
    CoTaskMemFree(text);
    return S_OK;
  }
  CoTaskMemFree(text);
  LPWSTR json = nullptr;
  if (SUCCEEDED(args->get_WebMessageAsJson(&json)) && json != nullptr) {
    const int size = to_utf8(json, std::wcslen(json), self.ui_text.data(), kMaxUiText);
    if (size < 0) {
      self.ui.drop_from_page();
    } else {
      self.ui.from_page(self.ui_text.data(), static_cast<std::size_t>(size));
    }
  }
  CoTaskMemFree(json);
  return S_OK;
}

void WebView2Platform::Impl::handle_page_text(const wchar_t* message) {
  const std::size_t length = std::wcslen(message);
  if (length >= 3 && std::wcsncmp(message, L"sc ", 3) == 0) {
    handle_screenshot(message);
    return;
  }
  const int size = to_utf8(message, length, text.data(), kMaxPageText);
  if (size < 0) {
    ++rejected;
    return;
  }
  const std::size_t bytes = static_cast<std::size_t>(size);
  switch (classify_page_message(text.data(), bytes)) {
    case PageMessage::kInput:
      if (!input.queue(text.data(), bytes)) ++rejected;
      break;
    case PageMessage::kFrame: {
      FrameRequest request;
      if (!parse_frame_request(text.data(), bytes, request)) {
        ++rejected;
        break;
      }
      renderer.set_output_size(request.width, request.height);
      flow.on_request(request);
      break;
    }
    case PageMessage::kHello:
      // The page's WebGL is ready (first load or a reload): a new
      // generation of the buffer, and every texture again.
      hello = true;
      post_buffer();
      break;
    case PageMessage::kProbe:
      if (parse_probe(text.data(), bytes, probe)) {
        has_probe = true;
      } else {
        ++rejected;
      }
      break;
    case PageMessage::kScreenshot:
    case PageMessage::kUnknown:
      ++rejected;
      break;
  }
}

void WebView2Platform::Impl::handle_screenshot(const wchar_t* message) {
  if (screenshot_path.empty()) {
    ++rejected;  // nobody asked
    return;
  }
  const std::string text_utf8 = to_utf8(message);
  const std::string prefix = "sc data:image/png;base64,";
  std::vector<std::uint8_t> png;
  if (text_utf8.compare(0, prefix.size(), prefix) != 0 || !decode_base64(text_utf8, prefix.size(), png) ||
      png.size() < 8 || png[1] != 'P' || png[2] != 'N' || png[3] != 'G') {
    std::fprintf(stderr, "webview2: the page sent no PNG for the screenshot\n");
    screenshot_path.clear();
    return;
  }
  if (write_file(screenshot_path, png)) {
    ++screenshots;
  } else {
    std::fprintf(stderr, "webview2: cannot write the screenshot\n");
  }
  screenshot_path.clear();
  // The whole window, UI included: WebView2 draws it into a PNG stream.
  if (!window_shot_path.empty() && webview && !capture_stream) {
    if (SUCCEEDED(SHCreateStreamOnFileEx(window_shot_path.c_str(), STGM_CREATE | STGM_WRITE, FILE_ATTRIBUTE_NORMAL,
                                         TRUE, nullptr, capture_stream.put()))) {
      if (FAILED(webview->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, capture_stream.get(),
                                         &on_capture))) {
        capture_stream.reset();
      }
    }
  }
  window_shot_path.clear();
}

bool WebView2Platform::Impl::allowed(ICoreWebView2NavigationStartingEventArgs* args) const {
  LPWSTR uri = nullptr;
  const bool ours = SUCCEEDED(args->get_Uri(&uri)) && starts_with(uri, origin);
  CoTaskMemFree(uri);
  return ours;
}

HRESULT WebView2Platform::Impl::navigation_starting(Impl& self, ICoreWebView2*,
                                                    ICoreWebView2NavigationStartingEventArgs* args) {
  if (args == nullptr) return S_OK;
  if (!self.allowed(args)) {
    // The game window shows the game's UI and nothing else (a link in it,
    // or a script, cannot take it to another site).
    args->put_Cancel(TRUE);
    return S_OK;
  }
  // A (re)load: the new page has no textures and no buffer, and has seen no view.
  self.ui.page_gone();
  self.flow.page_gone();
  self.hello = false;
  return S_OK;
}

HRESULT WebView2Platform::Impl::frame_navigation_starting(Impl& self, ICoreWebView2*,
                                                          ICoreWebView2NavigationStartingEventArgs* args) {
  // An iframe (one the UI made, or one a script injected) is held to the
  // same rule as the page: our origin only. Nothing else changes: the page
  // and its buffer stay.
  if (args != nullptr && !self.allowed(args)) args->put_Cancel(TRUE);
  return S_OK;
}

HRESULT WebView2Platform::Impl::navigation_completed(Impl& self, ICoreWebView2*,
                                                     ICoreWebView2NavigationCompletedEventArgs* args) {
  BOOL success = FALSE;
  if (args != nullptr) args->get_IsSuccess(&success);
  if (success) {
    self.ui.page_ready();  // sends the last view again
  } else {
    COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
    if (args != nullptr) args->get_WebErrorStatus(&status);
    std::fprintf(stderr, "webview2: the UI page did not load (web error %d)\n", static_cast<int>(status));
  }
  return S_OK;
}

HRESULT WebView2Platform::Impl::new_window(Impl&, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) {
  if (args != nullptr) args->put_Handled(TRUE);  // no pop-up windows
  return S_OK;
}

HRESULT WebView2Platform::Impl::process_failed(Impl& self, ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) {
  COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
  if (args != nullptr) args->get_ProcessFailedKind(&kind);
  if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED) {
    std::fprintf(stderr, "webview2: the browser process exited; closing\n");
    self.close_requested = true;
  } else if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
             kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE) {
    std::fprintf(stderr, "webview2: the page's process failed; reloading it\n");
    if (self.webview) self.webview->Reload();
  }
  return S_OK;
}

bool WebView2Platform::Impl::dispatch_messages() {
  MSG message;
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    if (message.message == WM_QUIT) {
      close_requested = true;
      return false;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return true;
}

bool WebView2Platform::Impl::wait_until(const bool& done, double deadline) {
  while (!done) {
    if (!dispatch_messages()) return false;
    if (done) break;
    const double left = deadline - clock();
    if (!(left > 0.0)) return false;
    MsgWaitForMultipleObjectsEx(0, nullptr, wait_milliseconds(left), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  }
  return true;
}

void WebView2Platform::Impl::fit_to_window() {
  if (!controller || hwnd == nullptr) return;
  RECT bounds;
  GetClientRect(hwnd, &bounds);
  controller->put_Bounds(bounds);
}

bool WebView2Platform::Impl::make_buffer(std::size_t slot_bytes) {
  Com<ICoreWebView2SharedBuffer> created;
  const UINT64 size = static_cast<UINT64>(kBufferHeaderBytes + kSlotCount * slot_bytes);
  if (!environment12 || FAILED(environment12->CreateSharedBuffer(size, created.put())) || !created) return false;
  BYTE* bytes = nullptr;
  if (FAILED(created->get_Buffer(&bytes)) || bytes == nullptr) return false;
  // The page lets go of the old buffer when the new one arrives.
  buffer.take(created.get());
  buffer_bytes = bytes;
  slot_capacity = slot_bytes;
  return true;
}

void WebView2Platform::Impl::post_buffer() {
  if (!buffer || !webview17) return;
  const std::uint32_t generation = flow.new_generation();
  write_buffer_header(buffer_bytes, static_cast<std::uint32_t>(slot_capacity), generation);
  wchar_t data[64];
  std::swprintf(data, 64, L"{\"generation\":%u}", static_cast<unsigned>(generation));
  webview17->PostSharedBufferToScript(buffer.get(), COREWEBVIEW2_SHARED_BUFFER_ACCESS_READ_ONLY, data);
  textures_lost = true;  // the page may be new: queue every texture again (at the next pump)
}

void WebView2Platform::Impl::post_internal(const wchar_t* message) {
  // A leading \x01 marks the backend's own messages: renderer.js takes them
  // before the React bridge sees them.
  if (webview) webview->PostWebMessageAsString(message);
}

bool WebView2Platform::Impl::take_frame(const std::uint8_t* bytes, std::size_t size, std::uint32_t seq) {
  if (buffer_bytes == nullptr || !flow.can_send()) return false;
  if (size > slot_capacity) {
    // Grow: twice the size, at least this frame, at most kMaxSlotBytes. The
    // frame is dropped; the next one goes into the new buffer once the page
    // holds it, with every texture again (it is a new generation).
    std::size_t grown = slot_capacity * 2;
    while (grown < size) grown *= 2;
    if (grown > kMaxSlotBytes) grown = kMaxSlotBytes;
    if (size <= grown && make_buffer(grown)) post_buffer();
    return false;
  }
  const std::uint32_t slot = FrameFlow::slot_for(seq);
  std::memcpy(buffer_bytes + kBufferHeaderBytes + slot * slot_capacity, bytes, size);
  std::swprintf(frame_text.data(), frame_text.size(), L"\x01" L"f %u %u %u %u", static_cast<unsigned>(seq),
                static_cast<unsigned>(slot), static_cast<unsigned>(flow.generation()), static_cast<unsigned>(size));
  if (!webview || FAILED(webview->PostWebMessageAsString(frame_text.data()))) return false;
  flow.sent(seq);
  return true;
}

bool WebView2Platform::Impl::post_view(const char* json, std::size_t size) {
  if (!webview || !to_wide(json, size, view_text.data(), view_text.size())) return false;
  return SUCCEEDED(webview->PostWebMessageAsJson(view_text.data()));
}

LRESULT CALLBACK WebView2Platform::Impl::window_procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self != nullptr) {
    switch (message) {
      case WM_SIZE:
        self->fit_to_window();
        return 0;
      case WM_MOVE:
        if (self->controller) self->controller->NotifyParentWindowPositionChanged();
        return 0;
      case WM_DPICHANGED: {
        // Moved to a screen with another scale: take the size Windows suggests.
        const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
      }
      case WM_SETFOCUS:
        if (self->controller) self->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        return 0;
      case WM_CLOSE:
        self->close_requested = true;  // the app decides; the destructor destroys the window
        return 0;
      case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = 320;
        info->ptMinTrackSize.y = 240;
        return 0;
      }
      case WM_ERASEBKGND:
        return 1;  // the WebView covers everything
      default:
        break;
    }
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

// ------------------------------------------------------------------ WebView2Platform

WebView2Platform::WebView2Platform(Passkey, std::unique_ptr<Impl> impl) : m(std::move(impl)) {}

WebView2Platform::~WebView2Platform() = default;

std::unique_ptr<WebView2Platform> WebView2Platform::create(const WindowDesc& desc, const WebViewOptions& options,
                                                           std::string& error, CreateFailure& failure) {
  failure = CreateFailure::kOther;
  if (!options.clock) {
    error = "no clock to wait for WebView2 with";
    return nullptr;
  }
  auto impl = std::make_unique<Impl>();
  Impl& s = *impl;
  s.clock = options.clock;
  // One deadline for every wait below, so the whole start is bounded.
  const double deadline = s.clock() + options.start_timeout_seconds;
  s.developer = options.developer;
  s.url = to_wide(options.url);
  s.origin = origin_of(s.url);
  s.folder = to_wide(options.folder);
  if (s.url.empty()) {
    error = "no UI page to load";
    return nullptr;
  }

  // The loader: WebView2LoaderStatic.lib with MSVC; with MinGW,
  // WebView2Loader.dll next to the exe, loaded here (see cmake/ScrapsWebView2.cmake).
#if SCRAPS_WEBVIEW2_DYNAMIC_LOADER
  using VersionFunction = HRESULT(STDAPICALLTYPE*)(PCWSTR, LPWSTR*);
  using CreateFunction = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                                  ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
  // From the program's own folder (and Windows' for what it needs), never
  // from the working folder or PATH, where another DLL of that name may be.
  HMODULE loader = LoadLibraryExW(L"WebView2Loader.dll", nullptr,
                                  LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (loader == nullptr) {
    error = "WebView2Loader.dll is missing (it belongs next to the program)";
    return nullptr;
  }
  auto available_version = reinterpret_cast<VersionFunction>(
      reinterpret_cast<void*>(GetProcAddress(loader, "GetAvailableCoreWebView2BrowserVersionString")));
  auto create_environment = reinterpret_cast<CreateFunction>(
      reinterpret_cast<void*>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions")));
  if (available_version == nullptr || create_environment == nullptr) {
    error = "WebView2Loader.dll is not the expected one";
    return nullptr;
  }
#else
  auto available_version = &GetAvailableCoreWebView2BrowserVersionString;
  auto create_environment = &CreateCoreWebView2EnvironmentWithOptions;
#endif

  // Before anything else: is the runtime there at all?
  LPWSTR version = nullptr;
  const HRESULT found = available_version(nullptr, &version);
  const bool has_runtime = SUCCEEDED(found) && version != nullptr;
  CoTaskMemFree(version);
  if (!has_runtime) {
    failure = CreateFailure::kNoRuntime;
    error = "the Microsoft Edge WebView2 runtime is not installed (https://go.microsoft.com/fwlink/p/?LinkId=2124703)";
    return nullptr;
  }

  // Sharp text and canvas on high-DPI screens: the page renders at the
  // monitor's scale and reports devicePixelRatio.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  s.com_initialized = SUCCEEDED(com);  // S_FALSE too: already initialized here, released once each
  if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
    error = "COM could not start";
    return nullptr;
  }

  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = &Impl::window_procedure;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
  window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
  window_class.hbrBackground = nullptr;
  window_class.lpszClassName = kWindowClass;
  RegisterClassExW(&window_class);  // fails harmlessly if a second window registers it again

  // desc.width x desc.height in CSS pixels: scaled by the screen's DPI.
  const UINT dpi = GetDpiForSystem();
  RECT frame{0, 0, MulDiv(desc.width, static_cast<int>(dpi), 96), MulDiv(desc.height, static_cast<int>(dpi), 96)};
  const DWORD style = WS_OVERLAPPEDWINDOW;
  AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpi);
  // Centred on the main screen's work area and never bigger than it (with
  // CW_USEDEFAULT Windows may pick another size for a large window).
  int width = frame.right - frame.left;
  int height = frame.bottom - frame.top;
  RECT work{0, 0, width, height};
  MONITORINFO monitor{};
  monitor.cbSize = sizeof(monitor);
  if (GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor)) work = monitor.rcWork;
  if (width > work.right - work.left) width = work.right - work.left;
  if (height > work.bottom - work.top) height = work.bottom - work.top;
  const int left = work.left + (work.right - work.left - width) / 2;
  const int top = work.top + (work.bottom - work.top - height) / 2;
  const std::wstring title = to_wide(desc.title != nullptr ? desc.title : "Scraps");
  s.hwnd = CreateWindowExW(0, kWindowClass, title.c_str(), style, left, top, width, height, nullptr, nullptr, instance,
                           nullptr);
  if (s.hwnd == nullptr) {
    failure = CreateFailure::kNoDesktop;  // a window station without a desktop (a service)
    error = "cannot create the window";
    return nullptr;
  }
  SetWindowLongPtrW(s.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));
  ShowWindow(s.hwnd, SW_SHOWNORMAL);
  UpdateWindow(s.hwnd);

  // The profile folder: per user, never next to the exe (Program Files is read-only).
  std::wstring data_dir = to_wide(options.user_data_dir);
  for (wchar_t& c : data_dir) {
    if (c == L'/') c = L'\\';  // a Windows path, whichever way it was written
  }
  if (data_dir.empty()) {
    wchar_t local[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (length > 0 && length < MAX_PATH) data_dir = std::wstring(local) + L"\\Scraps\\WebView2";
  }

  const HRESULT created =
      create_environment(nullptr, data_dir.empty() ? nullptr : data_dir.c_str(), nullptr, &s.on_environment);
  if (FAILED(created) || !s.wait_until(s.environment_done, deadline) || !s.environment) {
    error = "WebView2 did not start";
    return nullptr;
  }
  if (FAILED(s.environment->CreateCoreWebView2Controller(s.hwnd, &s.on_controller)) ||
      !s.wait_until(s.controller_done, deadline) || !s.controller) {
    // Without a desktop to show it on (locked, disconnected) the control
    // cannot be made: that is no usable desktop, not a broken build.
    if (!has_input_desktop()) failure = CreateFailure::kNoDesktop;
    error = "the WebView2 control could not be created";
    return nullptr;
  }
  if (FAILED(s.controller->get_CoreWebView2(s.webview.put())) || !s.webview) {
    error = "the WebView2 control has no web view";
    return nullptr;
  }
  Com<ICoreWebView2_3> webview3;
  if (!s.webview.query(kIidWebView3, webview3) || !s.webview.query(kIidWebView17, s.webview17) ||
      !s.environment.query(kIidEnvironment12, s.environment12)) {
    error = "the WebView2 runtime is too old (shared buffers need version 114 or newer)";
    return nullptr;
  }

  // Behaviour: no browser around the game.
  Com<ICoreWebView2Settings> settings;
  if (SUCCEEDED(s.webview->get_Settings(settings.put())) && settings) {
    settings->put_AreDevToolsEnabled(s.developer ? TRUE : FALSE);
    settings->put_AreDefaultContextMenusEnabled(s.developer ? TRUE : FALSE);
    settings->put_IsStatusBarEnabled(FALSE);
    settings->put_IsZoomControlEnabled(FALSE);
    Com<ICoreWebView2Settings3> settings3;
    if (settings.query(kIidSettings3, settings3)) {
      // F5, Ctrl+R, Ctrl+F, F12...: off in release builds (F5 is quick save).
      settings3->put_AreBrowserAcceleratorKeysEnabled(s.developer ? TRUE : FALSE);
    }
  }
  Com<ICoreWebView2Controller2> controller2;
  if (s.controller.query(kIidController2, controller2)) {
    COREWEBVIEW2_COLOR dark{255, 16, 18, 24};  // no white flash before the page draws
    controller2->put_DefaultBackgroundColor(dark);
  }
  s.fit_to_window();
  s.controller->put_IsVisible(TRUE);

  s.webview->add_WebMessageReceived(&s.on_message, &s.message_token);
  s.webview->add_NavigationStarting(&s.on_starting, &s.starting_token);
  s.webview->add_FrameNavigationStarting(&s.on_frame_starting, &s.frame_starting_token);
  s.webview->add_NavigationCompleted(&s.on_completed, &s.completed_token);
  s.webview->add_NewWindowRequested(&s.on_new_window, &s.new_window_token);
  s.webview->add_ProcessFailed(&s.on_failed, &s.failed_token);
  s.tokens_added = true;

  // renderer.js runs in every document the page loads, before its scripts.
  const std::wstring script =
      to_wide(std::string(reinterpret_cast<const char*>(kRendererJs), sizeof(kRendererJs) - 1));
  if (script.empty() || FAILED(s.webview->AddScriptToExecuteOnDocumentCreated(script.c_str(), &s.on_script)) ||
      !s.wait_until(s.script_done, deadline) || FAILED(s.create_result)) {
    error = "cannot add the renderer script to the page";
    return nullptr;
  }
  if (!s.make_buffer(kInitialSlotBytes)) {
    error = "cannot create the shared buffer for frames";
    return nullptr;
  }
  if (!s.folder.empty()) {
    const std::wstring host = to_wide(kUiHostName);
    if (FAILED(webview3->SetVirtualHostNameToFolderMapping(host.c_str(), s.folder.c_str(),
                                                           COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS))) {
      error = "cannot serve the UI folder " + options.folder;
      return nullptr;
    }
  }
  if (FAILED(s.webview->Navigate(s.url.c_str()))) {
    error = "cannot open " + options.url;
    return nullptr;
  }
  s.controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
  failure = CreateFailure::kNone;
  return std::make_unique<WebView2Platform>(Passkey(), std::move(impl));
}

PlatformServices WebView2Platform::services() {
  PlatformServices services;
  services.window = this;
  services.input = &m->input;
  services.renderer = &m->renderer;
  services.audio = nullptr;
  services.ui = &m->ui;
  return services;
}

void WebView2Platform::set_gamepad(const Input* pad) { m->input.set_gamepad(pad); }

bool WebView2Platform::wait_for_frame(double max_seconds) {
  // The frame before is over: whatever it did not answer needs a new request.
  m->flow.frame_done();
  if (!m->dispatch_messages() || m->close_requested) return false;
  if (m->flow.take_request()) return true;
  MsgWaitForMultipleObjectsEx(0, nullptr, wait_milliseconds(max_seconds), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  if (!m->dispatch_messages() || m->close_requested) return false;
  return m->flow.take_request();
}

void WebView2Platform::pump_events() {
  m->dispatch_messages();
  if (m->textures_lost) {
    m->textures_lost = false;
    m->renderer.resend_textures();
  }
  m->input.pump(GetForegroundWindow() == m->hwnd);
}

bool WebView2Platform::close_requested() const { return m->close_requested; }

void WebView2Platform::request_close() { m->close_requested = true; }

void WebView2Platform::set_title(const char* title) {
  if (title == nullptr) return;
  const std::wstring wide = to_wide(std::string(title));
  SetWindowTextW(m->hwnd, wide.c_str());
}

void WebView2Platform::click_play() { m->post_internal(L"\x01p"); }

void WebView2Platform::request_probe() {
  m->has_probe = false;
  m->post_internal(L"\x01q");
}

bool WebView2Platform::has_probe() const { return m->has_probe; }

const ProbeResult& WebView2Platform::probe() const { return m->probe; }

void WebView2Platform::request_screenshot(const std::string& path) {
  m->screenshot_path = to_wide(path);
  std::string window_path = path;
  const std::size_t dot = window_path.rfind('.');
  const std::size_t slash = window_path.find_last_of("/\\");
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) window_path.erase(dot);
  m->window_shot_path = to_wide(window_path + "-window.png");
  m->post_internal(L"\x01s");
}

int WebView2Platform::screenshots_written() const { return m->screenshots; }

bool WebView2Platform::page_ready() const { return m->hello; }

bool WebView2Platform::on_screen() const {
  return m->hwnd != nullptr && IsWindowVisible(m->hwnd) && !IsIconic(m->hwnd) && has_input_desktop();
}

std::uint64_t WebView2Platform::frames_drawn() const { return m->flow.frames_drawn(); }

std::size_t WebView2Platform::views_sent() const { return m->ui.views_sent(); }

std::size_t WebView2Platform::rejected_messages() const { return m->rejected + m->input.rejected(); }

std::vector<std::string> command_line_utf8() {
  std::vector<std::string> arguments;
  int count = 0;
  LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
  if (wide == nullptr) return arguments;
  for (int i = 0; i < count; ++i) arguments.push_back(to_utf8(wide[i]));
  LocalFree(wide);
  return arguments;
}

void attach_parent_console() {
  HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
  if (output != nullptr && output != INVALID_HANDLE_VALUE && GetFileType(output) != FILE_TYPE_UNKNOWN) return;
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
  FILE* out = nullptr;
  FILE* err = nullptr;
#ifdef _MSC_VER
  freopen_s(&out, "CONOUT$", "w", stdout);
  freopen_s(&err, "CONOUT$", "w", stderr);
#else
  out = std::freopen("CONOUT$", "w", stdout);
  err = std::freopen("CONOUT$", "w", stderr);
#endif
  (void)out;
  (void)err;
}

}  // namespace scraps::webview2
