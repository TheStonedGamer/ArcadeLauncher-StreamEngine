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
#include <optional>
#include <string>
#include <vector>

#include "net/gamestream_xml.h"
#include "net/http_client.h"
#include "net/identity.h"
#include "net/identity_store.h"
#include "net/nv_applist.h"
#include "net/nv_launch.h"
#include "net/nv_pairing.h"
#include "net/pairing_crypto.h"
#endif

#ifdef ASE_HAVE_MOONLIGHT
#include <memory>

#include "client/stream_session.h"
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

// Process-wide credential + paired-host store (lazy; default per-user data dir). Shared by every
// client.* handler so the identity minted at pair time is the one reused at stream time.
net::IdentityStore& store() {
  static net::IdentityStore s;
  return s;
}

// surroundAudioInfo for a stereo stream — SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(STEREO),
// inlined so this compiles without Limelight.h in the no-moonlight build (channelMask 0x3 << 16 |
// channelCount 2).
constexpr int kStereoSurroundInfo = (0x3 << 16) | 2;

// Assemble a positive 32-bit remote-input key id from 4 random bytes (big-endian). Both the launch
// query (decimal) and STREAM_CONFIGURATION's IV (big-endian) use this same value, so they match.
int make_ri_key_id(const std::string& four) {
  if (four.size() < 4) return 0;
  unsigned id = (static_cast<unsigned char>(four[0]) << 24) |
                (static_cast<unsigned char>(four[1]) << 16) |
                (static_cast<unsigned char>(four[2]) << 8) |
                (static_cast<unsigned char>(four[3]));
  return static_cast<int>(id & 0x7fffffffu);
}

// Build a pinned, HTTPS-ready client for an already-paired host: load the persistent client
// identity, pin the host's stored server cert, and learn its HTTPS port from an HTTP serverinfo
// probe. Used by every client.* handler that needs the authenticated channel (apps/start). On any
// failure sets code/msg (not_paired / internal / host_unreachable) and returns nullopt.
std::optional<net::NvHttpClient> connect_paired_host(const std::string& host, std::string& code,
                                                     std::string& msg) {
  auto paired = store().find_host(host);
  if (!paired) {
    code = "not_paired";
    msg = "host '" + host + "' is not paired; run client.pair first";
    return std::nullopt;
  }
  net::ClientIdentity identity;
  std::string uniqueId;
  if (!store().load_or_create_identity(identity, uniqueId)) {
    code = "internal";
    msg = "could not load the persistent client identity";
    return std::nullopt;
  }
  net::NvHttpClient client(host, kDefaultHttpPort, kDefaultHttpsPort, identity, uniqueId);
  client.set_pinned_server_cert(paired->serverCertPem);

  // serverinfo over HTTP first — it's unauthenticated and tells us the HTTPS port to switch to.
  const std::string infoHttp = client.get_http("serverinfo", "", 5000);
  if (infoHttp.empty() || net::xml_attr(infoHttp, "root", "status_code") != "200") {
    code = "host_unreachable";
    msg = "no serverinfo from " + host + " (is the host awake and GameStream/Sunshine running?)";
    return std::nullopt;
  }
  const int httpsPort = std::atoi(net::xml_text(infoHttp, "HttpsPort").c_str());
  if (httpsPort > 0) client.set_https_port(httpsPort);
  return client;
}
}  // namespace
#endif

#ifdef ASE_HAVE_MOONLIGHT
namespace {
// The process-singleton live-stream session (moonlight-common-c is single-connection). Created on
// the first client.start with a state sink that forwards phase changes to the launcher as
// stream.state events over the shared IPC server.
std::unique_ptr<client::StreamSession>& stream_session(ipc::Server& s) {
  static std::unique_ptr<client::StreamSession> session;
  if (!session) {
    ipc::Server* sp = &s;
    auto onState = [sp](const client::StateEvent& e) {
      Value d = Value::object();
      d.set("phase", Value::string(e.phase));
      d.set("reason", Value::string(e.reason));
      if (!e.nativeWindow.empty()) d.set("nativeWindow", Value::string(e.nativeWindow));
      sp->emit("stream.state", d);
    };
    auto onStats = [sp](const client::StreamStats& st) {
      Value d = Value::object();
      d.set("framesDecoded", Value::number(static_cast<double>(st.framesDecoded)));
      d.set("framesPresented", Value::number(static_cast<double>(st.framesPresented)));
      d.set("decodeErrors", Value::number(static_cast<double>(st.decodeErrors)));
      d.set("hardware", Value::boolean(st.hardware));
      d.set("width", Value::number(st.width));
      d.set("height", Value::number(st.height));
      d.set("rttMs", Value::number(st.rttMs));
      d.set("rttVarMs", Value::number(st.rttVarMs));
      sp->emit("stream.stats", d);
    };
    session = std::make_unique<client::StreamSession>(std::move(onState), std::move(onStats));
  }
  return session;
}
}  // namespace
#endif

