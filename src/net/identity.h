// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Client identity for GameStream — the Qt-free port of moonlight-qt's IdentityManager credential
// generation. The engine presents a self-signed RSA cert as its client identity during pairing and
// on every HTTPS request; the server remembers it after pairing. OpenSSL-only (ASE_WITH_OPENSSL).
#pragma once
#include <string>

namespace ase::net {

// A GameStream client identity: a self-signed cert and its private key, both PEM-encoded.
struct ClientIdentity {
  std::string certPem;
  std::string keyPem;
};

// Generate a fresh identity: RSA-2048, self-signed, CN="NVIDIA GameStream Client", 20-year
// validity, SHA-256 signature — matching IdentityManager::createCredentials so GFE/Sunshine accept
// it. Returns false on any OpenSSL failure (out is left unspecified).
bool generate_identity(ClientIdentity& out);

// A random 64-bit value rendered as lowercase hex — the GameStream `uniqueid` query parameter.
// Stable per client/session; the server keys pairing state on it. "" on RNG failure.
std::string generate_unique_id();

}  // namespace ase::net
