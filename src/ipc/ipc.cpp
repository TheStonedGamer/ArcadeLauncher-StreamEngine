// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "ipc/ipc.h"

#include <cstring>

namespace ase::ipc {

int protocol_version() {
#ifdef ASE_IPC_PROTOCOL_VERSION
  static_assert(ASE_IPC_PROTOCOL_VERSION == kProtocolVersion,
                "CMake ASE_IPC_PROTOCOL_VERSION must match ipc::kProtocolVersion");
#endif
  return kProtocolVersion;
}

std::string ipc_token_from_args(int argc, char** argv) {
  for (int i = 0; i < argc; ++i) {
    if (std::strcmp(argv[i], "--ipc") == 0 && i + 1 < argc) return argv[i + 1];
  }
  return std::string();
}

}  // namespace ase::ipc
