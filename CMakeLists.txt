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
  src/host/host_apps.cpp          # Sunshine apps.json model (host.syncApps/listApps) — always built
  src/host/sunshine_backend.cpp   # drives the bundled Sunshine child (host.enable/status)
  src/host/sunshine_detect.cpp    # detect/adopt a system-installed or already-running Sunshine
  src/host/client_trust.cpp       # seed account client certs into Sunshine state (zero-PIN auto-pair)
  src/client/stream_main.cpp
  src/client/stream_config.cpp
  src/client/stream_session.cpp   # live connection driver (no-op unless ASE_HAVE_MOONLIGHT)
  src/client/stream_window.cpp    # SDL window + input (no-op unless ASE_HAVE_SDL)
  src/client/video_renderer.cpp   # FFmpeg decode + present (no-op unless ASE_HAVE_FFMPEG && ASE_HAVE_SDL)
  src/client/audio_renderer.cpp   # Opus decode + SDL audio (no-op unless ASE_HAVE_OPUS && ASE_HAVE_SDL)
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

# GameStream pairing crypto (NvHTTP port) needs OpenSSL. moonlight-common-c links OpenSSL
# PRIVATEly, so its headers/libs do NOT propagate to us — we find_package it ourselves. Default it
# ON whenever moonlight is linked (pairing only matters in a client build that has the streaming
# core), but allow forcing it on alone to build/test the crypto against a standalone OpenSSL.
option(ASE_WITH_OPENSSL "Build the GameStream pairing crypto (needs OpenSSL)" ${ASE_LINK_MOONLIGHT})
if(ASE_WITH_OPENSSL)
  find_package(OpenSSL REQUIRED)
  target_sources(arcade-stream-engine PRIVATE
    src/net/pairing_crypto.cpp   # pairing crypto primitives
    src/net/identity.cpp         # client cert/key generation
    src/net/identity_store.cpp   # persistent identity + paired-host registry
    src/net/http_client.cpp      # TLS HTTP client (client-cert + pinned server-cert)
    src/net/nv_pairing.cpp       # 5-stage pair() state machine
    src/net/nv_launch.cpp        # launch/resume handshake (app start -> RTSP session URL)
    src/net/nv_applist.cpp       # app list parse + name->id resolution (client.apps / client.start)
  )
  # http_client uses libssl (TLS), the others only libcrypto.
  target_link_libraries(arcade-stream-engine PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  target_compile_definitions(arcade-stream-engine PRIVATE ASE_HAVE_OPENSSL=1)
endif()

# Renderer deps (milestone 3): SDL2 for the window/input surface and FFmpeg for HW-accelerated
# video decode. Opt-in like the fork links — with these OFF the engine still builds + connects, just
# headless (null video sink). SDL is found via its CMake config (vcpkg on Windows, libsdl2-dev on
# Linux); FFmpeg via vcpkg's FindFFMPEG on Windows and pkg-config elsewhere.
option(ASE_LINK_SDL    "Link SDL2 for the stream window + input"        OFF)
option(ASE_LINK_FFMPEG "Link FFmpeg for hardware video decode"          OFF)
if(ASE_BUILD_CLIENT AND ASE_LINK_SDL)
  find_package(SDL2 CONFIG REQUIRED)
  target_link_libraries(arcade-stream-engine PRIVATE SDL2::SDL2)
  # We own main(); tell SDL not to hijack it (StreamWindow calls SDL_SetMainReady()).
  target_compile_definitions(arcade-stream-engine PRIVATE ASE_HAVE_SDL=1 SDL_MAIN_HANDLED=1)
endif()
if(ASE_BUILD_CLIENT AND ASE_LINK_FFMPEG)
  if(WIN32)
    find_package(FFMPEG REQUIRED)  # vcpkg ships share/ffmpeg/FindFFMPEG.cmake
    target_include_directories(arcade-stream-engine PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(arcade-stream-engine PRIVATE ${FFMPEG_LIBRARIES})
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavutil)
    target_link_libraries(arcade-stream-engine PRIVATE PkgConfig::FFMPEG)
  endif()
  target_compile_definitions(arcade-stream-engine PRIVATE ASE_HAVE_FFMPEG=1)
endif()

# Opus audio decode (rides with the renderer build; needs SDL for output). vcpkg `opus` ships a CMake
# config target (Opus::opus); Linux uses pkg-config.
option(ASE_LINK_OPUS "Link Opus for audio decode" OFF)
if(ASE_BUILD_CLIENT AND ASE_LINK_OPUS)
  if(WIN32)
    find_package(Opus CONFIG REQUIRED)
    target_link_libraries(arcade-stream-engine PRIVATE Opus::opus)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
    target_link_libraries(arcade-stream-engine PRIVATE PkgConfig::OPUS)
  endif()
  # Opus headers live under .../include/opus on every packaging (vcpkg + distro); add that dir so the
  # bare `#include <opus_multistream.h>` resolves identically on Windows and Linux.
  find_path(OPUS_HEADER_DIR opus_multistream.h PATH_SUFFIXES opus)
  if(OPUS_HEADER_DIR)
    target_include_directories(arcade-stream-engine PRIVATE ${OPUS_HEADER_DIR})
  endif()
  target_compile_definitions(arcade-stream-engine PRIVATE ASE_HAVE_OPUS=1)
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

  # App-list parse + name->id resolution (NvHTTP port) — pure (the NvHttpClient fetch overload is
  # OpenSSL-gated out), so this runs in every build like ase_net_tests.
  add_executable(ase_applist_tests
    tests/test_nv_applist.cpp
    src/net/nv_applist.cpp
    src/net/gamestream_xml.cpp
  )
  target_include_directories(ase_applist_tests PRIVATE src)
  add_test(NAME ase_applist_tests COMMAND ase_applist_tests)

  # Host apps.json (de)serialization + diff, and the pure Sunshine-binary/arg resolution helpers —
  # dependency-free (only json + std::filesystem), so this runs in every build.
  add_executable(ase_host_apps_tests
    tests/test_host_apps.cpp
    src/host/host_apps.cpp
    src/host/sunshine_backend.cpp
    src/host/sunshine_detect.cpp
    src/host/client_trust.cpp
    src/ipc/json.cpp
  )
  target_include_directories(ase_host_apps_tests PRIVATE src)
  add_test(NAME ase_host_apps_tests COMMAND ase_host_apps_tests)

  # GameStream pairing crypto (NvHTTP port) — needs OpenSSL, so only in the ASE_WITH_OPENSSL build.
  if(ASE_WITH_OPENSSL)
    add_executable(ase_crypto_tests
      tests/test_pairing_crypto.cpp
      src/net/pairing_crypto.cpp
    )
    target_include_directories(ase_crypto_tests PRIVATE src)
    target_link_libraries(ase_crypto_tests PRIVATE OpenSSL::Crypto)
    add_test(NAME ase_crypto_tests COMMAND ase_crypto_tests)

    # Identity generation, HTTP request/response helpers, and the full 5-stage pair() state machine
    # driven against an in-process simulated GameStream server (no sockets).
    add_executable(ase_pairing_tests
      tests/test_nv_pairing.cpp
      src/net/nv_pairing.cpp
      src/net/pairing_crypto.cpp
      src/net/identity.cpp
      src/net/http_client.cpp
      src/net/gamestream_xml.cpp
    )
    target_include_directories(ase_pairing_tests PRIVATE src)
    target_link_libraries(ase_pairing_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    add_test(NAME ase_pairing_tests COMMAND ase_pairing_tests)

    # Persistent identity + paired-host registry (round-trips a temp store dir on disk).
    add_executable(ase_identity_store_tests
      tests/test_identity_store.cpp
      src/net/identity_store.cpp
      src/net/identity.cpp
      src/ipc/json.cpp
    )
    target_include_directories(ase_identity_store_tests PRIVATE src)
    target_link_libraries(ase_identity_store_tests PRIVATE OpenSSL::Crypto)
    add_test(NAME ase_identity_store_tests COMMAND ase_identity_store_tests)

    # Launch/resume query construction + the launch flow over a simulated transport.
    add_executable(ase_launch_tests
      tests/test_nv_launch.cpp
      src/net/nv_launch.cpp
      src/net/http_client.cpp
      src/net/identity.cpp
      src/net/gamestream_xml.cpp
    )
    target_include_directories(ase_launch_tests PRIVATE src)
    target_link_libraries(ase_launch_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    add_test(NAME ase_launch_tests COMMAND ase_launch_tests)
  endif()
endif()
