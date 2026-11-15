cmake_minimum_required(VERSION 3.16)
project(mdview VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Static CRT everywhere (also inside litehtml): the plugin and app need no
# Visual C++ redistributable.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(WIN32)
  add_compile_definitions(UNICODE _UNICODE NOMINMAX)
endif()

# --- third party (vendored, built from source) -------------------------------
set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/litehtml EXCLUDE_FROM_ALL)

add_library(md4c STATIC
  third_party/md4c/md4c.c
  third_party/md4c/md4c-html.c
  third_party/md4c/entity.c)
target_include_directories(md4c PUBLIC third_party/md4c)

# Our own targets get strict warnings; third-party code keeps its defaults.
function(mdview_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive- /external:anglebrackets /external:W0)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# --- portable core: file -> UTF-8 -> chunks -> HTML (no GUI types) -----------
add_library(mdview_core STATIC src/core/document.cpp)
target_include_directories(mdview_core PUBLIC src)
target_link_libraries(mdview_core PUBLIC md4c)
mdview_warnings(mdview_core)

# --- portable renderer: virtualized litehtml view ----------------------------
add_library(mdview_render STATIC src/render/view.cpp)
target_link_libraries(mdview_render PUBLIC mdview_core litehtml)
mdview_warnings(mdview_render)

# --- Windows frontend --------------------------------------------------------
if(WIN32)
  set(LH_WIN third_party/litehtml/containers/windows)
  add_library(mdview_win32 STATIC
    ${LH_WIN}/win32/win32_container.cpp
    ${LH_WIN}/gdiplus/gdiplus_container.cpp
    src/platform/win32/container.cpp
    src/platform/win32/view_window.cpp)
  target_include_directories(mdview_win32 SYSTEM PUBLIC ${LH_WIN})
  target_link_libraries(mdview_win32 PUBLIC mdview_render gdiplus shlwapi user32 gdi32 shell32 msimg32)
  # GDI+ headers need min/max in scope despite NOMINMAX.
  target_compile_options(mdview_win32 PUBLIC
    "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/gdiplus_minmax.h")
  if(MSVC)
    # Warnings only for our files in this target.
    set_source_files_properties(src/platform/win32/container.cpp src/platform/win32/view_window.cpp
      PROPERTIES COMPILE_OPTIONS "/W4;/utf-8;/permissive-;/external:anglebrackets;/external:W0")
  endif()

  add_executable(mdview WIN32 apps/mdview/main_win32.cpp)
  target_link_libraries(mdview PRIVATE mdview_win32)
  mdview_warnings(mdview)

  # Lister plugin for Total Commander / Double Commander.
  add_library(mdview_wlx SHARED plugins/wlx/wlx_plugin.cpp plugins/wlx/mdview.def)
  target_link_libraries(mdview_wlx PRIVATE mdview_win32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WLX_SUFFIX ".wlx64")
  else()
    set(WLX_SUFFIX ".wlx")
  endif()
  set_target_properties(mdview_wlx PROPERTIES OUTPUT_NAME mdview PREFIX "" SUFFIX ${WLX_SUFFIX})
  mdview_warnings(mdview_wlx)
  set(MDVIEW_PLATFORM mdview_win32)
else()
  # --- cairo/pango frontend (Linux, BSD) -------------------------------------
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(CAIRO_PANGO REQUIRED IMPORTED_TARGET cairo pangocairo)
  set(LH_CAIRO third_party/litehtml/containers/cairo)
  add_library(mdview_cairo STATIC
    ${LH_CAIRO}/container_cairo.cpp
    ${LH_CAIRO}/container_cairo_pango.cpp
    ${LH_CAIRO}/cairo_borders.cpp
    ${LH_CAIRO}/conic_gradient.cpp
    src/platform/cairo/container.cpp)
  target_include_directories(mdview_cairo SYSTEM PUBLIC ${LH_CAIRO})
  target_link_libraries(mdview_cairo PUBLIC mdview_render PkgConfig::CAIRO_PANGO)
  set_source_files_properties(src/platform/cairo/container.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

  add_executable(mdview apps/mdview/main_cli.cpp)
  target_link_libraries(mdview PRIVATE mdview_cairo)
  mdview_warnings(mdview)
  set(MDVIEW_PLATFORM mdview_cairo)
endif()

# --- tests -------------------------------------------------------------------
enable_testing()
add_executable(mdview-core-tests tests/core_tests.cpp)
target_link_libraries(mdview-core-tests PRIVATE mdview_core)
mdview_warnings(mdview-core-tests)
add_test(NAME core COMMAND mdview-core-tests)

add_executable(mdview-render-tests tests/render_tests.cpp)
target_link_libraries(mdview-render-tests PRIVATE ${MDVIEW_PLATFORM})
target_compile_definitions(mdview-render-tests PRIVATE
  MDVIEW_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
mdview_warnings(mdview-render-tests)
add_test(NAME render COMMAND mdview-render-tests)

if(WIN32)
  # Fake lister host: loads a .wlx like Total Commander and reports UI stalls.
  #   mdview-lister-host build\mdview.wlx64 file.md 10
  add_executable(mdview-lister-host tests/harness/lister_host.cpp)
  target_link_libraries(mdview-lister-host PRIVATE user32 ole32)
endif()
