// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Unix domain-socket client transport. The launcher creates/binds/listens; the engine connects.
#if !defined(_WIN32)
#include "ipc/transport.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace ase::ipc {
namespace {

class SocketTransport : public ITransport {
 public:
  explicit SocketTransport(int fd) : fd_(fd) {}
  ~SocketTransport() override { close(); }

  bool read_exact(void* buf, size_t n) override {
    auto* p = static_cast<unsigned char*>(buf);
    size_t got = 0;
    while (got < n) {
      ssize_t r = ::recv(fd_, p + got, n - got, 0);
      if (r == 0) return false;  // peer closed
      if (r < 0) { if (errno == EINTR) continue; return false; }
      got += static_cast<size_t>(r);
    }
    return true;
  }
  bool write_all(const void* buf, size_t n) override {
    const auto* p = static_cast<const unsigned char*>(buf);
    size_t sent = 0;
    while (sent < n) {
      ssize_t w = ::send(fd_, p + sent, n - sent, MSG_NOSIGNAL);
      if (w < 0) { if (errno == EINTR) continue; return false; }
      sent += static_cast<size_t>(w);
    }
    return true;
  }
  void close() override {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
  }

 private:
  int fd_;
};

std::string socket_path(const std::string& token) {
  const char* runtime = std::getenv("XDG_RUNTIME_DIR");
  std::string dir = (runtime && *runtime) ? runtime : "/tmp";
  return dir + "/arcade-stream-engine-" + token + ".sock";
}

}  // namespace

std::unique_ptr<ITransport> connect_local(const std::string& token, std::string& err) {
  const std::string path = socket_path(token);
  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) { err = std::string("socket() failed: ") + std::strerror(errno); return nullptr; }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (path.size() >= sizeof(addr.sun_path)) {
    err = "socket path too long: " + path;
    ::close(fd);
    return nullptr;
  }
  std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    err = "failed to connect to " + path + ": " + std::strerror(errno);
    ::close(fd);
    return nullptr;
  }
  return std::make_unique<SocketTransport>(fd);
}

}  // namespace ase::ipc
#endif  // !_WIN32
