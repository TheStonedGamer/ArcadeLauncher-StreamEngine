// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Tests for the NvHTTP port's identity generation, HTTP helpers, and the 5-stage pair() state
// machine. The state machine is exercised end-to-end against an in-process simulated GameStream
// server that performs the real host-side crypto — proving the client's stage sequencing, crypto
// wiring, and PIN/MITM checks without any sockets. Needs OpenSSL (ASE_WITH_OPENSSL build only).
#include <cstdio>
#include <string>

#include "net/gamestream_xml.h"
#include "net/http_client.h"
#include "net/identity.h"
#include "net/nv_pairing.h"
#include "net/pairing_crypto.h"

using namespace ase::net;

static int g_failures = 0;
#define CHECK(cond, what)                                                       \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (what), __FILE__, __LINE__);  \
      ++g_failures;                                                             \
    }                                                                          \
  } while (0)

// ---- identity ------------------------------------------------------------------------------
static void test_identity() {
  ClientIdentity id;
  CHECK(generate_identity(id), "identity generated");
  CHECK(id.certPem.find("BEGIN CERTIFICATE") != std::string::npos, "cert is PEM");
  CHECK(id.keyPem.find("PRIVATE KEY") != std::string::npos, "key is PEM");
  // The generated key and cert must form a working sign/verify pair.
  std::string sig = sign_message("hello", id.keyPem);
  CHECK(!sig.empty() && verify_signature("hello", sig, id.certPem), "generated identity signs/verifies");
  CHECK(!signature_from_pem_cert(id.certPem).empty(), "generated cert carries a signature");

  std::string uid = generate_unique_id();
  CHECK(uid.size() == 16, "unique id is 16 hex chars");
  CHECK(uid.find_first_not_of("0123456789abcdef") == std::string::npos, "unique id is lowercase hex");
}

