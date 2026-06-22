// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "host/host_apps.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ipc/json.h"

namespace ase::host {

using json::Value;

namespace {

// Per-user StreamEngine data dir (mirrors net::default_store_dir, re-implemented here because that
// one lives in the OpenSSL-only TU and host mode builds without OpenSSL).
std::string data_dir() {
#ifdef _WIN32
  if (const char* appdata = std::getenv("APPDATA")) {
    return std::string(appdata) + "\\ArcadeLauncher\\StreamEngine";
  }
  return ".";
#else
  if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
    return std::string(xdg) + "/arcade-stream-engine";
  }
  if (const char* home = std::getenv("HOME")) {
    return std::string(home) + "/.local/share/arcade-stream-engine";
  }
  return ".";
#endif
}

void set_str(Value& o, const char* k, const std::string& v) {
  if (!v.empty()) o.set(k, Value::string(v));
}

}  // namespace

std::string serialize_apps_json(const std::vector<HostApp>& apps) {
  Value root = Value::object();
  root.set("env", Value::object());
  Value arr = Value::array();
  for (const auto& a : apps) {
    Value o = Value::object();
    o.set("name", Value::string(a.name));
    set_str(o, "cmd", a.cmd);
    set_str(o, "image-path", a.imagePath);
    set_str(o, "working-dir", a.workingDir);
    set_str(o, "output", a.output);
    o.set("elevated", Value::boolean(a.elevated));
    o.set("auto-detach", Value::boolean(a.autoDetach));
    o.set("wait-all", Value::boolean(a.waitAll));
    o.set("exit-timeout", Value::number(a.exitTimeout));
    arr.push(std::move(o));
  }
  root.set("apps", std::move(arr));
  return json::dump(root);
}

std::vector<HostApp> parse_apps_json(const std::string& text) {
  std::vector<HostApp> out;
  auto parsed = json::parse(text);
  if (!parsed) return out;
  const Value* apps = parsed->find("apps");
  if (!apps || !apps->is_array()) return out;
  for (const Value& a : apps->arr) {
    if (!a.is_object()) continue;
    HostApp h;
    h.name = a.get_str("name");
    if (h.name.empty()) continue;  // name is required; a nameless entry isn't a usable app
    h.cmd = a.get_str("cmd");
    h.imagePath = a.get_str("image-path");
    h.workingDir = a.get_str("working-dir");
    h.output = a.get_str("output");
    h.elevated = a.get_bool("elevated", false);
    h.autoDetach = a.get_bool("auto-detach", true);
    h.waitAll = a.get_bool("wait-all", true);
    h.exitTimeout = static_cast<int>(a.get_num("exit-timeout", 5));
    out.push_back(std::move(h));
  }
  return out;
}

std::string default_apps_path() {
#ifdef _WIN32
  return data_dir() + "\\sunshine_apps.json";
#else
  return data_dir() + "/sunshine_apps.json";
#endif
}

std::vector<HostApp> read_apps(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return parse_apps_json(ss.str());
}

bool write_apps(const std::string& path, const std::vector<HostApp>& apps) {
  std::error_code ec;
  const std::filesystem::path p(path);
  if (p.has_parent_path()) {
    std::filesystem::create_directories(p.parent_path(), ec);  // ec ignored — open below is the real test
  }
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f << serialize_apps_json(apps);
  return static_cast<bool>(f);
}

SyncDiff diff_apps(const std::vector<HostApp>& prev, const std::vector<HostApp>& next) {
  SyncDiff d;
  auto same = [](const HostApp& a, const HostApp& b) {
    return a.cmd == b.cmd && a.imagePath == b.imagePath && a.workingDir == b.workingDir &&
           a.output == b.output && a.elevated == b.elevated && a.autoDetach == b.autoDetach &&
           a.waitAll == b.waitAll && a.exitTimeout == b.exitTimeout;
  };
  auto find_by_name = [](const std::vector<HostApp>& v, const std::string& name) -> const HostApp* {
    for (const auto& a : v) {
      if (a.name == name) return &a;
    }
    return nullptr;
  };
  for (const auto& n : next) {
    const HostApp* p = find_by_name(prev, n.name);
    if (!p) {
      ++d.added;
    } else if (!same(*p, n)) {
      ++d.updated;
    }
  }
  for (const auto& p : prev) {
    if (!find_by_name(next, p.name)) ++d.removed;
  }
  return d;
}

}  // namespace ase::host
