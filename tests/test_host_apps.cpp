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

#include "host/client_trust.h"
#include "host/host_apps.h"
#include "host/sunshine_backend.h"
#include "host/sunshine_detect.h"
#include "ipc/json.h"

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
  CHECK(args.size() == 6, "apps + log path/level + state/cert/pkey pins");
  if (args.size() == 6) {
    CHECK(args[0] == "file_apps=/data/sunshine_apps.json", "points Sunshine at the apps file");
    // log_path / state / cert / pkey all sit beside the apps file (parent dir), platform-normalized.
    const std::filesystem::path dir("/data");
    CHECK(args[1] == "log_path=" + (dir / "sunshine.log").string(), "Sunshine log beside apps file");
    CHECK(args[2] == "min_log_level=1", "debug-level Sunshine logging");
    CHECK(args[3] == "file_state=" + (dir / "sunshine_state.json").string(),
          "pins the state file (trusted clients)");
    CHECK(args[4] == "cert=" + (dir / "cert.pem").string(), "pins the server cert");
    CHECK(args[5] == "pkey=" + (dir / "pkey.pem").string(), "pins the server key");
  }
  // The path helpers agree with the pinned args (host.deviceInfo/trustClient read the same files).
  CHECK(host_state_path("/data/sunshine_apps.json") ==
            (std::filesystem::path("/data") / "sunshine_state.json").string(),
        "host_state_path matches the pin");
  CHECK(host_cert_path("/data/sunshine_apps.json") ==
            (std::filesystem::path("/data") / "cert.pem").string(),
        "host_cert_path matches the pin");
  CHECK(host_state_path("").empty() && host_cert_path("").empty() && host_pkey_path("").empty(),
        "no apps path -> empty helper paths");
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

// Find root.named_devices[i].cert values in a state JSON (test-only reader).
static std::vector<std::string> trusted_certs(const std::string& stateJson) {
  std::vector<std::string> out;
  auto doc = ase::json::parse(stateJson);
  if (!doc) return out;
  const ase::json::Value* root = doc->find("root");
  if (!root) return out;
  const ase::json::Value* devices = root->find("named_devices");
  if (!devices || !devices->is_array()) return out;
  for (const auto& d : devices->arr) out.push_back(d.get_str("cert"));
  return out;
}

static void test_trust_client_from_empty() {
  // Seeding into no prior state yields a well-formed tree with exactly our client.
  const std::string s = add_trusted_client("", "PC-B", "CERT_B", "uuid-1");
  const auto certs = trusted_certs(s);
  CHECK(certs.size() == 1 && certs[0] == "CERT_B", "fresh state holds the seeded client cert");
  CHECK(has_trusted_client(s, "CERT_B"), "has_trusted_client sees the seeded cert");
  CHECK(!has_trusted_client(s, "CERT_OTHER"), "unknown cert is not trusted");
  CHECK(!has_trusted_client("", "CERT_B"), "empty state trusts nothing");
}

static void test_trust_client_idempotent() {
  // Re-seeding the same cert (every host start does) must not duplicate it.
  std::string s = add_trusted_client("", "PC-B", "CERT_B", "uuid-1");
  s = add_trusted_client(s, "PC-B", "CERT_B", "uuid-2");
  CHECK(trusted_certs(s).size() == 1, "same cert is not added twice");
}

static void test_trust_client_appends_and_preserves() {
  // Adding a second client keeps the first, and pre-existing root fields survive.
  std::string s = add_trusted_client(R"({"root":{"uniqueid":"HOST-UID","named_devices":[]}})",
                                     "PC-B", "CERT_B", "uuid-1");
  s = add_trusted_client(s, "PC-C", "CERT_C", "uuid-2");
  const auto certs = trusted_certs(s);
  CHECK(certs.size() == 2, "second distinct client is appended");
  CHECK(has_trusted_client(s, "CERT_B") && has_trusted_client(s, "CERT_C"), "both clients trusted");
  // root.uniqueid must be preserved (Sunshine keys its own identity off it).
  auto doc = ase::json::parse(s);
  CHECK(doc && doc->find("root") && doc->find("root")->get_str("uniqueid") == "HOST-UID",
        "existing root.uniqueid is preserved");
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
  test_trust_client_from_empty();
  test_trust_client_idempotent();
  test_trust_client_appends_and_preserves();

  if (g_failures == 0) {
    std::printf("all host_apps tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "%d check(s) failed\n", g_failures);
  return 1;
}
