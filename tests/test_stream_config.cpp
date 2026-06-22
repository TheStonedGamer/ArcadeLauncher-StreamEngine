// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Unit tests for client.start settings validation (src/client/stream_config.cpp). Pure logic,
// fork-independent — runs in every build configuration. Same tiny CHECK harness as test_ipc.cpp.
#include <cstdio>
#include <string>

#include "client/stream_config.h"
#include "ipc/json.h"

using namespace ase;
using json::Value;

static int g_failures = 0;
#define CHECK(cond, what)                                                       \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (what), __FILE__, __LINE__);  \
      ++g_failures;                                                             \
    }                                                                          \
  } while (0)

// Build a settings object from explicit fields.
static Value settings(int w, int h, int fps, int kbps, const std::string& mode, bool hdr) {
  Value v = Value::object();
  v.set("width", Value::number(w));
  v.set("height", Value::number(h));
  v.set("fps", Value::number(fps));
  v.set("bitrateKbps", Value::number(kbps));
  v.set("displayMode", Value::string(mode));
  v.set("hdr", Value::boolean(hdr));
  return v;
}

static void test_defaults_when_null() {
  client::StreamSettings out;
  std::string code, msg;
  bool ok = client::validate_stream_settings(Value::null(), out, code, msg);
  CHECK(ok, "null settings accepted (all defaults)");
  CHECK(out.width == 1920 && out.height == 1080, "defaults to 1080p");
  CHECK(out.fps == 60 && out.bitrateKbps == 20000, "defaults 60fps / 20Mbps");
  CHECK(out.displayMode == "fullscreen" && !out.hdr, "defaults fullscreen, hdr off");
}

static void test_valid_full() {
  client::StreamSettings out;
  std::string code, msg;
  bool ok = client::validate_stream_settings(
      settings(2560, 1440, 120, 50000, "borderless", true), out, code, msg);
  CHECK(ok, "valid 1440p120 HDR accepted");
  CHECK(out.width == 2560 && out.height == 1440 && out.fps == 120, "fields parsed through");
  CHECK(out.displayMode == "borderless" && out.hdr, "mode+hdr parsed through");
}

static void test_partial_object_uses_defaults() {
  Value v = Value::object();
  v.set("fps", Value::number(30));  // only fps provided
  client::StreamSettings out;
  std::string code, msg;
  bool ok = client::validate_stream_settings(v, out, code, msg);
  CHECK(ok, "partial settings accepted");
  CHECK(out.fps == 30, "provided fps wins");
  CHECK(out.width == 1920, "missing width falls back to default");
}

static void expect_bad(const Value& v, const char* what) {
  client::StreamSettings out;
  std::string code, msg;
  bool ok = client::validate_stream_settings(v, out, code, msg);
  CHECK(!ok, what);
  CHECK(code == "bad_params", "rejection uses bad_params code");
  CHECK(!msg.empty(), "rejection sets a message");
}

static void test_out_of_range_rejected() {
  expect_bad(settings(100, 1080, 60, 20000, "fullscreen", false), "width too small rejected");
  expect_bad(settings(1920, 9000, 60, 20000, "fullscreen", false), "height too big rejected");
  expect_bad(settings(1920, 1080, 5, 20000, "fullscreen", false), "fps too low rejected");
  expect_bad(settings(1920, 1080, 300, 20000, "fullscreen", false), "fps too high rejected");
  expect_bad(settings(1920, 1080, 60, 100, "fullscreen", false), "bitrate too low rejected");
  expect_bad(settings(1920, 1080, 60, 20000, "picture-in-picture", false), "unknown mode rejected");
}

static void test_non_object_rejected() {
  expect_bad(Value::string("1080p"), "string settings rejected");
  expect_bad(Value::number(60), "number settings rejected");
}

int main() {
  test_defaults_when_null();
  test_valid_full();
  test_partial_object_uses_defaults();
  test_out_of_range_rejected();
  test_non_object_rejected();

  if (g_failures == 0) {
    std::printf("all stream-config tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "%d check(s) failed\n", g_failures);
  return 1;
}
