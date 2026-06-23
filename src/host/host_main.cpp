// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Host mode: serve this PC over GameStream by driving the bundled Sunshine fork.
//
// The engine ships the vendored fork's `sunshine[.exe]` beside itself and controls it as a managed
// child process (see host/sunshine_backend.h for why we don't link it in-process). The app catalog
// the host streams is the engine-managed apps.json (host/host_apps.h), which Sunshine reads.
//
// host.syncApps / host.listApps / host.status manage that catalog and report state regardless of
// whether the Sunshine binary is present — so the launcher's "My PCs" + hosting UI is functional now;
// host.enable additionally starts/stops the child, which needs the binary bundled.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "host/host_apps.h"
#include "host/sunshine_backend.h"
#include "ipc/ipc.h"
#include "ipc/json.h"
#include "ipc/server.h"
#include "ipc/transport.h"

namespace ase {

using json::Value;

// Convert a launcher-supplied games array ({name,cmd,image,workingDir,...}) into HostApp records.
static std::vector<host::HostApp> games_from_params(const Value& params) {
  std::vector<host::HostApp> apps;
  const Value* games = params.find("games");
  if (!games || !games->is_array()) return apps;
  for (const Value& g : games->arr) {
    if (!g.is_object()) continue;
    host::HostApp a;
    a.name = g.get_str("name");
    if (a.name.empty()) continue;
    // Storefront URIs (Steam/Epic) aren't runnable programs — wrap them in the host OS's opener so
    // Sunshine can actually launch the title when this PC is streamed (see host_launch_command).
    a.cmd = host::host_launch_command(g.get_str("cmd"));
    a.imagePath = g.get_str("image", g.get_str("imagePath"));
    a.workingDir = g.get_str("workingDir", g.get_str("working-dir"));
    a.output = g.get_str("output");
    a.elevated = g.get_bool("elevated", false);
    a.autoDetach = g.get_bool("autoDetach", true);
    a.waitAll = g.get_bool("waitAll", true);
    a.exitTimeout = static_cast<int>(g.get_num("exitTimeout", 5));
    apps.push_back(std::move(a));
  }
  return apps;
}

static Value app_to_json(const host::HostApp& a) {
  Value o = Value::object();
  o.set("name", Value::string(a.name));
  o.set("cmd", Value::string(a.cmd));
  o.set("imagePath", Value::string(a.imagePath));
  return o;
}

static void register_host_methods(ipc::Server& s, std::shared_ptr<host::SunshineBackend> backend) {
  // host.status -> capability/state snapshot.
  //   installed = a Sunshine is available to host (bundled/system binary OR one already running) →
  //               the launcher needn't download its sidecar.
  //   running   = a Sunshine host is active (our child OR an adopted external instance).
  //   managed   = WE started the active host (so we can stop it). `running && !managed` ⇒ the user's
  //               own Sunshine, which we adopt but never stop — the UI shows it as external.
  s.on("host.status", [backend](const Value&, std::string&, std::string&) {
    const auto apps = host::read_apps(backend->apps_path());
    const bool managed = backend->running();
    const bool active = backend->host_active();
    Value r = Value::object();
    r.set("installed", Value::boolean(backend->bundled() || active));
    r.set("running", Value::boolean(active));
    r.set("managed", Value::boolean(managed));
    r.set("configured", Value::boolean(!apps.empty()));
    r.set("gpuCapable", Value::boolean(true));
    r.set("appsCount", Value::number(static_cast<double>(apps.size())));
    return r;
  });

  // host.enable {on} -> start/stop hosting. start() adopts an already-running Sunshine; stop() only
  // ends our own managed child (an adopted external instance keeps running).
  s.on("host.enable", [backend](const Value& params, std::string& code, std::string& msg) {
    const bool on = params.get_bool("on", false);
    bool ok = on ? backend->start(msg) : backend->stop(msg);
    if (!ok) {
      // "internal" only if we actually had something to start (a binary, or a live instance);
      // otherwise it's genuinely not installed and the launcher should fetch the sidecar.
      code = (backend->bundled() || backend->host_active()) ? "internal" : "not_installed";
      return Value::null();
    }
    Value r = Value::object();
    r.set("running", Value::boolean(backend->host_active()));
    return r;
  });

  // host.syncApps {games:[...]} -> publish the launcher's library into the host apps catalog; report
  // the diff vs what was registered before. Writes the apps.json the bundled Sunshine reads.
  s.on("host.syncApps", [backend](const Value& params, std::string& code, std::string& msg) {
    const auto prev = host::read_apps(backend->apps_path());
    const auto next = games_from_params(params);
    const host::SyncDiff d = host::diff_apps(prev, next);
    if (!host::write_apps(backend->apps_path(), next)) {
      code = "internal";
      msg = "could not write apps catalog at " + backend->apps_path();
      return Value::null();
    }
    Value r = Value::object();
    r.set("added", Value::number(d.added));
    r.set("removed", Value::number(d.removed));
    r.set("updated", Value::number(d.updated));
    r.set("total", Value::number(static_cast<double>(next.size())));
    return r;
  });

  // host.listApps -> the host's registered/streamable games (powers the "My PCs" tab, T12k-9).
  s.on("host.listApps", [backend](const Value&, std::string&, std::string&) {
    const auto apps = host::read_apps(backend->apps_path());
    Value arr = Value::array();
    for (const auto& a : apps) arr.push(app_to_json(a));
    Value r = Value::object();
    r.set("apps", std::move(arr));
    return r;
  });

  // host.pairAccept {pin} -> accept an inbound pairing PIN. Sunshine's PIN pairing is confirmed via
  // its own web UI / pin endpoint; the engine does not own that flow yet.
  s.on("host.pairAccept", [](const Value&, std::string& code, std::string& msg) {
    code = "unsupported";
    msg = "host PIN pairing is handled by Sunshine's own web UI; engine-side accept not wired yet";
    return Value::null();
  });

  // host.deviceInfo -> identity for gateway registration (T12k-7/8). Cert fingerprint comes from the
  // running Sunshine's credentials; not surfaced over the engine yet.
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
  auto backend = std::make_shared<host::SunshineBackend>();
  register_host_methods(server, backend);

  if (!server.handshake(err)) {
    std::fprintf(stderr, "[host] handshake failed: %s\n", err.c_str());
    return 70;  // EX_SOFTWARE
  }
  std::fprintf(stderr,
               "[host] connected (IPC protocol v%d); serving host.* (sunshine %s, apps %s).\n",
               ase::ipc::protocol_version(),
               backend->bundled() ? backend->binary_path().c_str() : "not bundled",
               backend->apps_path().c_str());
  if (!server.run(err)) {
    std::fprintf(stderr, "[host] ipc error: %s\n", err.c_str());
    return 70;
  }
  return 0;
}

}  // namespace ase
