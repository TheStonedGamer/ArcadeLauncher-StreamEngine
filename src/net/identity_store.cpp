// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "net/identity_store.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ipc/json.h"

namespace ase::net {

namespace fs = std::filesystem;

namespace {

// File names within the store dir.
constexpr const char* kCertFile = "client.crt";
constexpr const char* kKeyFile = "client.key";
constexpr const char* kUniqueIdFile = "uniqueid";
constexpr const char* kHostsFile = "hosts.json";

std::string getenv_str(const char* name) {
  const char* v = std::getenv(name);
  return v ? std::string(v) : std::string();
}

// Read an entire file into a string; returns false if it does not exist / cannot be read.
bool read_file(const fs::path& p, std::string& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

// Write a file atomically-ish: write a sibling .tmp then rename over the target. Creates parent
// dirs as needed. Returns false on any IO error.
bool write_file(const fs::path& p, const std::string& data) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);  // ok if it already exists
  const fs::path tmp = p.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) return false;
  }
  fs::rename(tmp, p, ec);
  if (ec) {  // Windows rename fails if the target exists — replace explicitly.
    fs::remove(p, ec);
    fs::rename(tmp, p, ec);
  }
  return !ec;
}

}  // namespace

std::string default_store_dir() {
#ifdef _WIN32
  const std::string appdata = getenv_str("APPDATA");
  if (!appdata.empty()) return (fs::path(appdata) / "ArcadeLauncher" / "StreamEngine").string();
#else
  const std::string xdg = getenv_str("XDG_DATA_HOME");
  if (!xdg.empty()) return (fs::path(xdg) / "arcade-stream-engine").string();
  const std::string home = getenv_str("HOME");
  if (!home.empty()) return (fs::path(home) / ".local" / "share" / "arcade-stream-engine").string();
#endif
  return (fs::path(".") / "arcade-stream-engine").string();
}

IdentityStore::IdentityStore(std::string baseDir) : dir_(std::move(baseDir)) {}

bool IdentityStore::load_or_create_identity(ClientIdentity& outIdentity, std::string& outUniqueId) {
  const fs::path base(dir_);
  std::string cert, key, uid;
  const bool haveCert = read_file(base / kCertFile, cert);
  const bool haveKey = read_file(base / kKeyFile, key);
  const bool haveUid = read_file(base / kUniqueIdFile, uid);

  // Reuse the persisted identity only if all three pieces are present and non-empty; otherwise
  // (re)generate the whole set so we never present a half-written identity.
  if (haveCert && haveKey && haveUid && !cert.empty() && !key.empty() && !uid.empty()) {
    outIdentity.certPem = cert;
    outIdentity.keyPem = key;
    outUniqueId = uid;
    return true;
  }

  ClientIdentity fresh;
  if (!generate_identity(fresh)) return false;
  std::string freshUid = generate_unique_id();
  if (freshUid.empty()) return false;

  if (!write_file(base / kCertFile, fresh.certPem)) return false;
  if (!write_file(base / kKeyFile, fresh.keyPem)) return false;
  if (!write_file(base / kUniqueIdFile, freshUid)) return false;

  outIdentity = fresh;
  outUniqueId = freshUid;
  return true;
}

namespace {

// Load hosts.json -> vector<PairedHost>. A missing/malformed file is treated as an empty registry
// (so a corrupt file degrades to "no paired hosts" rather than a hard failure).
std::vector<PairedHost> read_hosts(const fs::path& path) {
  std::vector<PairedHost> hosts;
  std::string text;
  if (!read_file(path, text)) return hosts;
  auto parsed = json::parse(text);
  if (!parsed || !parsed->is_array()) return hosts;
  for (const auto& v : parsed->arr) {
    if (!v.is_object()) continue;
    PairedHost h;
    h.address = v.get_str("address");
    h.name = v.get_str("name");
    h.serverCertPem = v.get_str("serverCert");
    if (!h.address.empty()) hosts.push_back(std::move(h));
  }
  return hosts;
}

bool write_hosts(const fs::path& path, const std::vector<PairedHost>& hosts) {
  json::Value arr = json::Value::array();
  for (const auto& h : hosts) {
    json::Value o = json::Value::object();
    o.set("address", json::Value::string(h.address));
    o.set("name", json::Value::string(h.name));
    o.set("serverCert", json::Value::string(h.serverCertPem));
    arr.push(std::move(o));
  }
  return write_file(path, json::dump(arr));
}

}  // namespace

bool IdentityStore::upsert_host(const PairedHost& host) {
  if (host.address.empty()) return false;
  const fs::path path = fs::path(dir_) / kHostsFile;
  std::vector<PairedHost> hosts = read_hosts(path);
  bool replaced = false;
  for (auto& h : hosts) {
    if (h.address == host.address) {
      h = host;
      replaced = true;
      break;
    }
  }
  if (!replaced) hosts.push_back(host);
  return write_hosts(path, hosts);
}

std::optional<PairedHost> IdentityStore::find_host(const std::string& address) const {
  for (auto& h : read_hosts(fs::path(dir_) / kHostsFile)) {
    if (h.address == address) return h;
  }
  return std::nullopt;
}

std::vector<PairedHost> IdentityStore::list_hosts() const {
  return read_hosts(fs::path(dir_) / kHostsFile);
}

bool IdentityStore::remove_host(const std::string& address) {
  const fs::path path = fs::path(dir_) / kHostsFile;
  std::vector<PairedHost> hosts = read_hosts(path);
  const size_t before = hosts.size();
  hosts.erase(std::remove_if(hosts.begin(), hosts.end(),
                             [&](const PairedHost& h) { return h.address == address; }),
              hosts.end());
  if (hosts.size() == before) return false;  // nothing removed
  return write_hosts(path, hosts);
}

}  // namespace ase::net
