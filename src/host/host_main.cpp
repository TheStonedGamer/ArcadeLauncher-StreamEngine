// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Host mode: serve this PC over GameStream. Wraps the Sunshine fork (vendor/sunshine) and
// exposes its control surface (status/enable/syncApps/pairAccept/deviceInfo) over IPC.
#include <cstdio>
#include "ipc/ipc.h"

namespace ase {

int host_main(int /*argc*/, char** /*argv*/) {
  std::fprintf(stderr,
    "[host] not implemented yet (IPC protocol v%d).\n"
    "       Pending: vendor the Sunshine fork at vendor/sunshine and wire host.* methods.\n",
    ase::ipc::protocol_version());
  return 64;  // EX_USAGE — placeholder until implemented
}

}  // namespace ase
