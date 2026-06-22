// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// The dispatch server for the launcher<->engine protocol: hello handshake, then a req/res loop
// over method handlers, plus unsolicited events. Transport-agnostic (tested over an in-memory
// transport; runs over a named pipe / domain socket in production).
#pragma once
#include <functional>
#include <map>
#include <mutex>
#include <string>

#include "ipc/json.h"
#include "ipc/transport.h"

namespace ase::ipc {

// A method handler returns its `result` value. To signal failure, set errCode (an IPC error
// code, see docs/IPC.md) and errMsg; the return value is then ignored and an error res is sent.
using Handler =
    std::function<json::Value(const json::Value& params, std::string& errCode, std::string& errMsg)>;

class Server {
 public:
  explicit Server(ITransport& t) : t_(t) {}

  void on(const std::string& method, Handler h) { handlers_[method] = std::move(h); }

  // Exchange hello frames and verify the peer's protocolVersion. false + err on mismatch/EOF.
  bool handshake(std::string& err);

  // Send an unsolicited event (best-effort; ignores write errors). Thread-safe: may be called from
  // a worker thread (e.g. the stream session) concurrently with the run() loop's response writes.
  void emit(const std::string& event, const json::Value& data);

  // Read/dispatch loop until the peer closes. Returns true on clean EOF, false + err on error.
  bool run(std::string& err);

 private:
  // Serializes all frame writes to the transport so emit() (worker thread) and run()'s res writes
  // (main thread) can never interleave a frame. Reads happen only on the run() thread, so the
  // read side needs no lock.
  bool write_locked(const std::string& payload, std::string& err);

  ITransport& t_;
  std::map<std::string, Handler> handlers_;
  std::mutex write_mu_;
};

}  // namespace ase::ipc
