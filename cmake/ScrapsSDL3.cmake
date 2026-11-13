# Finds or downloads SDL3. Sets SCRAPS_SDL3_TARGET to the target to link,
# or to an empty string when the SDL3 backend is not built, and
# SCRAPS_SDL3_LICENSE to SDL's LICENSE.txt (zlib) when it was downloaded:
# it is linked into the programs, so the release ships it as licenses/SDL3.txt.
#
# SDL3 is only ever linked by engine/backends/sdl3 (Rule 3).

set(SCRAPS_SDL3_TARGET "")
set(SCRAPS_SDL3_LICENSE "")

if(SCRAPS_WITH_SDL3)
  find_package(SDL3 CONFIG QUIET)
  if(SDL3_FOUND)
    set(SCRAPS_SDL3_TARGET SDL3::SDL3)
    message(STATUS "Scraps: using installed SDL3 ${SDL3_VERSION}")
  elseif(SCRAPS_FETCH_SDL3)
    include(FetchContent)
    message(STATUS "Scraps: SDL3 not installed, downloading release-3.4.20")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
      URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.20.tar.gz
      URL_HASH SHA256=5CB56D4223A41283F3C2BAF6F1263FBA171692409AE405A50417A7C8E7E28DEB
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      SYSTEM)  # SDL's headers never trip our warnings-as-errors
    FetchContent_MakeAvailable(SDL3)
    set(SCRAPS_SDL3_TARGET SDL3::SDL3-static)
    if(EXISTS "${sdl3_SOURCE_DIR}/LICENSE.txt")
      set(SCRAPS_SDL3_LICENSE "${sdl3_SOURCE_DIR}/LICENSE.txt")
    endif()
  else()
    message(WARNING "Scraps: SDL3 not found and SCRAPS_FETCH_SDL3 is OFF. "
                    "Only the headless backend will be built.")
  endif()
endif()
