// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "net/http_client.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
static const socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static const socket_t kInvalidSocket = -1;
#endif

namespace ase::net {

namespace {

void close_socket(socket_t s) {
  if (s == kInvalidSocket) return;
#if defined(_WIN32)
  closesocket(s);
#else
  ::close(s);
#endif
}

// One-time process socket init (no-op on POSIX). Idempotent.
bool socket_startup() {
#if defined(_WIN32)
  static bool started = false;
  if (!started) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    started = true;
  }
#endif
  return true;
}

// Open a TCP connection to host:port. Returns kInvalidSocket on failure. Best-effort recv timeout.
socket_t tcp_connect(const std::string& host, int port, int timeoutMs) {
  if (!socket_startup()) return kInvalidSocket;

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  addrinfo* res = nullptr;
  const std::string portStr = std::to_string(port);
  if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) return kInvalidSocket;

  socket_t sock = kInvalidSocket;
  for (addrinfo* ai = res; ai; ai = ai->ai_next) {
    sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sock == kInvalidSocket) continue;
    if (connect(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) break;
    close_socket(sock);
    sock = kInvalidSocket;
  }
  freeaddrinfo(res);

  if (sock != kInvalidSocket && timeoutMs > 0) {
#if defined(_WIN32)
    DWORD tv = static_cast<DWORD>(timeoutMs);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
  }
  return sock;
}

