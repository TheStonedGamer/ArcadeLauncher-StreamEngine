// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Unit tests for the host-mode catalog + Sunshine-driver helpers:
//   - host/host_apps.cpp: serialize/parse the Sunshine apps.json, round-trip, read/write a temp file,
//     and the publish diff (added/removed/updated by name).
//   - host/sunshine_backend.cpp: the PURE helpers resolve_sunshine_binary + build_launch_args (the
//     process spawn itself is platform code, exercised on a host with a bundled sunshine binary).
// Dependency-free (json + std::filesystem) — runs in every build.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "host/host_apps.h"
#include "host/sunshine_backend.h"
#include "host/sunshine_detect.h"

using namespace ase::host;

static int g_failures = 0;
#define CHECK(cond, what)                                                       \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (what), __FILE__, __LINE__);  \
      ++g_failures;                                                             \
    }                                                                          \
  } while (0)

static void test_serialize_parse_roundtrip() {
  std::vector<HostApp> apps;
  HostApp desktop;  // empty cmd = stream-the-desktop app
  desktop.name = "Desktop";
  apps.push_back(desktop);
  HostApp game;
  game.name = "Halo & Friends";  // entity-bearing name must round-trip through JSON
  game.cmd = "C:/Games/halo.exe";
  game.imagePath = "C:/art/halo.png";
  game.workingDir = "C:/Games";
  game.elevated = true;
  game.autoDetach = false;
  game.waitAll = false;
  game.exitTimeout = 12;
  apps.push_back(game);

  const std::string json = serialize_apps_json(apps);
  CHECK(json.find("\"apps\"") != std::string::npos, "doc has apps array");
  CHECK(json.find("\"env\"") != std::string::npos, "doc has env object Sunshine expects");
  CHECK(json.find("\"image-path\"") != std::string::npos, "uses Sunshine's image-path key");

  const std::vector<HostApp> back = parse_apps_json(json);
  CHECK(back.size() == 2, "round-trips both apps");
  if (back.size() == 2) {
    CHECK(back[0].name == "Desktop" && back[0].cmd.empty(), "desktop app: name kept, no cmd");
    CHECK(back[1].name == "Halo & Friends", "name with & round-trips");
    CHECK(back[1].cmd == "C:/Games/halo.exe", "cmd round-trips");
    CHECK(back[1].imagePath == "C:/art/halo.png", "image-path round-trips");
    CHECK(back[1].elevated && !back[1].autoDetach && !back[1].waitAll, "bools round-trip");
    CHECK(back[1].exitTimeout == 12, "exit-timeout round-trips");
  }
}

static void test_parse_tolerant() {
  // Missing optionals default; a nameless app is dropped; extra keys (prep-cmd) are ignored.
  const std::string xml =
      "{\"apps\":["
      "{\"name\":\"OnlyName\"},"
      "{\"cmd\":\"x\"},"
      "{\"name\":\"WithPrep\",\"cmd\":\"y\",\"prep-cmd\":[{\"do\":\"a\",\"undo\":\"b\"}]}"
      "]}";
  const std::vector<HostApp> apps = parse_apps_json(xml);
  CHECK(apps.size() == 2, "drops the nameless entry, keeps the two named");
  if (apps.size() == 2) {
    CHECK(apps[0].name == "OnlyName" && apps[0].exitTimeout == 5, "defaults applied");
    CHECK(apps[0].autoDetach && apps[0].waitAll, "bool defaults true");
    CHECK(apps[1].name == "WithPrep" && apps[1].cmd == "y", "extra keys ignored");
  }

  CHECK(parse_apps_json("not json").empty(), "malformed -> empty");
  CHECK(parse_apps_json("{\"nope\":1}").empty(), "missing apps array -> empty");
}

static void test_read_write_file() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "ase_host_apps_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  const std::string path = (dir / "nested" / "sunshine_apps.json").string();

  std::vector<HostApp> apps;
  HostApp a;
  a.name = "Steam";
  a.cmd = "steam";
  apps.push_back(a);

  CHECK(write_apps(path, apps), "write creates parent dirs + file");
  const std::vector<HostApp> back = read_apps(path);
  CHECK(back.size() == 1 && back[0].name == "Steam", "read-back matches written");
  CHECK(read_apps((dir / "missing.json").string()).empty(), "absent file -> empty");

  fs::remove_all(dir, ec);
}

