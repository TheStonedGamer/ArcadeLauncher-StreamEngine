// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "ipc/frame.h"

#include <cstdint>

namespace ase::ipc {

bool write_frame(ITransport& t, const std::string& payload, std::string& err) {
  if (payload.size() > kMaxFrame) { err = "frame exceeds 8 MiB cap"; return false; }
  const uint32_t n = static_cast<uint32_t>(payload.size());
  unsigned char hdr[4] = {
      static_cast<unsigned char>(n & 0xff),
      static_cast<unsigned char>((n >> 8) & 0xff),
      static_cast<unsigned char>((n >> 16) & 0xff),
      static_cast<unsigned char>((n >> 24) & 0xff),
  };
  if (!t.write_all(hdr, 4)) { err = "write frame header failed"; return false; }
  if (n && !t.write_all(payload.data(), n)) { err = "write frame body failed"; return false; }
  return true;
}

bool read_frame(ITransport& t, std::string& out, std::string& err) {
  unsigned char hdr[4];
  if (!t.read_exact(hdr, 4)) return false;  // clean EOF (err stays empty)
  const uint32_t n = static_cast<uint32_t>(hdr[0]) |
                     (static_cast<uint32_t>(hdr[1]) << 8) |
                     (static_cast<uint32_t>(hdr[2]) << 16) |
                     (static_cast<uint32_t>(hdr[3]) << 24);
  if (n > kMaxFrame) { err = "incoming frame exceeds 8 MiB cap"; return false; }
  out.resize(n);
  if (n && !t.read_exact(&out[0], n)) { err = "truncated frame body"; return false; }
  return true;
}

}  // namespace ase::ipc
