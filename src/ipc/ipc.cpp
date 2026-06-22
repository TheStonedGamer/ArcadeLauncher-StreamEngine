// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "ipc/ipc.h"

namespace ase::ipc {

int protocol_version() {
#ifdef ASE_IPC_PROTOCOL_VERSION
  static_assert(ASE_IPC_PROTOCOL_VERSION == kProtocolVersion,
                "CMake ASE_IPC_PROTOCOL_VERSION must match ipc::kProtocolVersion");
#endif
  return kProtocolVersion;
}

}  // namespace ase::ipc
