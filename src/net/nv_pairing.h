// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// The GameStream 5-stage pairing handshake — the Qt-free port of moonlight-qt's
// NvPairingManager::pair(). It is decoupled from the network via an injected transport callback so
// the full state machine (and its crypto sequencing) is unit-testable against a simulated server
// with no sockets. The production path plugs an NvHttpClient into the same callback.
// OpenSSL-only (ASE_WITH_OPENSSL).
#pragma once
#include <functional>
#include <string>

#include "net/identity.h"
#include "net/pairing_crypto.h"

namespace ase::net {

class NvHttpClient;

enum class PairState {
  Paired,             // success — serverCertPem is populated
  Failed,             // protocol/transport failure at some stage
  PinWrong,           // server's response didn't match the PIN
  AlreadyInProgress,  // server returned no plaincert (already pairing with someone)
};

struct PairResult {
  PairState state = PairState::Failed;
  std::string serverCertPem;  // the pinned server cert (valid when state == Paired)
};

// Perform a GameStream `pair`/`unpair` request. `https` selects the TLS base URL; `command` is the
// path ("pair"/"unpair"); `args` is the encoded query tail. Returns the response body, or "" on any
// failure. Implementations add uniqueid/uuid and (for https) the client cert + server-cert pin.
using PairTransport =
    std::function<std::string(bool https, const std::string& command, const std::string& args)>;

// Run the 5-stage pairing flow.
//   transport        — performs the HTTP/HTTPS requests (see above).
//   identity         — the client cert (sent as clientcert) + key (signs the pairing secret).
//   pin              — the PIN shown to the user (e.g. "1234").
//   serverMajorVersion — GFE/Sunshine generation; >= 7 selects SHA-256, else SHA-1.
//   setPinnedCert    — called once with the server's plaincert PEM so the transport can pin it for
//                      the stage-5 HTTPS request.
//   randomFn         — random-byte source (injectable for deterministic tests; defaults to OpenSSL).
PairResult nv_pair(const PairTransport& transport, const ClientIdentity& identity,
                   const std::string& pin, int serverMajorVersion,
                   const std::function<void(const std::string&)>& setPinnedCert,
                   const std::function<std::string(int)>& randomFn = random_bytes);

// Convenience wrapper that wires nv_pair onto a live NvHttpClient (real sockets). `identity` must be
// the same identity the client was constructed with.
PairResult pair_with_client(NvHttpClient& client, const ClientIdentity& identity,
                            const std::string& pin, int serverMajorVersion);

}  // namespace ase::net