static void register_client_methods(ipc::Server& s) {
  // client.hosts -> the paired hosts persisted in the engine store. (Skeleton/no-OpenSSL build has
  // no store, so it returns an empty list.)
  s.on("client.hosts", [](const Value&, std::string&, std::string&) {
    Value r = Value::object();
    Value hosts = Value::array();
#ifdef ASE_HAVE_OPENSSL
    for (const auto& h : store().list_hosts()) {
      Value o = Value::object();
      o.set("name", Value::string(h.name));
      o.set("address", Value::string(h.address));
      o.set("paired", Value::boolean(true));
      o.set("state", Value::string("idle"));
      hosts.push(std::move(o));
    }
#endif
    r.set("hosts", std::move(hosts));
    return r;
  });

  // client.apps {host} -> streamable apps on a paired host (GameStream `applist`, over the pinned
  // HTTPS channel). Each entry is {id, name, hdrSupported}; the launcher uses it for a chooser and to
  // map a chosen name back to the appid client.start expects. (Skeleton/no-OpenSSL build can't reach
  // a host, so it returns an empty list.)
  s.on("client.apps", [](const Value& params, std::string& code, std::string& msg) -> Value {
#ifdef ASE_HAVE_OPENSSL
    const std::string host = params.get_str("host");
    if (host.empty()) {
      code = "bad_params";
      msg = "client.apps requires a non-empty string 'host'";
      return Value::null();
    }
    auto client = connect_paired_host(host, code, msg);
    if (!client) return Value::null();  // code/msg set by the helper

    std::string err;
    const std::vector<net::AppInfo> apps = net::fetch_app_list(*client, err);
    if (!err.empty()) {
      code = "host_unreachable";
      msg = "could not fetch the app list from " + host + " (" + err + ")";
      return Value::null();
    }
    Value r = Value::object();
    Value arr = Value::array();
    for (const auto& a : apps) {
      Value o = Value::object();
      o.set("id", Value::number(a.id));
      o.set("name", Value::string(a.title));
      o.set("hdrSupported", Value::boolean(a.hdrSupported));
      arr.push(std::move(o));
    }
    r.set("apps", std::move(arr));
    return r;
#else
    (void)params;
    Value r = Value::object();
    r.set("apps", Value::array());
    return r;
#endif
  });

  // client.pair {host, pin} -> run the GameStream pairing handshake (Qt-free NvHTTP port). Fetches
  // serverinfo over HTTP to learn the HTTPS port + server generation, generates a client identity,
  // then drives the 5-stage pair(). On success persists the host's pinned cert in the engine store
  // and returns it (hex) to the launcher. The client identity is loaded once from the store and
  // reused on every call/session, so a host stays paired across engine restarts.
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
    std::string uniqueId;
    if (!store().load_or_create_identity(identity, uniqueId)) {
      code = "pairing_failed";
      msg = "could not load or generate the persistent client identity";
      return Value::null();
    }

    net::NvHttpClient client(host, kDefaultHttpPort, kDefaultHttpsPort, identity, uniqueId);

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
        // Persist the pairing so client.start can re-pin this host without re-pairing. Best-effort:
        // a write failure must not fail an otherwise-successful pair (the launcher keeps its own
        // registry too), so we still report success.
        store().upsert_host({host, params.get_str("name"), res.serverCertPem});
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

  // client.start {host, app, settings, embedWindow?} -> begin streaming. Validates/normalizes
  // `settings` (fails fast with bad_params), connects to the paired host (pins its cert, learns the
  // HTTPS port), resolves `app` (numeric appid or name via the app list), negotiates the remote-input
  // key, runs the launch/resume handshake, then hands off to the worker-threaded StreamSession which
  // drives LiStartConnection and reports progress as stream.state / stream.stats events. Returns
  // {started:true} once the session is launched; connect/decode progress arrives as events.
  // (Full build only; skeleton/no-OpenSSL or no-moonlight builds return an honest error.)
  s.on("client.start", [&s](const Value& params, std::string& code, std::string& msg) -> Value {
    client::StreamSettings settings;
    const Value* sv = params.find("settings");
    if (!client::validate_stream_settings(sv ? *sv : Value::null(), settings, code, msg)) {
      return Value::null();  // code/msg already set to bad_params + offending field
    }
    (void)s;
#ifdef ASE_HAVE_OPENSSL
    const std::string host = params.get_str("host");
    if (host.empty()) {
      code = "bad_params";
      msg = "client.start requires a non-empty string 'host'";
      return Value::null();
    }
    // `app` may be a numeric GameStream appid or a human app name; the name is resolved against the
    // host's app list below (once the HTTPS channel is up). Require it non-empty up front.
    const std::string appRef = params.get_str("app");
    if (appRef.empty()) {
      code = "bad_params";
      msg = "client.start requires 'app' (a GameStream appid or app name)";
      return Value::null();
    }

    // Load the persistent identity, pin the host cert, and learn the HTTPS port (HTTP serverinfo).
    auto client = connect_paired_host(host, code, msg);
    if (!client) return Value::null();  // not_paired / internal / host_unreachable set by the helper

    // serverinfo over the pinned-cert HTTPS channel for the authenticated fields (currentgame,
    // GfeVersion, ServerCodecModeSupport).
    const std::string info = client->get_https("serverinfo", "", 5000);
    if (info.empty() || net::xml_attr(info, "root", "status_code") != "200") {
      code = "host_unreachable";
      msg = "serverinfo over HTTPS failed — the pairing may be stale; re-pair the host";
      return Value::null();
    }

    // Resolve `app` to a numeric appid: a positive numeric value passes through (no extra round-trip);
    // anything else is matched case-insensitively against the host's app list.
    int appId = std::atoi(appRef.c_str());
    if (appId <= 0) {
      std::string err;
      const std::vector<net::AppInfo> apps = net::fetch_app_list(*client, err);
      if (!err.empty()) {
        code = "host_unreachable";
        msg = "could not fetch the app list to resolve '" + appRef + "' (" + err + ")";
        return Value::null();
      }
      appId = net::resolve_app_id(apps, appRef);
      if (appId <= 0) {
        code = "app_not_found";
        msg = "no app named '" + appRef + "' on host '" + host + "'";
        return Value::null();
      }
    }

    const std::string appVersion = net::xml_text(info, "appversion");
    const std::string gfeVersion = net::xml_text(info, "GfeVersion");
    const int codecSupport = std::atoi(net::xml_text(info, "ServerCodecModeSupport").c_str());
    const int currentGame = std::atoi(net::xml_text(info, "currentgame").c_str());

    // Negotiate the remote-input AES key (rikey) + id; these must be identical in the launch query
    // and the stream configuration.
    const std::string riKey = net::random_bytes(16);
    const int riKeyId = make_ri_key_id(net::random_bytes(4));
    if (riKey.size() != 16) {
      code = "internal";
      msg = "could not generate the remote-input key";
      return Value::null();
    }

    net::LaunchConfig lc;
    lc.appId = appId;
    lc.width = settings.width;
    lc.height = settings.height;
    lc.fps = settings.fps;
    lc.sops = true;
    lc.hdr = settings.hdr;
    lc.isGfe = !gfeVersion.empty();
    lc.gamepadMask = 0x1;
    lc.riKeyHex = net::to_hex(riKey);
    lc.riKeyId = riKeyId;
    lc.surroundAudioInfo = kStereoSurroundInfo;
#ifdef ASE_HAVE_MOONLIGHT
    lc.extraQuery = LiGetLaunchUrlQueryParameters();
#endif
    const net::LaunchResult launched = net::launch_or_resume(*client, lc, currentGame);
    if (!launched.ok) {
      code = "internal";
      msg = launched.error;
      return Value::null();
    }

#ifdef ASE_HAVE_MOONLIGHT
    client::StartInfo si;
    si.host = host;
    si.serverAppVersion = appVersion;
    si.gfeVersion = gfeVersion;
    si.rtspSessionUrl = launched.rtspSessionUrl;
    si.serverCodecModeSupport = codecSupport;
    si.settings = settings;
    si.riKey = riKey;
    si.riKeyId = riKeyId;
    si.embedWindow = params.get_bool("embedWindow", false);

    std::string err;
    if (!stream_session(s)->start(si, err)) {
      code = "engine_busy";
      msg = err;
      return Value::null();
    }
    // Connection proceeds on the session's worker thread; progress (incl. the reparent handle via a
    // "window" stream.state event) and decode stats arrive as events.
    Value r = Value::object();
    r.set("started", Value::boolean(true));
    return r;
#else
    (void)appVersion;
    (void)codecSupport;
    code = "internal";
    msg = "engine built without the moonlight client (ASE_LINK_MOONLIGHT=OFF); streaming unavailable";
    return Value::null();
#endif
#else
    (void)settings;
    code = "not_paired";
    msg = "engine built without OpenSSL; client pairing/streaming unavailable";
    return Value::null();
#endif
  });

  // client.stop -> end the current stream (idempotent; no-op if nothing is running).
  s.on("client.stop", [&s](const Value&, std::string&, std::string&) {
    (void)s;
#ifdef ASE_HAVE_MOONLIGHT
    stream_session(s)->stop();
#endif
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
