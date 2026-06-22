// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Wire framing: a 4-byte little-endian unsigned length prefix, then that many bytes of UTF-8
// JSON (exactly one object per frame). See docs/IPC.md "Framing".
#pragma once
#include <cstddef>
#include <string>

#include "ipc/transport.h"

namespace ase::ipc {

constexpr size_t kMaxFrame = 8u * 1024u * 1024u;  // reject larger frames

// Returns false and sets err on a hard error (oversize / write failure).
bool write_frame(ITransport& t, const std::string& payload, std::string& err);

// Reads one frame into out. Returns false on EOF (err stays empty) OR on a hard error
// (err set). Callers distinguish: !ok && err.empty() == clean close.
bool read_frame(ITransport& t, std::string& out, std::string& err);

}  // namespace ase::ipc
