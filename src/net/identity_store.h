// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Persistence for the GameStream client — the Qt-free port of moonlight-qt's IdentityManager
// settings store (plus a paired-hosts registry). The client identity (self-signed cert + key +
// uniqueid) must be STABLE across runs: the host remembers the exact cert it paired with, so a
// fresh identity per session would silently un-pair every host. This store generates the identity
// once, persists it under a per-user data dir, and reuses it; it also records each paired host's
// pinned server cert so client.start can present the right TLS pin without re-pairing.
//
// OpenSSL-only (ASE_WITH_OPENSSL) — identity creation needs net::generate_identity. The on-disk
// format is deliberately plain (PEM files + a small JSON registry) so it is inspectable and has no
// extra build deps beyond the standard library.
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "net/identity.h"

namespace ase::net {

// A host this client has paired with. `address` is the key the launcher pairs/starts by (LAN or
// mesh address as a string); `serverCertPem` is the pinned cert proven during pairing.
struct PairedHost {
  std::string address;
  std::string name;           // friendly label (optional; "" if the launcher didn't supply one)
  std::string serverCertPem;  // PEM — pinned for the stream-time TLS handshake
};

// Resolve the default per-user store directory (does not create it):
//   Windows: %APPDATA%\ArcadeLauncher\StreamEngine
//   else:    $XDG_DATA_HOME/arcade-stream-engine  (fallback ~/.local/share/arcade-stream-engine)
// Falls back to "./arcade-stream-engine" if no home/appdata env var is set.
std::string default_store_dir();

// On-disk credential + paired-host store. Construct with an explicit base dir (tests inject a temp
// path); the default-arg overload uses default_store_dir(). The directory is created lazily on the
// first write, so merely constructing the store touches no filesystem.
class IdentityStore {
 public:
  explicit IdentityStore(std::string baseDir);
  IdentityStore() : IdentityStore(default_store_dir()) {}

  const std::string& dir() const { return dir_; }

  // The stable client identity. On first call (no persisted files) it generates a fresh identity +
  // uniqueid via net::generate_identity / net::generate_unique_id and writes them; afterwards it
  // returns the persisted values. Returns false only on an OpenSSL generation failure or an
  // unwritable directory (outputs left unspecified).
  bool load_or_create_identity(ClientIdentity& outIdentity, std::string& outUniqueId);

  // Paired-host registry (hosts.json). Upsert replaces any existing record with the same address.
  bool upsert_host(const PairedHost& host);
  std::optional<PairedHost> find_host(const std::string& address) const;
  std::vector<PairedHost> list_hosts() const;
  bool remove_host(const std::string& address);

 private:
  std::string dir_;
};

}  // namespace ase::net
