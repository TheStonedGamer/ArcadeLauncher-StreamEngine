// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Host mode: serve this PC over GameStream. Wraps the Sunshine fork (vendor/sunshine) and
// exposes its control surface (status/enable/syncApps/listApps/pairAccept/deviceInfo) over IPC.
//
// The Sunshine fork is not vendored yet, so the host.* handlers below return honest STUB data
// over a fully working IPC server. This lets the launcher develop against a real engine wire
// protocol now; each handler gets its real body when the fork lands (see docs/BUILD.md).
#include <cstdio>

#include "ipc/ipc.h"
#include "ipc/json.h"
#include "ipc/server.h"
#include "ipc/transport.h"

namespace ase {

using json::Value;

static void register_host_methods(ipc::Server& s) {
  // host.status -> capability/state snapshot. Stub: nothing installed yet.
  s.on("host.status", [](const Value&, std::string&, std::string&) {
    Value r = Value::object();
    r.set("installed", Value::boolean(false));
    r.set("running", Value::boolean(false));
    r.set("configured", Value::boolean(false));
    r.set("gpuCapable", Value::boolean(true));
    r.set("appsCount", Value::number(0));
    return r;
  });

  // host.enable {on} -> start/stop hosting. Stub: cannot actually enable without the fork.
  s.on("host.enable", [](const Value& params, std::string& code, std::string& msg) {
    const bool on = params.get_bool("on", false);
    if (on) {
      code = "not_installed";
      msg = "host backend (Sunshine fork) not vendored yet";
      return Value::null();
    }
    Value r = Value::object();
    r.set("running", Value::boolean(false));
    return r;
  });

  // host.syncApps {games:[...]} -> diff library vs registered apps. Stub: no-op diff.
  s.on("host.syncApps", [](const Value&, std::string&, std::string&) {
    Value r = Value::object();
    r.set("added", Value::number(0));
    r.set("removed", Value::number(0));
    r.set("updated", Value::number(0));
    return r;
  });

  // host.listApps -> the host's registered/streamable games (powers the "My PCs" tab, T12k-9).
  s.on("host.listApps", [](const Value&, std::string&, std::string&) {
    Value r = Value::object();
    r.set("apps", Value::array());
    return r;
  });

  // host.pairAccept {pin} -> accept an inbound pairing PIN. Stub: cannot pair without the fork.
  s.on("host.pairAccept", [](const Value&, std::string& code, std::string& msg) {
    code = "not_installed";
    msg = "host pairing requires the Sunshine fork (not vendored yet)";
    return Value::null();
  });

  // host.deviceInfo -> identity for gateway registration (T12k-7/8). Stub: empty fields.
  s.on("host.deviceInfo", [](const Value&, std::string&, std::string&) {
    Value r = Value::object();
    r.set("deviceId", Value::string(""));
    r.set("lanAddr", Value::string(""));
    r.set("meshAddr", Value::string(""));
    r.set("certFingerprint", Value::string(""));
    return r;
  });
}

int host_main(int argc, char** argv) {
  const std::string token = ipc::ipc_token_from_args(argc, argv);
  if (token.empty()) {
    std::fprintf(stderr,
      "[host] requires --ipc <token> (the launcher's control pipe/socket; see docs/IPC.md).\n"
      "       The engine connects to a launcher-created endpoint — it has no standalone mode.\n");
    return 64;  // EX_USAGE
  }

  std::string err;
  auto transport = ipc::connect_local(token, err);
  if (!transport) {
    std::fprintf(stderr, "[host] %s\n", err.c_str());
    return 69;  // EX_UNAVAILABLE
  }

  ipc::Server server(*transport);
  register_host_methods(server);

  if (!server.handshake(err)) {
    std::fprintf(stderr, "[host] handshake failed: %s\n", err.c_str());
    return 70;  // EX_SOFTWARE
  }
  std::fprintf(stderr, "[host] connected (IPC protocol v%d); serving host.* (stub backend).\n",
               ase::ipc::protocol_version());
  if (!server.run(err)) {
    std::fprintf(stderr, "[host] ipc error: %s\n", err.c_str());
    return 70;
  }
  return 0;
}

}  // namespace ase
