// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Detect a Sunshine that already exists on this machine, so host mode can ADOPT it instead of
// starting a second instance — two separate copies would collide on Sunshine's fixed GameStream
// ports and the second would fail to bind. Two axes:
//   - a Sunshine *binary* installed system-wide → start that instead of downloading our sidecar
//   - a Sunshine *already listening* → adopt the live instance and never spawn our own
#pragma once
#include <string>
#include <vector>

namespace ase::host {

// Candidate paths where a system-installed Sunshine may live, in preference order
// (derived from env / PATH). Does not touch the filesystem.
std::vector<std::string> system_sunshine_candidates();

// First system candidate that `exists`, else "" — `exists` is injected so this is unit-testable
// without a real Sunshine on disk.
std::string resolve_system_sunshine(bool (*exists)(const std::string&));

// True if a Sunshine GameStream host already answers on this machine (a short localhost probe of
// Sunshine's default HTTP/HTTPS ports). Used to adopt a pre-existing instance.
bool sunshine_is_listening();

}  // namespace ase::host