// ---- pure HTTP helpers ---------------------------------------------------------------------
static void test_http_helpers() {
  CHECK(build_request_target("pair", "abc", "deadbeef", "phrase=getservercert") ==
            "/pair?uniqueid=abc&uuid=deadbeef&phrase=getservercert",
        "request target with args");
  CHECK(build_request_target("serverinfo", "abc", "deadbeef", "") ==
            "/serverinfo?uniqueid=abc&uuid=deadbeef",
        "request target without args");

  HttpResponse ok = parse_http_response(
      "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Type: text/xml\r\n\r\nhello");
  CHECK(ok.ok && ok.httpStatus == 200 && ok.body == "hello", "parses status + body");

  HttpResponse chunked = parse_http_response(
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n");
  CHECK(chunked.ok && chunked.body == "hello world", "decodes chunked body");

  CHECK(!parse_http_response("garbage not http").ok, "rejects non-HTTP");
}

// ---- simulated GameStream server -----------------------------------------------------------
// Implements the host side of the 5-stage handshake with real crypto, so a correct client reaches
// PAIRED. Uses the *real* PIN to derive its AES key; a client given the wrong PIN must get PinWrong.
namespace {

// Extract the value of `name=` from an "a=1&b=2" query tail (up to the next '&' or end).
std::string param(const std::string& args, const std::string& name) {
  size_t at = args.find(name + "=");
  if (at == std::string::npos) return "";
  at += name.size() + 1;
  size_t end = args.find('&', at);
  return args.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

std::string root200(const std::string& inner) {
  return "<?xml version=\"1.0\"?><root status_code=\"200\">" + inner + "</root>";
}

class SimServer {
 public:
  SimServer(std::string pin, int gen, bool offerCert = true)
      : pin_(std::move(pin)), gen7_(gen >= 7), offerCert_(offerCert) {
    generate_identity(id_);
  }

  const std::string& serverCertPem() const { return id_.certPem; }

  // Matches the PairTransport signature.
  std::string respond(bool /*https*/, const std::string& command, const std::string& args) {
    if (command == "unpair") return root200("");
    if (command != "pair") return "";

    if (args.find("phrase=getservercert") != std::string::npos) {
      std::string salt = from_hex(param(args, "salt"));
      aesKey_ = derive_aes_key(salt, pin_, gen7_);
      std::string cert = offerCert_ ? to_hex(id_.certPem) : "";  // "" => already-in-progress path
      return root200("<paired>1</paired><plaincert>" + cert + "</plaincert>");
    }

    if (args.find("clientchallenge=") != std::string::npos) {
      std::string clientChallenge = aes_ecb_decrypt(from_hex(param(args, "clientchallenge")), aesKey_);
      serverSecret_ = std::string(16, '\x5a');
      std::string serverChallenge(16, '\x33');
      std::string serverResponse =
          (gen7_ ? sha256 : sha1)(clientChallenge + signature_from_pem_cert(id_.certPem) + serverSecret_);
      std::string blob = aes_ecb_encrypt(serverResponse + serverChallenge, aesKey_);
      return root200("<paired>1</paired><challengeresponse>" + to_hex(blob) + "</challengeresponse>");
    }

    if (args.find("serverchallengeresp=") != std::string::npos) {
      std::string secret = serverSecret_ + sign_message(serverSecret_, id_.keyPem);
      return root200("<paired>1</paired><pairingsecret>" + to_hex(secret) + "</pairingsecret>");
    }

    if (args.find("clientpairingsecret=") != std::string::npos ||
        args.find("phrase=pairchallenge") != std::string::npos) {
      return root200("<paired>1</paired>");
    }
    return "";
  }

 private:
  std::string pin_;
  bool gen7_;
  bool offerCert_;
  ClientIdentity id_;
  std::string aesKey_;
  std::string serverSecret_;
};

// Deterministic byte source so test runs are reproducible (and to prove randomFn injection works).
std::string seq_bytes(int n) {
  static unsigned char counter = 1;
  std::string out;
  for (int i = 0; i < n; ++i) out += static_cast<char>(counter++);
  return out;
}

}  // namespace

static void test_pair_success() {
  SimServer server("4321", 7);
  ClientIdentity client;
  generate_identity(client);
  std::string pinned;

  PairTransport transport = [&](bool https, const std::string& cmd, const std::string& args) {
    return server.respond(https, cmd, args);
  };
  auto setPinned = [&](const std::string& pem) { pinned = pem; };

  PairResult r = nv_pair(transport, client, "4321", 7, setPinned, seq_bytes);
  CHECK(r.state == PairState::Paired, "correct PIN -> PAIRED");
  CHECK(r.serverCertPem == server.serverCertPem(), "returns the server cert");
  CHECK(pinned == server.serverCertPem(), "server cert was pinned mid-flow");
}

static void test_pair_wrong_pin() {
  SimServer server("4321", 7);
  ClientIdentity client;
  generate_identity(client);
  std::string pinned;
  PairTransport transport = [&](bool https, const std::string& cmd, const std::string& args) {
    return server.respond(https, cmd, args);
  };
  // Client uses a different PIN than the server -> derived keys differ -> challenge mismatch.
  PairResult r = nv_pair(transport, client, "0000", 7, [&](const std::string& p) { pinned = p; },
                         seq_bytes);
  CHECK(r.state == PairState::PinWrong, "wrong PIN -> PinWrong");
}

static void test_pair_already_in_progress() {
  SimServer server("4321", 7, /*offerCert=*/false);  // returns empty plaincert
  ClientIdentity client;
  generate_identity(client);
  PairTransport transport = [&](bool https, const std::string& cmd, const std::string& args) {
    return server.respond(https, cmd, args);
  };
  PairResult r = nv_pair(transport, client, "4321", 7, [](const std::string&) {}, seq_bytes);
  CHECK(r.state == PairState::AlreadyInProgress, "no plaincert -> AlreadyInProgress");
}

static void test_pair_transport_failure() {
  ClientIdentity client;
  generate_identity(client);
  // Transport always fails (empty) -> stage 1 fails cleanly.
  PairTransport dead = [](bool, const std::string&, const std::string&) { return std::string(); };
  PairResult r = nv_pair(dead, client, "4321", 7, [](const std::string&) {}, seq_bytes);
  CHECK(r.state == PairState::Failed, "dead transport -> Failed");
}

int main() {
  test_identity();
  test_http_helpers();
  test_pair_success();
  test_pair_wrong_pin();
  test_pair_already_in_progress();
  test_pair_transport_failure();

  if (g_failures == 0) {
    std::printf("all nv-pairing tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "%d check(s) failed\n", g_failures);
  return 1;
}
