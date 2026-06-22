// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// CLI entry point. Dispatches to host (Sunshine fork) or client (Moonlight fork) mode.
#include <cstdio>
#include <cstring>

#ifndef ENGINE_VERSION
#define ENGINE_VERSION "0.0.0-dev"
#endif

namespace ase {
int host_main(int argc, char** argv);    // src/host/host_main.cpp
int stream_main(int argc, char** argv);  // src/client/stream_main.cpp
}

static void print_usage(const char* prog) {
  std::fprintf(stderr,
    "arcade-stream-engine %s — ArcadeLauncher combined GameStream engine (GPL-3.0)\n\n"
    "Usage:\n"
    "  %s host    [--ipc <name>]   Host mode  (Sunshine fork): serve this PC\n"
    "  %s stream  [--ipc <name>]   Client mode (Moonlight fork): play a remote host\n"
    "  %s --version\n"
    "  %s --help\n\n"
    "--ipc <name> connects to the launcher's control pipe/socket (see docs/IPC.md).\n",
    ENGINE_VERSION, prog, prog, prog, prog);
}

int main(int argc, char** argv) {
  const char* prog = (argc > 0) ? argv[0] : "arcade-stream-engine";
  if (argc < 2) { print_usage(prog); return 2; }

  const char* cmd = argv[1];
  if (!std::strcmp(cmd, "--version") || !std::strcmp(cmd, "version")) {
    std::printf("%s\n", ENGINE_VERSION);
    return 0;
  }
  if (!std::strcmp(cmd, "--help") || !std::strcmp(cmd, "-h") || !std::strcmp(cmd, "help")) {
    print_usage(prog);
    return 0;
  }
  if (!std::strcmp(cmd, "host"))   return ase::host_main(argc - 1, argv + 1);
  if (!std::strcmp(cmd, "stream")) return ase::stream_main(argc - 1, argv + 1);

  std::fprintf(stderr, "unknown command: %s\n\n", cmd);
  print_usage(prog);
  return 2;
}
