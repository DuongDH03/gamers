# The WebView2 SDK (Microsoft.Web.WebView2 NuGet package, MIT licence), for
# the webview2 backend and scraps_web (UI layout A). Windows only.
#
# Sets SCRAPS_WEBVIEW2_SDK to TRUE and defines the interface target
# scraps_webview2_sdk (headers + loader) when it is available.
#
# The loader:
#   MSVC   WebView2LoaderStatic.lib, linked in: nothing extra to ship.
#   MinGW  WebView2Loader.dll, loaded at run time with LoadLibrary (the
#          static .lib is MSVC-only). It is copied next to scraps_web.exe and
#          installed with it; SCRAPS_WEBVIEW2_LOADER_DLL names it.
# The headers are plain COM (MIDL output) and build with both compilers; they
# are included as SYSTEM headers so their style never trips -Werror or /WX.

set(SCRAPS_WEBVIEW2_SDK FALSE)
set(SCRAPS_WEBVIEW2_LOADER_DLL "")

option(SCRAPS_WITH_WEBVIEW2 "Build the WebView2 backend and scraps_web (Windows)" ON)

if(WIN32 AND SCRAPS_WITH_WEBVIEW2)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS "Scraps: scraps_web is built for 64-bit Windows only; skipping it")
    return()
  endif()
  include(FetchContent)
  set(scraps_webview2_version 1.0.4314.35)
  message(STATUS "Scraps: WebView2 SDK ${scraps_webview2_version} (NuGet)")
  FetchContent_Declare(webview2
    URL https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${scraps_webview2_version}/microsoft.web.webview2.${scraps_webview2_version}.nupkg
    URL_HASH SHA256=90778DEB1B77E36292828B4B5833C9255C27533F40E0DDD55891AE718944EDC4
    DOWNLOAD_NAME webview2.zip  # a .nupkg is a zip; the name makes CMake extract it
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(webview2)
  set(scraps_webview2_native "${webview2_SOURCE_DIR}/build/native")
  if(NOT EXISTS "${scraps_webview2_native}/include/WebView2.h")
    message(WARNING "Scraps: the WebView2 package has no build/native/include/WebView2.h; skipping scraps_web")
    return()
  endif()

  add_library(scraps_webview2_sdk INTERFACE)
  target_include_directories(scraps_webview2_sdk SYSTEM INTERFACE "${scraps_webview2_native}/include")
  if(MSVC)
    target_link_libraries(scraps_webview2_sdk INTERFACE "${scraps_webview2_native}/x64/WebView2LoaderStatic.lib")
    target_compile_definitions(scraps_webview2_sdk INTERFACE SCRAPS_WEBVIEW2_DYNAMIC_LOADER=0)
  else()
    set(SCRAPS_WEBVIEW2_LOADER_DLL "${scraps_webview2_native}/x64/WebView2Loader.dll")
    target_compile_definitions(scraps_webview2_sdk INTERFACE SCRAPS_WEBVIEW2_DYNAMIC_LOADER=1)
  endif()
  set(SCRAPS_WEBVIEW2_SDK TRUE)
endif()
