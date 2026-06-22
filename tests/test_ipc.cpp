// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Unit tests for the IPC core: JSON round-trip, framing, and the handshake/dispatch loop.
// No test framework — a tiny CHECK macro; the process exits non-zero on the first failure so
// CTest reports the build red. Uses an in-memory transport, so it needs no OS pipes/sockets.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ipc/frame.h"
#include "ipc/ipc.h"
#include "ipc/json.h"
#include "ipc/server.h"
#include "ipc/transport.h"

using namespace ase;

static int g_failures = 0;
#define CHECK(cond, what)                                              \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (what), __FILE__, __LINE__); \
      ++g_failures;                                                    \
    }                                                                  \
  } while (0)

// In-memory transport: `in` is bytes the engine reads (what the launcher "sent"); `out`
// accumulates what the engine writes. Single-threaded — preload `in`, run, inspect `out`.
struct MemTransport : ipc::ITransport {
  std::string in;
  size_t pos = 0;
  std::string out;

  bool read_exact(void* buf, size_t n) override {
    if (pos + n > in.size()) return false;  // EOF
    std::memcpy(buf, in.data() + pos, n);
    pos += n;
    return true;
  }
  bool write_all(const void* buf, size_t n) override {
    out.append(static_cast<const char*>(buf), n);
    return true;
  }
  void close() override {}
};

// Encode one frame the way the launcher would, for preloading MemTransport::in.
static std::string framed(const std::string& payload) {
  std::string s;
  const uint32_t n = static_cast<uint32_t>(payload.size());
  s.push_back(static_cast<char>(n & 0xff));
  s.push_back(static_cast<char>((n >> 8) & 0xff));
  s.push_back(static_cast<char>((n >> 16) & 0xff));
  s.push_back(static_cast<char>((n >> 24) & 0xff));
  s += payload;
  return s;
}

// Split a byte buffer of consecutive frames back into payloads.
static std::vector<std::string> unframe_all(const std::string& buf) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i + 4 <= buf.size()) {
    uint32_t n = static_cast<unsigned char>(buf[i]) |
                 (static_cast<unsigned char>(buf[i + 1]) << 8) |
                 (static_cast<unsigned char>(buf[i + 2]) << 16) |
                 (static_cast<uint32_t>(static_cast<unsigned char>(buf[i + 3])) << 24);
    i += 4;
    if (i + n > buf.size()) break;
    out.push_back(buf.substr(i, n));
    i += n;
  }
  return out;
}

static void test_json_roundtrip() {
  const std::string src =
      R"({"kind":"req","id":7,"method":"client.start","params":{"host":"10.0.0.5",)"
      R"("settings":{"width":1920,"height":1080,"fps":60,"hdr":true},"tags":["a","b"],)"
      R"("note":"line\nbreak \"q\" — end","empty":null}})";
  auto v = json::parse(src);
  CHECK(v.has_value(), "parse complex object");
  if (!v) return;
  CHECK(v->get_str("kind") == "req", "kind field");
  CHECK(static_cast<int>(v->get_num("id")) == 7, "id field");
  CHECK(v->get_str("method") == "client.start", "method field");
  const json::Value* params = v->find("params");
  CHECK(params && params->is_object(), "params object");
  CHECK(params && params->get_str("host") == "10.0.0.5", "nested host");
  const json::Value* settings = params ? params->find("settings") : nullptr;
  CHECK(settings && static_cast<int>(settings->get_num("width")) == 1920, "deep width");
  CHECK(settings && settings->get_bool("hdr") == true, "deep bool");
  const json::Value* tags = params ? params->find("tags") : nullptr;
  CHECK(tags && tags->is_array() && tags->arr.size() == 2, "array len");
  CHECK(params && params->get_str("note") == "line\nbreak \"q\" \xE2\x80\x94 end", "escapes + \\u");
  const json::Value* empty = params ? params->find("empty") : nullptr;
  CHECK(empty && empty->is_null(), "null value");

  // dump -> reparse must preserve the structure.
  auto rev = json::parse(json::dump(*v));
  CHECK(rev.has_value(), "reparse dumped");
  CHECK(rev && static_cast<int>(rev->get_num("id")) == 7, "id survives round-trip");

  // Integers dump without a decimal point.
  CHECK(json::dump(json::Value::number(42)) == "42", "int dump");
  // Malformed inputs are rejected.
  CHECK(!json::parse("{bad}").has_value(), "reject bad object");
  CHECK(!json::parse("{\"a\":1} trailing").has_value(), "reject trailing garbage");
  CHECK(!json::parse("[1,2,").has_value(), "reject truncated array");
}

