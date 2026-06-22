// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "net/nv_launch.h"

#include <string>

#include "net/gamestream_xml.h"
#include "net/http_client.h"

namespace ase::net {

namespace {

// Launching an app can take a while (NvHTTP LAUNCH_TIMEOUT_MS).
constexpr int kLaunchTimeoutMs = 30000;

// The HDR capability block GFE/Sunshine expect appended when a 10-bit mode is requested. Mirrors
// NvHTTP::startApp's literal — the values are fixed placeholders moonlight itself sends.
constexpr const char* kHdrQuery =
    "&hdrMode=1&clientHdrCapVersion=0&clientHdrCapSupportedFlagsInUint32=0"
    "&clientHdrCapMetaDataId=NV_STATIC_METADATA_TYPE_1&clientHdrCapDisplayData=0x0x0x0x0x0x0x0x0x0x0";

}  // namespace

std::string build_launch_args(const LaunchConfig& cfg) {
  // GFE quirk: an FPS over 60 makes SOPS fall back to 720p60, so send 0 to keep the requested
  // resolution. Sunshine doesn't need the hack.
  const int modeFps = (cfg.fps > 60 && cfg.isGfe) ? 0 : cfg.fps;

  std::string q;
  q += "appid=" + std::to_string(cfg.appId);
  q += "&mode=" + std::to_string(cfg.width) + "x" + std::to_string(cfg.height) + "x" +
       std::to_string(modeFps);
  q += "&additionalStates=1&sops=" + std::string(cfg.sops ? "1" : "0");
  q += "&rikey=" + cfg.riKeyHex;
  q += "&rikeyid=" + std::to_string(cfg.riKeyId);
  if (cfg.hdr) q += kHdrQuery;
  q += "&localAudioPlayMode=" + std::string(cfg.localAudio ? "1" : "0");
  q += "&surroundAudioInfo=" + std::to_string(cfg.surroundAudioInfo);
  q += "&remoteControllersBitmap=" + std::to_string(cfg.gamepadMask);
  q += "&gcmap=" + std::to_string(cfg.gamepadMask);
  q += "&gcpersist=" + std::string(cfg.persistControllers ? "1" : "0");
  q += cfg.extraQuery;  // LiGetLaunchUrlQueryParameters() — already begins with '&' (or empty)
  return q;
}

std::string build_resume_args(const LaunchConfig& cfg) {
  // Resume keeps the running app's mode; the host only needs the fresh input key + audio routing.
  std::string q;
  q += "rikey=" + cfg.riKeyHex;
  q += "&rikeyid=" + std::to_string(cfg.riKeyId);
  q += "&surroundAudioInfo=" + std::to_string(cfg.surroundAudioInfo);
  q += cfg.extraQuery;
  return q;
}

LaunchResult run_launch(const LaunchTransport& transport, const std::string& verb,
                        const LaunchConfig& cfg) {
  const std::string args = (verb == "resume") ? build_resume_args(cfg) : build_launch_args(cfg);
  const std::string resp = transport(verb, args);
  if (resp.empty() || xml_attr(resp, "root", "status_code") != "200") {
    return {false, "", "host rejected " + verb + " (no status-200 response)"};
  }
  const std::string url = xml_text(resp, "sessionUrl0");
  if (url.empty()) {
    return {false, "", verb + " succeeded but returned no sessionUrl0"};
  }
  return {true, url, ""};
}

LaunchResult launch_or_resume(NvHttpClient& client, const LaunchConfig& cfg, int currentGame) {
  // currentGame != 0 means an app is already running on the host — re-attach with resume rather than
  // launching a second app (which the host refuses).
  const std::string verb = (currentGame != 0) ? "resume" : "launch";
  LaunchTransport transport = [&client](const std::string& v, const std::string& a) {
    return client.get_https(v, a, kLaunchTimeoutMs);
  };
  return run_launch(transport, verb, cfg);
}

}  // namespace ase::net
