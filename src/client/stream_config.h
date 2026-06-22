// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Engine-side validation/normalization of the client.start "settings" object (docs/IPC.md).
// Pure logic, no moonlight dependency: the engine vets what the launcher asks for BEFORE it is
// handed to moonlight-common-c, so bad requests fail fast with a clear `bad_params` instead of
// surfacing as an opaque connection failure deep in the stream stack.
#pragma once
#include <string>

#include "ipc/json.h"

namespace ase::client {

// Normalized stream request. Fields omitted by the launcher fall back to these defaults; present
// fields are range-checked. Mirrors moonlight's STREAM_CONFIGURATION inputs without including its
// header, so this compiles in the fork-independent (ASE_LINK_MOONLIGHT=OFF) build too.
struct StreamSettings {
  int width = 1920;
  int height = 1080;
  int fps = 60;
  int bitrateKbps = 20000;
  std::string displayMode = "fullscreen";  // windowed | fullscreen | borderless
  bool hdr = false;
};

// Validate a client.start "settings" object. On success fills `out` and returns true. On failure
// returns false with code="bad_params" and a message naming the offending field. A null/absent
// settings object is treated as "all defaults" (valid) — the launcher may omit it.
bool validate_stream_settings(const json::Value& settings, StreamSettings& out,
                              std::string& code, std::string& msg);

}  // namespace ase::client
