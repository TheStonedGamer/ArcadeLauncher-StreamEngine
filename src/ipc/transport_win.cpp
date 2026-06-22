// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Windows named-pipe client transport. The launcher creates the pipe; the engine connects.
#if defined(_WIN32)
#include "ipc/transport.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ase::ipc {
namespace {

class PipeTransport : public ITransport {
 public:
  explicit PipeTransport(HANDLE h) : h_(h) {}
  ~PipeTransport() override { close(); }

  bool read_exact(void* buf, size_t n) override {
    auto* p = static_cast<unsigned char*>(buf);
    size_t got = 0;
    while (got < n) {
      DWORD r = 0;
      if (!ReadFile(h_, p + got, static_cast<DWORD>(n - got), &r, nullptr) || r == 0) return false;
      got += r;
    }
    return true;
  }
  bool write_all(const void* buf, size_t n) override {
    const auto* p = static_cast<const unsigned char*>(buf);
    size_t sent = 0;
    while (sent < n) {
      DWORD w = 0;
      if (!WriteFile(h_, p + sent, static_cast<DWORD>(n - sent), &w, nullptr) || w == 0) return false;
      sent += w;
    }
    return true;
  }
  void close() override {
    if (h_ != INVALID_HANDLE_VALUE) { CloseHandle(h_); h_ = INVALID_HANDLE_VALUE; }
  }

 private:
  HANDLE h_;
};

}  // namespace

std::unique_ptr<ITransport> connect_local(const std::string& token, std::string& err) {
  const std::string name = "\\\\.\\pipe\\arcade-stream-engine-" + token;
  // Retry briefly if the pipe exists but all instances are momentarily busy.
  for (int attempt = 0; attempt < 50; ++attempt) {
    HANDLE h = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      return std::make_unique<PipeTransport>(h);
    }
    if (GetLastError() != ERROR_PIPE_BUSY) break;
    if (!WaitNamedPipeA(name.c_str(), 200)) break;
  }
  err = "failed to connect to pipe " + name + " (GetLastError=" + std::to_string(GetLastError()) + ")";
  return nullptr;
}

}  // namespace ase::ipc
#endif  // _WIN32
