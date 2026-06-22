// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// The GameStream app-launch handshake — the Qt-free port of moonlight-qt's NvHTTP::startApp()
// (launch / resume). Before video can flow the client must tell the host which app to run and hand
// it the remote-input AES key/id (rikey/rikeyid) that the input stream will be encrypted with; the
// host replies with the RTSP session URL that LiStartConnection() needs. As with nv_pairing, the
// request-building is pure (unit-tested) and the network is injected so the whole flow can be tested
// against a simulated server with no sockets. OpenSSL-only (ASE_WITH_OPENSSL) — it runs over the
// pinned-cert HTTPS channel established at pairing time.
#pragma once
#include <functional>
#include <string>

namespace ase::net {

class NvHttpClient;

// Everything needed to build a launch/resume query. The rikey/rikeyid here MUST be the same values
// placed in STREAM_CONFIGURATION.remoteInputAesKey / remoteInputAesIv, or the host will reject the
// encrypted input stream.
struct LaunchConfig {
  int appId = 0;
  int width = 1920;
  int height = 1080;
  int fps = 60;
  bool sops = true;            // server-side optimal playable settings (host adjusts its resolution)
  bool localAudio = false;     // also play audio on the host
  bool hdr = false;            // request a 10-bit/HDR mode
  int gamepadMask = 0x1;       // remoteControllersBitmap / gcmap
  bool persistControllers = false;
  bool isGfe = false;          // GFE quirk: fps>60 must be sent as 0 (Sunshine doesn't need it)
  std::string riKeyHex;        // 16-byte remote-input AES key, lowercase hex (32 chars)
  int riKeyId = 0;             // remote-input key id (big-endian first 4 bytes of the AES IV)
  int surroundAudioInfo = 0;   // SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(...) for the audio config
  std::string extraQuery;      // LiGetLaunchUrlQueryParameters() (Sunshine extras); "" when unlinked
};

// Build the launch/resume query tail (everything after the uniqueid/uuid the HTTP client prepends).
// Pure — mirrors NvHTTP::startApp's argument string so it can be checked with a KAT.
std::string build_launch_args(const LaunchConfig& cfg);

// Build the resume query tail. Resume re-attaches to an already-running app, so the host only needs
// the new input key (it keeps the running mode); mirrors the subset moonlight sends on resume.
std::string build_resume_args(const LaunchConfig& cfg);

struct LaunchResult {
  bool ok = false;
  std::string rtspSessionUrl;  // <sessionUrl0> — passed to LiStartConnection as SERVER_INFORMATION
  std::string error;           // human-readable reason when !ok
};

// Performs the launch (or resume) request. `verb` is "launch" or "resume"; `transport` issues the
// HTTPS GET and returns the response body ("" on failure). Parses status + sessionUrl0.
using LaunchTransport =
    std::function<std::string(const std::string& verb, const std::string& args)>;
LaunchResult run_launch(const LaunchTransport& transport, const std::string& verb,
                        const LaunchConfig& cfg);

// Convenience: launch (currentGame==0) or resume (already running this app) over a live client.
// `currentGame` is the host's <currentgame> from serverinfo (0 = idle).
LaunchResult launch_or_resume(NvHttpClient& client, const LaunchConfig& cfg, int currentGame);

}  // namespace ase::net
