// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Client mode: play a remote host over GameStream. Wraps the Moonlight fork (moonlight-common-c
// + an engine-owned SDL2/HW-decode renderer) and exposes its control surface
// (hosts/apps/pair/start/stop) over IPC. Renders into a borderless child window whose native
// handle is returned to the launcher for reparenting.
//
// The Moonlight fork is not vendored yet, so the client.* handlers below return honest STUB
// data over a fully working IPC server (see docs/BUILD.md).
#include <cstdio>

#include "ipc/ipc.h"
#include "ipc/json.h"
#include "ipc/server.h"
#include "ipc/transport.h"

namespace ase {

using json::Value;

static void register_client_methods(ipc::Server& s) {
  // client.hosts -> known/paired hosts. Stub: none.
  s.on("client.hosts", [](const Value&, std::string&, std::string&) {
    Value r = Value::object();
    r.set("hosts", Value::array());
    return r;
  });

  // client.apps {host} -> streamable apps on a host. Stub: none.
  s.on("client.apps", [](const Value&, std::string&, std::string&) {
    Value r = Value::object();
    r.set("apps", Value::array());
    return r;
  });

  // client.pair {host, pin?} -> pair with a host. Stub: cannot pair without the fork.
  s.on("client.pair", [](const Value&, std::string& code, std::string& msg) {
    code = "pairing_failed";
    msg = "client backend (Moonlight fork) not vendored yet";
    return Value::null();
  });

  // client.start {host, app, settings, embedWindow?} -> begin streaming. Stub: not paired.
  s.on("client.start", [](const Value&, std::string& code, std::string& msg) {
    code = "not_paired";
    msg = "streaming requires the Moonlight fork (not vendored yet)";
    return Value::null();
  });

  // client.stop -> end the current stream. Stub: nothing running.
  s.on("client.stop", [](const Value&, std::string&, std::string&) {
    Value r = Value::object();
    r.set("stopped", Value::boolean(true));
    return r;
  });
}

int stream_main(int argc, char** argv) {
  const std::string token = ipc::ipc_token_from_args(argc, argv);
  if (token.empty()) {
    std::fprintf(stderr,
      "[stream] requires --ipc <token> (the launcher's control pipe/socket; see docs/IPC.md).\n"
      "         The engine connects to a launcher-created endpoint — it has no standalone mode.\n");
    return 64;  // EX_USAGE
  }

  std::string err;
  auto transport = ipc::connect_local(token, err);
  if (!transport) {
    std::fprintf(stderr, "[stream] %s\n", err.c_str());
    return 69;  // EX_UNAVAILABLE
  }

  ipc::Server server(*transport);
  register_client_methods(server);

  if (!server.handshake(err)) {
    std::fprintf(stderr, "[stream] handshake failed: %s\n", err.c_str());
    return 70;  // EX_SOFTWARE
  }
  std::fprintf(stderr, "[stream] connected (IPC protocol v%d); serving client.* (stub backend).\n",
               ase::ipc::protocol_version());
  if (!server.run(err)) {
    std::fprintf(stderr, "[stream] ipc error: %s\n", err.c_str());
    return 70;
  }
  return 0;
}

}  // namespace ase
