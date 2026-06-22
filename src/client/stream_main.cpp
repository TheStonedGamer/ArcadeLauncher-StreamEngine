// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Client mode: play a remote host over GameStream. Wraps the Moonlight fork (vendor/moonlight)
// and exposes its control surface (pair/start/stop/hosts/apps) over IPC. Renders into a
// borderless child window whose native handle is returned to the launcher for reparenting.
#include <cstdio>
#include "ipc/ipc.h"

namespace ase {

int stream_main(int /*argc*/, char** /*argv*/) {
  std::fprintf(stderr,
    "[stream] not implemented yet (IPC protocol v%d).\n"
    "         Pending: vendor the Moonlight fork at vendor/moonlight and wire client.* methods.\n",
    ase::ipc::protocol_version());
  return 64;  // EX_USAGE — placeholder until implemented
}

}  // namespace ase
