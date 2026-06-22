// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Host-mode app catalog — the engine-owned read/write of the Sunshine `apps.json` the host streams.
// Sunshine's app list (Desktop, Steam Big Picture, each published game) lives in a JSON file that its
// `proc::parse`/`proc::refresh` read (top-level `{"env":{…},"apps":[{name,cmd,image-path,…}]}`); the
// launcher publishes its library by writing that file, and the engine points the bundled Sunshine at
// it. This module is the Qt-free, dependency-free model + (de)serialization for that file using the
// engine's own json — so `host.syncApps`/`host.listApps` are real in every build (they manage the
// config the bundled Sunshine consumes), independent of whether the Sunshine binary is present.
#pragma once
#include <string>
#include <vector>

namespace ase::host {

// One streamable app, the subset of Sunshine's `apps.json` entry the launcher publishes. Mirrors the
// keys read by Sunshine's `proc::parse` (`name` required; the rest optional). `cmd` empty = a "stream
// the desktop, launch nothing" entry (Sunshine's Desktop app).
struct HostApp {
  std::string name;        // "name"        (required)
  std::string cmd;         // "cmd"         (launch command; empty = desktop/placebo app)
  std::string imagePath;   // "image-path"  (box art)
  std::string workingDir;  // "working-dir"
  std::string output;      // "output"      (log redirect; empty = inherit)
  bool elevated = false;   // "elevated"
  bool autoDetach = true;  // "auto-detach"
  bool waitAll = true;     // "wait-all"
  int exitTimeout = 5;     // "exit-timeout" (seconds)
};

// Serialize apps to the Sunshine `apps.json` document. Always emits the top-level
// `{"env":{},"apps":[…]}` shape `proc::parse` expects; each app always carries "name" and only emits
// the optional string fields when non-empty.
std::string serialize_apps_json(const std::vector<HostApp>& apps);

// Parse a Sunshine `apps.json` document into HostApp records. Tolerant of missing optional fields and
// of extra keys (e.g. `prep-cmd`/`detached`, which the launcher view doesn't model). Returns empty on
// malformed JSON, a missing `apps` array, or apps without a name.
std::vector<HostApp> parse_apps_json(const std::string& text);

// Default location of the engine-managed apps file (per-user data dir; does not create it):
//   Windows: %APPDATA%\ArcadeLauncher\StreamEngine\sunshine_apps.json
//   else:    $XDG_DATA_HOME/arcade-stream-engine/sunshine_apps.json  (~/.local/share fallback)
// Falls back to "./sunshine_apps.json" if no home/appdata env var is set. Matches the StreamEngine
// data dir used by the client identity store.
std::string default_apps_path();

// Read the apps file (empty vector if absent/unreadable/malformed). Write creates the parent dir
// lazily and returns false on an I/O failure.
std::vector<HostApp> read_apps(const std::string& path);
bool write_apps(const std::string& path, const std::vector<HostApp>& apps);

// Diff result for host.syncApps — how a publish changed the catalog (by app name).
struct SyncDiff {
  int added = 0;
  int removed = 0;
  int updated = 0;
};

// Compute the diff applying `next` over `prev` (match by name; a same-named app with any differing
// field counts as updated). Pure — host.syncApps reports this after writing `next`.
SyncDiff diff_apps(const std::vector<HostApp>& prev, const std::vector<HostApp>& next);

}  // namespace ase::host