static void test_diff() {
  std::vector<HostApp> prev;
  HostApp keep;  keep.name = "Keep";  keep.cmd = "k";
  HostApp drop;  drop.name = "Drop";
  HostApp edit;  edit.name = "Edit";  edit.cmd = "old";
  prev = {keep, drop, edit};

  std::vector<HostApp> next;
  HostApp keep2 = keep;                    // unchanged
  HostApp edit2; edit2.name = "Edit"; edit2.cmd = "new";  // changed cmd
  HostApp add;   add.name = "Add";
  next = {keep2, edit2, add};

  const SyncDiff d = diff_apps(prev, next);
  CHECK(d.added == 1, "one added (Add)");
  CHECK(d.removed == 1, "one removed (Drop)");
  CHECK(d.updated == 1, "one updated (Edit cmd changed)");

  const SyncDiff same = diff_apps(prev, prev);
  CHECK(same.added == 0 && same.removed == 0 && same.updated == 0, "identical -> no changes");
}

static bool always(const std::string&) { return true; }
static bool never(const std::string&) { return false; }
// Only paths ending in the platform sunshine name "exist".
static bool only_bundled(const std::string& p) {
  const std::string n =
#ifdef _WIN32
      "sunshine.exe";
#else
      "sunshine";
#endif
  return p.size() >= n.size() && p.compare(p.size() - n.size(), n.size(), n) == 0;
}

static void test_resolve_binary() {
  // Env override wins when it exists.
  CHECK(resolve_sunshine_binary("/engine", "/custom/sun", always) == "/custom/sun",
        "existing env override is used");
  // Env override that doesn't exist falls through to the engine dir.
  const std::string fromDir = resolve_sunshine_binary("/engine", "/missing", only_bundled);
  CHECK(fromDir.find("/engine") == 0 && fromDir.find("sunshine") != std::string::npos,
        "falls back to <engineDir>/sunshine");
  // Nothing exists -> empty (bundled() will be false).
  CHECK(resolve_sunshine_binary("/engine", "", never).empty(), "no binary found -> empty");
  CHECK(resolve_sunshine_binary("", "", always).empty(), "no engine dir + no override -> empty");
}

static void test_launch_args() {
  const auto args = build_launch_args("/data/sunshine_apps.json");
  CHECK(args.size() == 1, "one config override arg");
  if (!args.empty()) CHECK(args[0] == "file_apps=/data/sunshine_apps.json", "points Sunshine at the apps file");
  CHECK(build_launch_args("").empty(), "no apps path -> no args");
}

// Only the system Sunshine path our resolver tries on *this* platform "exists".
static bool only_system(const std::string& p) {
  const std::string n =
#ifdef _WIN32
      "\\Sunshine\\sunshine.exe";
#else
      "/sunshine";
#endif
  return p.size() >= n.size() && p.compare(p.size() - n.size(), n.size(), n) == 0;
}

static void test_system_sunshine() {
  // The candidate list is non-empty on every platform (env-derived dirs + well-known prefixes),
  // and every entry points at a Sunshine binary.
  const auto candidates = system_sunshine_candidates();
  CHECK(!candidates.empty(), "system candidates are enumerated");
  for (const auto& c : candidates) {
    CHECK(c.find("sunshine") != std::string::npos, "candidate names the sunshine binary");
  }
  // Resolution picks the first existing candidate, and reports none when nothing is on disk.
  CHECK(!resolve_system_sunshine(only_system).empty(), "finds a system Sunshine when one exists");
  CHECK(resolve_system_sunshine(never).empty(), "no system Sunshine -> empty");
}

static void test_host_launch_command() {
  // A real exe path (no scheme) and the desktop placeholder pass through untouched.
  CHECK(host_launch_command("C:/Games/halo.exe") == "C:/Games/halo.exe", "exe path unchanged");
  CHECK(host_launch_command("").empty(), "empty (desktop app) stays empty");

  // Storefront URIs get wrapped in the host OS opener so Sunshine can launch them.
  const std::string steam = host_launch_command("steam://rungameid/220");
  const std::string epic = host_launch_command("com.epicgames.launcher://apps/x?action=launch");
  CHECK(steam.find("steam://rungameid/220") != std::string::npos, "steam uri preserved in wrap");
  CHECK(epic.find("com.epicgames.launcher://") != std::string::npos, "epic uri preserved in wrap");
  CHECK(steam != "steam://rungameid/220", "steam uri is actually wrapped, not passed through");
#ifdef _WIN32
  CHECK(steam.rfind("cmd /C start \"\" \"", 0) == 0, "windows wraps with start + empty title");
#else
  CHECK(steam.rfind("xdg-open \"", 0) == 0, "posix wraps with xdg-open");
#endif
  // Wrapping is deterministic, so a re-sync of the same URI produces the same cmd (no spurious
  // "updated" in the host.syncApps diff).
  CHECK(host_launch_command("steam://rungameid/220") == steam, "same uri -> same wrap (diff-stable)");
}

int main() {
  test_serialize_parse_roundtrip();
  test_parse_tolerant();
  test_read_write_file();
  test_diff();
  test_resolve_binary();
  test_launch_args();
  test_host_launch_command();
  test_system_sunshine();

  if (g_failures == 0) {
    std::printf("all host_apps tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "%d check(s) failed\n", g_failures);
  return 1;
}
