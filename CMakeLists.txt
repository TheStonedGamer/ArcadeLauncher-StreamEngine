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

add_executable(arcade-stream-engine
  src/main.cpp
  src/host/host_main.cpp
  src/client/stream_main.cpp
  src/ipc/ipc.cpp
)

target_include_directories(arcade-stream-engine PRIVATE src)
target_compile_definitions(arcade-stream-engine PRIVATE
  ENGINE_VERSION="${ENGINE_VERSION}"
  ASE_IPC_PROTOCOL_VERSION=1
)

# --- Fork integration (wired during build bring-up; see docs/ARCHITECTURE.md) ---------------
# if(ASE_BUILD_HOST AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sunshine/CMakeLists.txt")
#   add_subdirectory(vendor/sunshine EXCLUDE_FROM_ALL)
#   target_link_libraries(arcade-stream-engine PRIVATE sunshine-core)   # name TBD
# endif()
# if(ASE_BUILD_CLIENT AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/moonlight/.../moonlight-common-c")
#   add_subdirectory(vendor/moonlight/.../moonlight-common-c EXCLUDE_FROM_ALL)
#   target_link_libraries(arcade-stream-engine PRIVATE moonlight-common-c)
# endif()

install(TARGETS arcade-stream-engine RUNTIME DESTINATION bin)
