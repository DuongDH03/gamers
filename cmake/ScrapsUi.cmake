# The React UI (ui/, docs/specs/2026-11-14-offline-essentials-and-ui.md,
# part 6), built with npm into <build>/ui/dist for scraps_web.
#
#   SCRAPS_BUILD_UI (default ON): when npm is found, the target scraps_ui runs
#   `npm ci` (again only when package.json or package-lock.json change) and
#   `npm run build` (again only when a file in ui/ changes). Both run through
#   cmake/ScrapsNpm.cmake, which holds a lock on ui/.npm.lock: two presets
#   building at once share ui/node_modules/ and take turns. Without npm, or
#   with the option off, build it by hand (cd ui && npm ci && npm run build)
#   and scraps_web and the release use ui/dist.
#
# Sets SCRAPS_UI_DIST (where index.html will be), SCRAPS_UI_TARGET (the
# target that builds it, or empty), SCRAPS_NODE (node, or empty).

option(SCRAPS_BUILD_UI "Build the React UI in ui/ with npm when npm is found" ON)

set(scraps_ui_source "${PROJECT_SOURCE_DIR}/ui")
set(SCRAPS_UI_DIST "${scraps_ui_source}/dist")
set(SCRAPS_UI_TARGET "")
find_program(SCRAPS_NODE NAMES node node.exe)
find_program(SCRAPS_NPM NAMES npm.cmd npm)

if(SCRAPS_BUILD_UI AND SCRAPS_NPM AND EXISTS "${scraps_ui_source}/package-lock.json")
  set(SCRAPS_UI_DIST "${PROJECT_BINARY_DIR}/ui/dist")
  set(scraps_ui_installed "${PROJECT_BINARY_DIR}/ui/npm-ci.stamp")
  file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/ui")
  file(GLOB_RECURSE scraps_ui_sources CONFIGURE_DEPENDS "${scraps_ui_source}/src/*")
  list(APPEND scraps_ui_sources "${scraps_ui_source}/index.html" "${scraps_ui_source}/vite.config.ts"
       "${scraps_ui_source}/tsconfig.json" "${scraps_ui_source}/package.json")
  set(scraps_npm_script "${CMAKE_CURRENT_LIST_DIR}/ScrapsNpm.cmake")
  add_custom_command(
    OUTPUT "${scraps_ui_installed}"
    COMMAND ${CMAKE_COMMAND} -DSTEP=ci "-DNPM=${SCRAPS_NPM}" "-DUI_DIR=${scraps_ui_source}"
            "-DSTAMP=${scraps_ui_installed}" -P "${scraps_npm_script}"
    DEPENDS "${scraps_ui_source}/package.json" "${scraps_ui_source}/package-lock.json" "${scraps_npm_script}"
    COMMENT "UI: npm ci"
    VERBATIM)
  # Built into this build folder, so two presets never overwrite each other.
  add_custom_command(
    OUTPUT "${SCRAPS_UI_DIST}/index.html"
    COMMAND ${CMAKE_COMMAND} -DSTEP=build "-DNPM=${SCRAPS_NPM}" "-DUI_DIR=${scraps_ui_source}"
            "-DOUT_DIR=${SCRAPS_UI_DIST}" -P "${scraps_npm_script}"
    DEPENDS "${scraps_ui_installed}" ${scraps_ui_sources}
    COMMENT "UI: npm run build -> ${SCRAPS_UI_DIST}"
    VERBATIM)
  add_custom_target(scraps_ui ALL DEPENDS "${SCRAPS_UI_DIST}/index.html")
  set(SCRAPS_UI_TARGET scraps_ui)
  message(STATUS "Scraps: the UI is built with ${SCRAPS_NPM} into ${SCRAPS_UI_DIST}")
else()
  if(NOT SCRAPS_BUILD_UI)
    set(scraps_ui_why "SCRAPS_BUILD_UI is OFF")
  elseif(NOT SCRAPS_NPM)
    set(scraps_ui_why "npm not found")
  else()
    set(scraps_ui_why "ui/package-lock.json is missing")
  endif()
  if(EXISTS "${SCRAPS_UI_DIST}/index.html")
    message(STATUS "Scraps: the UI is not built here (${scraps_ui_why}); scraps_web and the release use "
                   "the hand-built ${SCRAPS_UI_DIST}")
  else()
    message(STATUS "Scraps: the UI is not built here (${scraps_ui_why}); scraps_web and the release use "
                   "${SCRAPS_UI_DIST} if it is there, and it is not: build it (cd ui && npm ci && npm run build) "
                   "or scraps_web cannot start and the package_runs test fails")
  endif()
endif()
