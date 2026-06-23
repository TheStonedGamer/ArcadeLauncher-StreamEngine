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
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "host/client_trust.h"
#include "host/host_apps.h"
#include "host/sunshine_backend.h"
#include "ipc/ipc.h"
#include "ipc/json.h"
#include "ipc/server.h"
#include "ipc/transport.h"

namespace ase {

using json::Value;

// Whole-file text read ("" if the file is absent — the common case before Sunshine's first launch).
static std::string read_text_file(const std::string& path) {
  if (path.empty()) return "";
  std::ifstream f(path, std::ios::binary);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static bool write_text_file(const std::string& path, const std::string& data) {
  if (path.empty()) return false;
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f << data;
  return f.good();
}

// A stable 32-hex-char id for a Sunshine named_devices entry, derived from the cert so re-trusting
// the same client yields the same uuid. Crypto-free on purpose: host mode must not depend on the
// OpenSSL-gated net layer (this id is an opaque label, not security material).
static std::string stable_uuid(const std::string& seed) {
  const std::size_t a = std::hash<std::string>{}(seed);
  const std::size_t b = std::hash<std::string>{}("ase-trust:" + seed);
  char buf[33];
  std::snprintf(buf, sizeof(buf), "%016zx%016zx", a, b);
  return std::string(buf, 32);
}

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

  // host.trustClient {name, certPem} -> authorize a GameStream client WITHOUT a PIN by seeding its
  // cert into Sunshine's state file (root.named_devices), which is exactly what a successful PIN pair
  // persists. This is the host half of brokered zero-PIN auto-pairing (fix A): the launcher fetches
  // the account's registered client certs and calls this for each BEFORE host.enable, so Sunshine
  // trusts them when it loads its state on start. Idempotent (re-seeding the same cert is a no-op).
  // Takes effect on the next Sunshine start — if one is already running it must be restarted to
  // reload named_devices, reported via `restartRequired`.
  s.on("host.trustClient", [backend](const Value& params, std::string& code, std::string& msg) {
    const std::string certPem = params.get_str("certPem");
    const std::string name = params.get_str("name", "ArcadeLauncher client");
    if (certPem.empty()) {
      code = "bad_request";
      msg = "host.trustClient requires a non-empty certPem";
      return Value::null();
    }
    const std::string statePath = host::host_state_path(backend->apps_path());
    if (statePath.empty()) {
      code = "internal";
      msg = "no Sunshine state path (apps path unset)";
      return Value::null();
    }
    const std::string current = read_text_file(statePath);
    const bool already = host::has_trusted_client(current, certPem);
    if (!already) {
      const std::string next =
          host::add_trusted_client(current, name, certPem, stable_uuid(certPem));
      if (!write_text_file(statePath, next)) {
        code = "internal";
        msg = "could not write Sunshine state at " + statePath;
        return Value::null();
      }
    }
    Value r = Value::object();
    r.set("trusted", Value::boolean(true));
    r.set("alreadyTrusted", Value::boolean(already));
    // Newly-seeded certs only load at Sunshine start; a live host must restart to honor them.
    r.set("restartRequired", Value::boolean(!already && backend->running()));
    return r;
  });

  // Back-compat alias: the old name implied PIN handling, but the engine now authorizes by cert.
  // Accepts the same {name, certPem}. Kept so an older launcher build doesn't 404.
  s.on("host.pairAccept", [backend](const Value& params, std::string& code, std::string& msg) {
    const std::string certPem = params.get_str("certPem");
    if (certPem.empty()) {
      code = "unsupported";
      msg = "host.pairAccept now authorizes by cert — pass {name, certPem} (PIN pairing is unused)";
      return Value::null();
    }
    const std::string statePath = host::host_state_path(backend->apps_path());
    const std::string current = read_text_file(statePath);
    if (!host::has_trusted_client(current, certPem)) {
      const std::string next = host::add_trusted_client(
          current, params.get_str("name", "ArcadeLauncher client"), certPem, stable_uuid(certPem));
      if (!write_text_file(statePath, next)) {
        code = "internal";
        msg = "could not write Sunshine state at " + statePath;
        return Value::null();
      }
    }
    Value r = Value::object();
    r.set("trusted", Value::boolean(true));
    return r;
  });

  // host.deviceInfo -> identity for gateway registration (T12k-7/8) AND, for cert pre-authorization,
  // this host's Sunshine server cert PEM. The client pins this cert (client.trustHost) so it trusts
  // the host without the handshake's server-cert exchange. The cert is created by Sunshine on first
  // launch at the engine-pinned path (host_cert_path); empty until then.
  s.on("host.deviceInfo", [backend](const Value&, std::string&, std::string&) {
    const std::string serverCertPem = read_text_file(host::host_cert_path(backend->apps_path()));
    Value r = Value::object();
    r.set("deviceId", Value::string(""));
    r.set("lanAddr", Value::string(""));
    r.set("meshAddr", Value::string(""));
    r.set("certFingerprint", Value::string(""));
    r.set("serverCertPem", Value::string(serverCertPem));
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