static void test_frame_roundtrip() {
  MemTransport w;
  std::string err;
  CHECK(ipc::write_frame(w, "hello world", err), "write_frame ok");

  MemTransport r;
  r.in = w.out;
  std::string got;
  CHECK(ipc::read_frame(r, got, err), "read_frame ok");
  CHECK(got == "hello world", "frame payload preserved");

  // A clean EOF (no bytes) is false with empty err.
  MemTransport empty;
  std::string got2;
  err.clear();
  CHECK(!ipc::read_frame(empty, got2, err) && err.empty(), "EOF is clean");
}

static void test_dispatch() {
  // Build the launcher's side: a hello, a known req, an unknown req. Then EOF.
  std::string hello = R"({"kind":"hello","protocolVersion":1,"launcherVersion":"x"})";
  std::string req_status = R"({"kind":"req","id":1,"method":"host.status"})";
  std::string req_unknown = R"({"kind":"req","id":2,"method":"host.nope"})";

  MemTransport t;
  t.in = framed(hello) + framed(req_status) + framed(req_unknown);

  ipc::Server s(t);
  s.on("host.status", [](const json::Value&, std::string&, std::string&) {
    json::Value r = json::Value::object();
    r.set("installed", json::Value::boolean(false));
    r.set("appsCount", json::Value::number(0));
    return r;
  });

  std::string err;
  CHECK(s.handshake(err), "handshake succeeds on matching version");
  CHECK(s.run(err), "run returns true on clean EOF");

  auto frames = unframe_all(t.out);
  // Expect: our hello, res(id=1 ok), res(id=2 error).
  CHECK(frames.size() == 3, "engine wrote hello + 2 responses");
  if (frames.size() != 3) return;

  auto h = json::parse(frames[0]);
  CHECK(h && h->get_str("kind") == "hello", "first frame is hello");
  CHECK(h && static_cast<int>(h->get_num("protocolVersion")) == ipc::kProtocolVersion,
        "hello advertises protocol version");

  auto r1 = json::parse(frames[1]);
  CHECK(r1 && r1->get_str("kind") == "res", "res kind");
  CHECK(r1 && static_cast<int>(r1->get_num("id")) == 1, "res echoes id 1");
  CHECK(r1 && r1->get_bool("ok") == true, "known method ok=true");
  const json::Value* result = r1 ? r1->find("result") : nullptr;
  CHECK(result && result->get_bool("installed") == false, "handler result passed through");

  auto r2 = json::parse(frames[2]);
  CHECK(r2 && static_cast<int>(r2->get_num("id")) == 2, "res echoes id 2");
  CHECK(r2 && r2->get_bool("ok") == false, "unknown method ok=false");
  const json::Value* error = r2 ? r2->find("error") : nullptr;
  CHECK(error && error->get_str("code") == "unsupported_method", "unknown -> unsupported_method");
}

static void test_handshake_version_mismatch() {
  MemTransport t;
  t.in = framed(R"({"kind":"hello","protocolVersion":999})");
  ipc::Server s(t);
  std::string err;
  CHECK(!s.handshake(err), "handshake rejects version mismatch");
  CHECK(!err.empty(), "mismatch sets an error message");
}

int main() {
  test_json_roundtrip();
  test_frame_roundtrip();
  test_dispatch();
  test_handshake_version_mismatch();

  if (g_failures == 0) {
    std::printf("all IPC tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "%d check(s) failed\n", g_failures);
  return 1;
}
