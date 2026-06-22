# ArcadeLauncher Stream Engine — GPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)

# Engine version from the VERSION file (single source of truth).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" ENGINE_VERSION)
string(STRIP "${ENGINE_VERSION}" ENGINE_VERSION)

project(arcade-stream-engine
  VERSION ${ENGINE_VERSION}
  DESCRIPTION "ArcadeLauncher combined GameStream engine (Sunshine host + Moonlight client)"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Forks are vendored under vendor/ (git submodules). Toggle for incremental bring-up.
option(ASE_BUILD_HOST   "Build host mode (Sunshine fork)"   ON)
option(ASE_BUILD_CLIENT "Build client mode (Moonlight fork)" ON)

# IPC core (transport-agnostic) — also reused by the test target below.
set(ASE_IPC_SOURCES
  src/ipc/ipc.cpp
  src/ipc/json.cpp
  src/ipc/frame.cpp
  src/ipc/server.cpp
)

# Platform byte transport (named pipe on Windows, Unix domain socket elsewhere).
if(WIN32)
  set(ASE_TRANSPORT_SOURCE src/ipc/transport_win.cpp)
else()
  set(ASE_TRANSPORT_SOURCE src/ipc/transport_posix.cpp)
endif()

add_executable(arcade-stream-engine
  src/main.cpp
  src/host/host_main.cpp
  src/client/stream_main.cpp
  ${ASE_IPC_SOURCES}
  ${ASE_TRANSPORT_SOURCE}
)

target_include_directories(arcade-stream-engine PRIVATE src)
target_compile_definitions(arcade-stream-engine PRIVATE
  ENGINE_VERSION="${ENGINE_VERSION}"
  ASE_IPC_PROTOCOL_VERSION=1
)

# --- Fork integration (wired during build bring-up; see docs/BUILD.md) ----------------------
# Client mode links moonlight-common-c (the CMake GameStream client core); the full qmake-based
# moonlight-qt app is NOT used — the engine owns its renderer/input (SDL2 + HW decode).
set(ASE_MOONLIGHT_COMMON_C "${CMAKE_CURRENT_SOURCE_DIR}/vendor/moonlight/app/moonlight-common-c")
if(ASE_BUILD_CLIENT AND EXISTS "${ASE_MOONLIGHT_COMMON_C}/CMakeLists.txt")
  add_subdirectory(${ASE_MOONLIGHT_COMMON_C} moonlight-common-c EXCLUDE_FROM_ALL)
  target_link_libraries(arcade-stream-engine PRIVATE moonlight-common-c)
  target_compile_definitions(arcade-stream-engine PRIVATE ASE_HAVE_MOONLIGHT=1)
endif()

# Host mode: first milestone is building the Sunshine fork standalone (prove deps), then
# carving out the host control surface. Link target name confirmed during that step.
# if(ASE_BUILD_HOST AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sunshine/CMakeLists.txt")
#   add_subdirectory(vendor/sunshine EXCLUDE_FROM_ALL)
#   target_link_libraries(arcade-stream-engine PRIVATE <sunshine-host-lib>)
#   target_compile_definitions(arcade-stream-engine PRIVATE ASE_HAVE_SUNSHINE=1)
# endif()

install(TARGETS arcade-stream-engine RUNTIME DESTINATION bin)

# --- Tests (fork-independent: JSON, framing, handshake/dispatch over an in-memory transport) --
option(ASE_BUILD_TESTS "Build unit tests" ON)
if(ASE_BUILD_TESTS)
  enable_testing()
  add_executable(ase_tests
    tests/test_ipc.cpp
    ${ASE_IPC_SOURCES}
  )
  target_include_directories(ase_tests PRIVATE src)
  target_compile_definitions(ase_tests PRIVATE
    ENGINE_VERSION="${ENGINE_VERSION}"
    ASE_IPC_PROTOCOL_VERSION=1
  )
  add_test(NAME ase_tests COMMAND ase_tests)
endif()
