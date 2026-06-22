// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Client mode: play a remote host over GameStream. Wraps the Moonlight fork (moonlight-common-c
// + an engine-owned SDL2/HW-decode renderer) and exposes its control surface
// (hosts/apps/pair/start/stop) over IPC. Renders into a borderless child window whose native
// handle is returned to the launcher for reparenting.
//
// The Moonlight fork (moonlight-common-c) is now vendored and, when built with
// ASE_LINK_MOONLIGHT=ON, linked into this binary (ASE_HAVE_MOONLIGHT). Pairing and the live
// connection path are still being implemented; until then the client.* handlers validate input
// and return honest errors over a fully working IPC server (see docs/BUILD.md milestone 3).
#include <cstdio>

#include "client/stream_config.h"
#include "ipc/ipc.h"
#include "ipc/json.h"
#include "ipc/server.h"
#include "ipc/transport.h"

#ifdef ASE_HAVE_MOONLIGHT
#include <Limelight.h>
#endif

#ifdef ASE_HAVE_OPENSSL
#include <cstdlib>
#include <string>

#include "net/gamestream_xml.h"
#include "net/http_client.h"
#include "net/identity.h"
#include "net/nv_pairing.h"
#endif

namespace ase {

using json::Value;

#ifdef ASE_HAVE_OPENSSL
namespace {
// GameStream default ports (NvHTTP).
constexpr int kDefaultHttpPort = 47989;
constexpr int kDefaultHttpsPort = 47984;

// First component of a dotted version like "7.1.431.0" (NvHTTP::parseQuad[0]); 0 if absent.
int major_version(const std::string& appversion) {
  return appversion.empty() ? 0 : std::atoi(appversion.c_str());
}
}  // namespace
#endif

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

  // client.pair {host, pin} -> run the GameStream pairing handshake (Qt-free NvHTTP port). Fetches
  // serverinfo over HTTP to learn the HTTPS port + server generation, generates a client identity,
  // then drives the 5-stage pair(). On success returns the pinned server cert (hex) so the launcher
  // can persist it. NOTE: identity + paired-cert persistence (IdentityManager) is the next step —
  // today a fresh identity is minted per call, so the pairing is not yet reused across sessions.
  s.on("client.pair", [](const Value& params, std::string& code, std::string& msg) -> Value {
#ifdef ASE_HAVE_OPENSSL
    const std::string host = params.get_str("host");
    const std::string pin = params.get_str("pin");
    if (host.empty() || pin.empty()) {
      code = "bad_params";
      msg = "client.pair requires non-empty string 'host' and 'pin'";
      return Value::null();
    }

    net::ClientIdentity identity;
    if (!net::generate_identity(identity)) {
      code = "pairing_failed";
      msg = "could not generate client identity";
      return Value::null();
    }

    net::NvHttpClient client(host, kDefaultHttpPort, kDefaultHttpsPort, identity,
                             net::generate_unique_id());

    // serverinfo (HTTP, no pairing needed) -> server generation + HTTPS port.
    const std::string info = client.get_http("serverinfo", "", 5000);
    if (info.empty() || net::xml_attr(info, "root", "status_code") != "200") {
      code = "host_unreachable";
      msg = "no serverinfo response from " + host + " (is GameStream/Sunshine running?)";
      return Value::null();
    }
    const int gen = major_version(net::xml_text(info, "appversion"));
    const int httpsPort = std::atoi(net::xml_text(info, "HttpsPort").c_str());
    if (httpsPort > 0) client.set_https_port(httpsPort);

    net::PairResult res = net::pair_with_client(client, identity, pin, gen);
    switch (res.state) {
      case net::PairState::Paired: {
        Value r = Value::object();
        r.set("paired", Value::boolean(true));
        r.set("serverCert", Value::string(net::to_hex(res.serverCertPem)));
        return r;
      }
      case net::PairState::PinWrong:
        code = "pin_wrong";
        msg = "the PIN entered did not match";
        return Value::null();
      case net::PairState::AlreadyInProgress:
        code = "already_pairing";
        msg = "the host is already pairing with another client";
        return Value::null();
      case net::PairState::Failed:
      default:
        code = "pairing_failed";
        msg = "pairing handshake failed";
        return Value::null();
    }
#else
    (void)params;
    code = "pairing_failed";
    msg = "engine built without OpenSSL (ASE_WITH_OPENSSL=OFF); pairing unavailable";
    return Value::null();
#endif
  });

  // client.start {host, app, settings, embedWindow?} -> begin streaming. The engine first
  // validates/normalizes `settings` (fails fast with bad_params), then would open the moonlight
  // connection. Pairing/connect isn't implemented yet, so a well-formed request still returns
  // not_paired honestly — but a malformed one is now rejected up front.
  s.on("client.start", [](const Value& params, std::string& code, std::string& msg) {
    client::StreamSettings settings;
    const Value* sv = params.find("settings");
    if (!client::validate_stream_settings(sv ? *sv : Value::null(), settings, code, msg)) {
      return Value::null();  // code/msg already set to bad_params + offending field
    }
    code = "not_paired";
    msg = "host not paired (client pairing/connect lands in milestone 3)";
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
#ifdef ASE_HAVE_MOONLIGHT
  // Touch a pure moonlight-common-c symbol so linkage is proven at runtime, not just at link time
  // (LiGetStageName(STAGE_NONE) -> "none"; no side effects, no connection).
  std::fprintf(stderr,
               "[stream] connected (IPC protocol v%d); moonlight-common-c linked (stage0=%s).\n",
               ase::ipc::protocol_version(), LiGetStageName(STAGE_NONE));
#else
  std::fprintf(stderr,
               "[stream] connected (IPC protocol v%d); serving client.* (moonlight not linked).\n",
               ase::ipc::protocol_version());
#endif
  if (!server.run(err)) {
    std::fprintf(stderr, "[stream] ipc error: %s\n", err.c_str());
    return 70;
  }
  return 0;
}

}  // namespace ase
