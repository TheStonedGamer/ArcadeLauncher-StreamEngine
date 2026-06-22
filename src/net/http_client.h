// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Minimal GameStream HTTP/HTTPS client — the Qt-free port of moonlight-qt's NvHTTP::openConnection.
// GameStream uses two ports: plain HTTP (47989) before pairing, and TLS (47984) once a server cert
// is known. TLS here is mutual: the engine presents its client identity, and the server cert is
// *pinned* (the GameStream model — the cert is self-signed, so default CA validation never applies).
// Only the socket/TLS guts need a platform; the request-building and response-parsing helpers are
// pure and unit-tested. OpenSSL-only (ASE_WITH_OPENSSL).
#pragma once
#include <string>

#include "net/identity.h"

namespace ase::net {

// Parsed HTTP response. `ok` means a well-formed HTTP reply was received (any status).
struct HttpResponse {
  bool ok = false;
  int httpStatus = 0;
  std::string body;
};

// Build the request target: "/<command>?uniqueid=<uid>&uuid=<uuidHex>[&<args>]". `args` is the
// already-encoded query tail (e.g. "phrase=getservercert&salt=..") and may be empty. Mirrors
// NvHTTP::openConnection's URL assembly.
std::string build_request_target(const std::string& command, const std::string& uniqueId,
                                 const std::string& uuidHex, const std::string& args);

// Split a raw HTTP/1.x response (status line + headers + body) into status code + body. Handles
// the CRLFCRLF header/body split and is tolerant of bare-LF. ok=false if it isn't an HTTP reply.
HttpResponse parse_http_response(const std::string& raw);

// A pinned-cert GameStream client for one host. Construct with the client identity and the session
// uniqueid; pin the server cert once it is learned (from the pairing plaincert) to enable HTTPS.
class NvHttpClient {
 public:
  NvHttpClient(std::string host, int httpPort, int httpsPort, ClientIdentity identity,
               std::string uniqueId);

  void set_pinned_server_cert(std::string pemCert) { pinnedServerCert_ = std::move(pemCert); }
  void set_https_port(int port) { httpsPort_ = port; }  // learned from serverinfo's <HttpsPort>
  const std::string& host() const { return host_; }

  // GET `command`?…&`args` over HTTP or HTTPS. Returns the response body, or "" on any transport,
  // TLS, or cert-pinning failure. `timeoutMs` <= 0 means no explicit timeout.
  std::string get_http(const std::string& command, const std::string& args, int timeoutMs);
  std::string get_https(const std::string& command, const std::string& args, int timeoutMs);

 private:
  std::string request(bool https, int port, const std::string& command, const std::string& args,
                      int timeoutMs);

  std::string host_;
  int httpPort_;
  int httpsPort_;
  ClientIdentity identity_;
  std::string uniqueId_;
  std::string pinnedServerCert_;
};

}  // namespace ase::net
