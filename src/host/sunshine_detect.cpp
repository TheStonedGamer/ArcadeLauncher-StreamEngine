// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "host/sunshine_detect.h"

#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
static const socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static const socket_t kInvalidSocket = -1;
#endif

namespace ase::host {

namespace {

// Sunshine's default GameStream control ports (HTTP, then HTTPS). One open is enough to conclude a
// host is up. These are the well-known Moonlight/GameStream ports — collisions with anything else
// are unlikely, and a false positive only means we adopt rather than start (the safe direction).
constexpr unsigned short kSunshinePorts[] = {47989, 47984};
constexpr int kProbeTimeoutMs = 250;

void close_socket(socket_t s) {
  if (s == kInvalidSocket) return;
#if defined(_WIN32)
  closesocket(s);
#else
  ::close(s);
#endif
}

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

// Non-blocking connect to 127.0.0.1:port, capped at kProbeTimeoutMs so a dropped (vs refused) port
// can't stall host.status. True only if the connection actually establishes.
bool port_open(unsigned short port) {
  if (!socket_startup()) return false;

  socket_t s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == kInvalidSocket) return false;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

#if defined(_WIN32)
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);
#else
  int flags = fcntl(s, F_GETFL, 0);
  fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif

  bool ok = false;
  int r = connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (r == 0) {
    ok = true;  // connected immediately (typical on localhost)
  } else {
#if defined(_WIN32)
    const bool inProgress = (WSAGetLastError() == WSAEWOULDBLOCK);
#else
    const bool inProgress = (errno == EINPROGRESS);
#endif
    if (inProgress) {
      fd_set wf;
      FD_ZERO(&wf);
      FD_SET(s, &wf);
      timeval tv{kProbeTimeoutMs / 1000, (kProbeTimeoutMs % 1000) * 1000};
#if defined(_WIN32)
      const int nfds = 0;  // ignored on Windows
#else
      const int nfds = s + 1;
#endif
      if (select(nfds, nullptr, &wf, nullptr, &tv) > 0) {
        int err = 0;
#if defined(_WIN32)
        int len = sizeof(err);
#else
        socklen_t len = sizeof(err);
#endif
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
        ok = (err == 0);
      }
    }
  }

  close_socket(s);
  return ok;
}

}  // namespace

bool sunshine_is_listening() {
  for (unsigned short port : kSunshinePorts) {
    if (port_open(port)) return true;
  }
  return false;
}

std::vector<std::string> system_sunshine_candidates() {
  std::vector<std::string> out;
#if defined(_WIN32)
  // Sunshine's MSI installs under Program Files\Sunshine; cover both 64/32-bit roots + per-user.
  for (const char* var : {"ProgramFiles", "ProgramW6432", "ProgramFiles(x86)", "LOCALAPPDATA"}) {
    const char* base = std::getenv(var);
    if (base && *base) out.push_back(std::string(base) + "\\Sunshine\\sunshine.exe");
  }
#else
  // PATH entries first (covers a packaged `sunshine` on the user's PATH), then common prefixes.
  if (const char* path = std::getenv("PATH")) {
    const std::string p(path);
    size_t start = 0;
    while (start <= p.size()) {
      const size_t sep = p.find(':', start);
      const std::string dir = p.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
      if (!dir.empty()) out.push_back(dir + "/sunshine");
      if (sep == std::string::npos) break;
      start = sep + 1;
    }
  }
  for (const char* dir : {"/usr/bin", "/usr/local/bin", "/opt/sunshine"}) {
    out.push_back(std::string(dir) + "/sunshine");
  }
#endif
  return out;
}

std::string resolve_system_sunshine(bool (*exists)(const std::string&)) {
  for (const auto& candidate : system_sunshine_candidates()) {
    if (exists(candidate)) return candidate;
  }
  return "";
}

}  // namespace ase::host
