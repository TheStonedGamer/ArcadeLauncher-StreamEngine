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
  src/client/stream_config.cpp
  src/net/gamestream_xml.cpp
  ${ASE_IPC_SOURCES}
  ${ASE_TRANSPORT_SOURCE}
)

target_include_directories(arcade-stream-engine PRIVATE src)
target_compile_definitions(arcade-stream-engine PRIVATE
  ENGINE_VERSION="${ENGINE_VERSION}"
  ASE_IPC_PROTOCOL_VERSION=1
)

# --- Fork integration (milestone 2+; see docs/BUILD.md) -------------------------------------
# The forks are vendored under vendor/ (submodules), but linking them pulls heavy build deps
# (FFmpeg, OpenSSL, SDL2, NVENC/AMF, …), so it is OPT-IN behind explicit options. With both
# OFF (the default) the engine builds as the fork-independent CLI + IPC server — keeping the
# skeleton build and CI green until each milestone's deps are installed on the runners.
#
# Client mode links moonlight-common-c (the CMake GameStream client core); the full qmake-based
# moonlight-qt app is NOT used — the engine owns its renderer/input (SDL2 + HW decode).
option(ASE_LINK_MOONLIGHT "Link the vendored moonlight-common-c (needs its build deps)" OFF)
set(ASE_MOONLIGHT_COMMON_C "${CMAKE_CURRENT_SOURCE_DIR}/vendor/moonlight/moonlight-common-c/moonlight-common-c")
if(ASE_BUILD_CLIENT AND ASE_LINK_MOONLIGHT)
  if(NOT EXISTS "${ASE_MOONLIGHT_COMMON_C}/CMakeLists.txt")
    message(FATAL_ERROR "ASE_LINK_MOONLIGHT=ON but ${ASE_MOONLIGHT_COMMON_C} is missing — run scripts/vendor.sh")
  endif()
  # moonlight-common-c defaults to BUILD_SHARED_LIBS=ON (a DLL). On MSVC a DLL that exports
  # no symbols emits no import .lib, so linking it fails (LNK1181). We embed it into the single
  # engine binary, so force a STATIC build for the subdirectory, then restore the prior value.
  # Its CMakeLists only honors BUILD_SHARED_LIBS when it is already defined.
  set(_ASE_SAVED_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
  set(BUILD_SHARED_LIBS OFF)
  add_subdirectory(${ASE_MOONLIGHT_COMMON_C} moonlight-common-c EXCLUDE_FROM_ALL)
  set(BUILD_SHARED_LIBS "${_ASE_SAVED_BUILD_SHARED_LIBS}")
  target_link_libraries(arcade-stream-engine PRIVATE moonlight-common-c)
  target_compile_definitions(arcade-stream-engine PRIVATE ASE_HAVE_MOONLIGHT=1)
endif()

# Host mode: first milestone is building the Sunshine fork standalone (prove deps), then
# carving out the host control surface. Link target name confirmed during that step.
option(ASE_LINK_SUNSHINE "Link the vendored Sunshine host backend (needs its build deps)" OFF)
if(ASE_BUILD_HOST AND ASE_LINK_SUNSHINE)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sunshine/CMakeLists.txt")
    message(FATAL_ERROR "ASE_LINK_SUNSHINE=ON but vendor/sunshine is missing — run scripts/vendor.sh")
  endif()
  add_subdirectory(vendor/sunshine EXCLUDE_FROM_ALL)
  # target_link_libraries(arcade-stream-engine PRIVATE <sunshine-host-lib>)  # name TBD milestone 3
  target_compile_definitions(arcade-stream-engine PRIVATE ASE_HAVE_SUNSHINE=1)
endif()

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

  # client.start settings validation — fork-independent (only needs json).
  add_executable(ase_client_tests
    tests/test_stream_config.cpp
    src/client/stream_config.cpp
    src/ipc/json.cpp
  )
  target_include_directories(ase_client_tests PRIVATE src)
  add_test(NAME ase_client_tests COMMAND ase_client_tests)

  # GameStream XML/hex parsing (NvHTTP port) — pure, dependency-free.
  add_executable(ase_net_tests
    tests/test_gamestream_xml.cpp
    src/net/gamestream_xml.cpp
  )
  target_include_directories(ase_net_tests PRIVATE src)
  add_test(NAME ase_net_tests COMMAND ase_net_tests)
endif()
