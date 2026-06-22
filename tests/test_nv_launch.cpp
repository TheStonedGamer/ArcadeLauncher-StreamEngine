// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Unit tests for the launch/resume handshake port (src/net/nv_launch.cpp): the pure query builders
// and the run_launch() flow against an injected transport (no sockets). OpenSSL build only.
#include <cstdio>
#include <string>

#include "net/nv_launch.h"

using namespace ase;
using namespace ase::net;

static int g_failures = 0;
#define CHECK(cond, what)                                                       \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (what), __FILE__, __LINE__);  \
      ++g_failures;                                                             \
    }                                                                          \
  } while (0)

static bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

static LaunchConfig sample_cfg() {
  LaunchConfig c;
  c.appId = 881448767;
  c.width = 1920;
  c.height = 1080;
  c.fps = 60;
  c.sops = true;
  c.localAudio = false;
  c.hdr = false;
  c.gamepadMask = 0x1;
  c.riKeyHex = "00112233445566778899aabbccddeeff";
  c.riKeyId = 123456;
  c.surroundAudioInfo = (0x3 << 16) | 2;
  c.extraQuery = "&corever=7";
  return c;
}

static void test_launch_args_shape() {
  const std::string q = build_launch_args(sample_cfg());
  CHECK(contains(q, "appid=881448767"), "carries appid");
  CHECK(contains(q, "mode=1920x1080x60"), "mode is WxHxFPS");
  CHECK(contains(q, "sops=1"), "sops on");
  CHECK(contains(q, "rikey=00112233445566778899aabbccddeeff"), "carries rikey hex");
  CHECK(contains(q, "rikeyid=123456"), "carries rikeyid");
  CHECK(contains(q, "surroundAudioInfo=196610"), "carries surround info (0x30002)");
  CHECK(contains(q, "&corever=7"), "appends the moonlight extra query");
  CHECK(!contains(q, "hdrMode=1"), "no HDR block when hdr=false");
}

static void test_gfe_high_fps_quirk() {
  LaunchConfig c = sample_cfg();
  c.fps = 120;
  c.isGfe = true;
  CHECK(contains(build_launch_args(c), "mode=1920x1080x0"), "GFE >60fps forced to 0 in mode");
  c.isGfe = false;
  CHECK(contains(build_launch_args(c), "mode=1920x1080x120"), "Sunshine keeps the real fps");
}

static void test_hdr_block() {
  LaunchConfig c = sample_cfg();
  c.hdr = true;
  CHECK(contains(build_launch_args(c), "hdrMode=1"), "HDR block present when hdr=true");
}

static void test_resume_args_minimal() {
  const std::string q = build_resume_args(sample_cfg());
  CHECK(contains(q, "rikey=00112233445566778899aabbccddeeff"), "resume carries rikey");
  CHECK(contains(q, "rikeyid=123456"), "resume carries rikeyid");
  CHECK(!contains(q, "appid="), "resume omits appid (re-attaches to the running app)");
  CHECK(!contains(q, "mode="), "resume omits mode");
}

static void test_run_launch_success() {
  LaunchTransport ok = [](const std::string& verb, const std::string& args) -> std::string {
    (void)verb;
    (void)args;
    return "<root status_code=\"200\"><sessionUrl0>rtsp://10.0.0.7:48010</sessionUrl0></root>";
  };
  LaunchResult r = run_launch(ok, "launch", sample_cfg());
  CHECK(r.ok, "status-200 + sessionUrl0 -> ok");
  CHECK(r.rtspSessionUrl == "rtsp://10.0.0.7:48010", "extracts sessionUrl0");
}

static void test_run_launch_failures() {
  LaunchTransport empty = [](const std::string&, const std::string&) { return std::string(); };
  CHECK(!run_launch(empty, "launch", sample_cfg()).ok, "empty response -> fail");

  LaunchTransport bad = [](const std::string&, const std::string&) {
    return std::string("<root status_code=\"401\"></root>");
  };
  CHECK(!run_launch(bad, "launch", sample_cfg()).ok, "non-200 status -> fail");

  LaunchTransport noUrl = [](const std::string&, const std::string&) {
    return std::string("<root status_code=\"200\"></root>");
  };
  CHECK(!run_launch(noUrl, "launch", sample_cfg()).ok, "200 but no sessionUrl0 -> fail");
}

int main() {
  test_launch_args_shape();
  test_gfe_high_fps_quirk();
  test_hdr_block();
  test_resume_args_minimal();
  test_run_launch_success();
  test_run_launch_failures();

  if (g_failures == 0) {
    std::printf("all nv_launch tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "%d check(s) failed\n", g_failures);
  return 1;
}
