// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Launcher <-> engine IPC. The ONLY contract between the (proprietary) launcher and this
// (GPL) engine. Wire protocol is documented in docs/IPC.md. Keep this an honest, documented
// protocol two independent programs could speak — never a fig-leaf over in-process coupling.
#pragma once
#include <string>

namespace ase::ipc {

// Bump on ANY breaking change to the launcher<->engine protocol. Mirrors the
// client<->server version-lockstep discipline. Also passed by CMake as
// ASE_IPC_PROTOCOL_VERSION for cross-checking.
constexpr int kProtocolVersion = 1;

// Message kinds (the "kind" field of every frame).
namespace kind {
constexpr const char* kHello = "hello";  // handshake, both directions
constexpr const char* kReq   = "req";    // launcher -> engine (has "id")
constexpr const char* kRes   = "res";    // engine -> launcher (echoes "id")
constexpr const char* kEvent = "event";  // engine -> launcher (unsolicited)
}  // namespace kind

// Returns kProtocolVersion (and asserts it matches the CMake-provided macro).
int protocol_version();

}  // namespace ase::ipc
