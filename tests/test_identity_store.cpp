// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Unit tests for the persistent identity + paired-host store (src/net/identity_store.cpp).
// Needs OpenSSL (identity generation), so it only builds in the ASE_WITH_OPENSSL configuration.
// Each test runs against a fresh temp directory it removes first; same tiny CHECK harness as the
// other suites.
#include <cstdio>
#include <filesystem>
#include <string>

#include "net/identity_store.h"

using namespace ase;
namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond, what)                                                       \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (what), __FILE__, __LINE__);  \
      ++g_failures;                                                             \
    }                                                                          \
  } while (0)

// A unique-ish temp dir for one test case; removed up front so each run starts clean.
static std::string temp_store(const std::string& tag) {
  fs::path p = fs::temp_directory_path() / ("ase_store_test_" + tag);
  std::error_code ec;
  fs::remove_all(p, ec);
  return p.string();
}

static void test_identity_is_generated_then_persisted() {
  const std::string dir = temp_store("identity");
  net::ClientIdentity id1;
  std::string uid1;
  {
    net::IdentityStore store(dir);
    bool ok = store.load_or_create_identity(id1, uid1);
    CHECK(ok, "first load_or_create_identity succeeds");
    CHECK(!id1.certPem.empty() && !id1.keyPem.empty(), "identity has cert + key");
    CHECK(!uid1.empty(), "identity has a uniqueid");
    CHECK(id1.certPem.find("BEGIN CERTIFICATE") != std::string::npos, "cert is PEM");
  }
  // The files must now exist on disk.
  CHECK(fs::exists(fs::path(dir) / "client.crt"), "client.crt written");
  CHECK(fs::exists(fs::path(dir) / "client.key"), "client.key written");
  CHECK(fs::exists(fs::path(dir) / "uniqueid"), "uniqueid written");

  // A second store over the same dir must reuse the identical identity (stable across sessions —
  // the whole point: a re-minted identity would silently un-pair every host).
  net::IdentityStore store2(dir);
  net::ClientIdentity id2;
  std::string uid2;
  bool ok = store2.load_or_create_identity(id2, uid2);
  CHECK(ok, "second load_or_create_identity succeeds");
  CHECK(id2.certPem == id1.certPem, "cert reused across sessions");
  CHECK(id2.keyPem == id1.keyPem, "key reused across sessions");
  CHECK(uid2 == uid1, "uniqueid reused across sessions");

  std::error_code ec;
  fs::remove_all(dir, ec);
}

static void test_host_registry_roundtrip() {
  const std::string dir = temp_store("hosts");
  {
    net::IdentityStore store(dir);
    CHECK(store.list_hosts().empty(), "registry starts empty");
    CHECK(!store.find_host("10.0.0.5"), "unknown host not found");

    CHECK(store.upsert_host({"10.0.0.5", "Living Room", "-----CERT-A-----"}), "add host A");
    CHECK(store.upsert_host({"10.0.0.9", "Office", "-----CERT-B-----"}), "add host B");
    CHECK(store.list_hosts().size() == 2, "two hosts persisted");

    auto a = store.find_host("10.0.0.5");
    CHECK(a && a->name == "Living Room" && a->serverCertPem == "-----CERT-A-----",
          "host A round-trips name + cert");
  }
  // Reopen: persistence survives a new store instance.
  net::IdentityStore reopened(dir);
  CHECK(reopened.list_hosts().size() == 2, "hosts survive reopen");

  // Upsert replaces (same address) rather than duplicating.
  CHECK(reopened.upsert_host({"10.0.0.5", "Den", "-----CERT-A2-----"}), "re-upsert host A");
  CHECK(reopened.list_hosts().size() == 2, "upsert replaces, no duplicate");
  auto a2 = reopened.find_host("10.0.0.5");
  CHECK(a2 && a2->name == "Den" && a2->serverCertPem == "-----CERT-A2-----", "upsert updated fields");

  // Remove.
  CHECK(reopened.remove_host("10.0.0.5"), "remove host A");
  CHECK(!reopened.remove_host("10.0.0.5"), "remove again reports nothing removed");
  CHECK(reopened.list_hosts().size() == 1, "one host left after remove");
  CHECK(!reopened.upsert_host({"", "bad", "x"}), "empty address rejected");

  std::error_code ec;
  fs::remove_all(dir, ec);
}

int main() {
  test_identity_is_generated_then_persisted();
  test_host_registry_roundtrip();

  if (g_failures == 0) {
    std::printf("all identity-store tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "%d check(s) failed\n", g_failures);
  return 1;
}
