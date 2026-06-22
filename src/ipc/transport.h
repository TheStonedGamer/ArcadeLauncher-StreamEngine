// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// The byte transport under the IPC framing. The launcher is the listener; the engine connects.
// One control connection per engine process. Local only — never a TCP socket (see docs/IPC.md).
#pragma once
#include <cstddef>
#include <memory>
#include <string>

namespace ase::ipc {

struct ITransport {
  virtual ~ITransport() = default;
  // Blocking, loops until exactly n bytes are read. false on clean EOF or error.
  virtual bool read_exact(void* buf, size_t n) = 0;
  // Blocking, loops until all n bytes are written. false on error.
  virtual bool write_all(const void* buf, size_t n) = 0;
  virtual void close() = 0;
};

// Connect to the launcher-created pipe/socket named by token:
//   Windows: \\.\pipe\arcade-stream-engine-<token>
//   Unix:    $XDG_RUNTIME_DIR/arcade-stream-engine-<token>.sock  (fallback /tmp)
// Returns nullptr and sets err on failure.
std::unique_ptr<ITransport> connect_local(const std::string& token, std::string& err);

}  // namespace ase::ipc
