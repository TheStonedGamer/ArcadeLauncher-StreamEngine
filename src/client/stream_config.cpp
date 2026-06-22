// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "client/stream_config.h"

namespace ase::client {

using json::Value;

namespace {
bool is_known_mode(const std::string& m) {
  return m == "windowed" || m == "fullscreen" || m == "borderless";
}
}  // namespace

bool validate_stream_settings(const Value& settings, StreamSettings& out,
                              std::string& code, std::string& msg) {
  // Absent/null settings == use all defaults (the launcher may omit it entirely).
  if (settings.is_null()) {
    out = StreamSettings{};
    return true;
  }
  if (!settings.is_object()) {
    code = "bad_params";
    msg = "settings must be an object";
    return false;
  }

  StreamSettings s;
  s.width = static_cast<int>(settings.get_num("width", s.width));
  s.height = static_cast<int>(settings.get_num("height", s.height));
  s.fps = static_cast<int>(settings.get_num("fps", s.fps));
  s.bitrateKbps = static_cast<int>(settings.get_num("bitrateKbps", s.bitrateKbps));
  s.displayMode = settings.get_str("displayMode", s.displayMode);
  s.hdr = settings.get_bool("hdr", s.hdr);

  auto fail = [&](const char* m) {
    code = "bad_params";
    msg = m;
    return false;
  };

  // Ranges: generous GameStream bounds (8K cap, 10–240 fps, 0.5–500 Mbps) — the engine rejects
  // only physically nonsensical requests; moonlight/host negotiate the rest.
  if (s.width < 256 || s.width > 7680) return fail("width out of range [256,7680]");
  if (s.height < 144 || s.height > 4320) return fail("height out of range [144,4320]");
  if (s.fps < 10 || s.fps > 240) return fail("fps out of range [10,240]");
  if (s.bitrateKbps < 500 || s.bitrateKbps > 500000)
    return fail("bitrateKbps out of range [500,500000]");
  if (!is_known_mode(s.displayMode))
    return fail("displayMode must be windowed|fullscreen|borderless");

  out = s;
  return true;
}

}  // namespace ase::client