bool send_all(socket_t s, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    int n = send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

std::string recv_all_plain(socket_t s) {
  std::string out;
  char buf[4096];
  for (;;) {
    int n = recv(s, buf, sizeof(buf), 0);
    if (n <= 0) break;
    out.append(buf, static_cast<size_t>(n));
  }
  return out;
}

bool send_all_ssl(SSL* ssl, const std::string& data) {
  size_t sent = 0;
  while (sent < data.size()) {
    int n = SSL_write(ssl, data.data() + sent, static_cast<int>(data.size() - sent));
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

std::string recv_all_ssl(SSL* ssl) {
  std::string out;
  char buf[4096];
  for (;;) {
    int n = SSL_read(ssl, buf, sizeof(buf));
    if (n <= 0) break;
    out.append(buf, static_cast<size_t>(n));
  }
  return out;
}

X509* read_pem_cert(const std::string& pem) {
  if (pem.empty()) return nullptr;
  BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
  if (!bio) return nullptr;
  X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free_all(bio);
  return cert;
}

// 32 random lowercase-hex chars for the per-request uuid query param.
std::string random_uuid_hex() {
  unsigned char b[16];
  if (RAND_bytes(b, sizeof(b)) != 1) return "00000000000000000000000000000000";
  static const char* d = "0123456789abcdef";
  std::string out;
  for (unsigned char c : b) { out += d[c >> 4]; out += d[c & 0xf]; }
  return out;
}

// Decode an HTTP chunked-transfer body. Returns the reassembled payload.
std::string dechunk(const std::string& body) {
  std::string out;
  size_t i = 0;
  while (i < body.size()) {
    size_t eol = body.find("\r\n", i);
    if (eol == std::string::npos) break;
    size_t len = 0;
    bool any = false;
    for (size_t j = i; j < eol; ++j) {  // hex chunk size (ignore any ;ext)
      char c = body[j];
      int v;
      if (c >= '0' && c <= '9') v = c - '0';
      else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
      else break;
      len = len * 16 + static_cast<size_t>(v);
      any = true;
    }
    if (!any || len == 0) break;  // malformed or final 0-chunk
    size_t start = eol + 2;
    if (start + len > body.size()) break;
    out.append(body, start, len);
    i = start + len + 2;  // skip chunk + trailing CRLF
  }
  return out;
}

}  // namespace

std::string build_request_target(const std::string& command, const std::string& uniqueId,
                                 const std::string& uuidHex, const std::string& args) {
  std::string t = "/" + command + "?uniqueid=" + uniqueId + "&uuid=" + uuidHex;
  if (!args.empty()) t += "&" + args;
  return t;
}

HttpResponse parse_http_response(const std::string& raw) {
  HttpResponse r;
  if (raw.compare(0, 5, "HTTP/") != 0) return r;

  // Status code: integer after the first space on the status line.
  size_t sp = raw.find(' ');
  if (sp == std::string::npos) return r;
  r.httpStatus = std::atoi(raw.c_str() + sp + 1);

  // Header/body split (prefer CRLFCRLF, tolerate LFLF).
  size_t sep = raw.find("\r\n\r\n");
  size_t bodyAt, hdrEnd;
  if (sep != std::string::npos) { hdrEnd = sep; bodyAt = sep + 4; }
  else if ((sep = raw.find("\n\n")) != std::string::npos) { hdrEnd = sep; bodyAt = sep + 2; }
  else { hdrEnd = raw.size(); bodyAt = raw.size(); }

  std::string headers = raw.substr(0, hdrEnd);
  std::transform(headers.begin(), headers.end(), headers.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::string body = raw.substr(bodyAt);
  if (headers.find("transfer-encoding: chunked") != std::string::npos) body = dechunk(body);

  r.ok = true;
  r.body = std::move(body);
  return r;
}

NvHttpClient::NvHttpClient(std::string host, int httpPort, int httpsPort, ClientIdentity identity,
                           std::string uniqueId)
    : host_(std::move(host)),
      httpPort_(httpPort),
      httpsPort_(httpsPort),
      identity_(std::move(identity)),
      uniqueId_(std::move(uniqueId)) {}

std::string NvHttpClient::get_http(const std::string& command, const std::string& args,
                                   int timeoutMs) {
  return request(/*https=*/false, httpPort_, command, args, timeoutMs);
}

std::string NvHttpClient::get_https(const std::string& command, const std::string& args,
                                    int timeoutMs) {
  return request(/*https=*/true, httpsPort_, command, args, timeoutMs);
}

std::string NvHttpClient::request(bool https, int port, const std::string& command,
                                  const std::string& args, int timeoutMs) {
  const std::string target = build_request_target(command, uniqueId_, random_uuid_hex(), args);
  const std::string req = "GET " + target + " HTTP/1.1\r\nHost: " + host_ + ":" +
                          std::to_string(port) + "\r\nConnection: close\r\n\r\n";

  socket_t sock = tcp_connect(host_, port, timeoutMs);
  if (sock == kInvalidSocket) return "";

  std::string raw;
  if (!https) {
    if (send_all(sock, req)) raw = recv_all_plain(sock);
    close_socket(sock);
  } else {
    // Mutual TLS: present our client identity, then pin the expected server cert.
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    SSL* ssl = nullptr;
    bool tlsOk = false;
    if (ctx) {
      // Load client cert + key from in-memory PEM.
      X509* clientCert = read_pem_cert(identity_.certPem);
      EVP_PKEY* clientKey = nullptr;
      if (!identity_.keyPem.empty()) {
        BIO* kb = BIO_new_mem_buf(identity_.keyPem.data(), static_cast<int>(identity_.keyPem.size()));
        if (kb) {
          clientKey = PEM_read_bio_PrivateKey(kb, nullptr, nullptr, nullptr);
          BIO_free_all(kb);
        }
      }
      if (clientCert) SSL_CTX_use_certificate(ctx, clientCert);
      if (clientKey) SSL_CTX_use_PrivateKey(ctx, clientKey);
      // We pin the server cert ourselves after the handshake; skip default CA validation.
      SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

      ssl = SSL_new(ctx);
      if (ssl) {
#if defined(_WIN32)
        SSL_set_fd(ssl, static_cast<int>(sock));
#else
        SSL_set_fd(ssl, sock);
#endif
        if (SSL_connect(ssl) == 1) {
          // Pin: the peer cert must byte-for-byte match the cert we learned during pairing.
          X509* peer = SSL_get_peer_certificate(ssl);
          X509* pinned = read_pem_cert(pinnedServerCert_);
          tlsOk = peer && pinned && X509_cmp(peer, pinned) == 0;
          X509_free(peer);
          X509_free(pinned);
        }
      }
      X509_free(clientCert);
      EVP_PKEY_free(clientKey);
    }

    if (tlsOk && send_all_ssl(ssl, req)) raw = recv_all_ssl(ssl);

    if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
    if (ctx) SSL_CTX_free(ctx);
    close_socket(sock);
  }

  HttpResponse resp = parse_http_response(raw);
  return resp.ok ? resp.body : "";
}

}  // namespace ase::net
