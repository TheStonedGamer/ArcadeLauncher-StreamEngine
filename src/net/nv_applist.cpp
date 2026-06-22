// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "net/nv_applist.h"

#include <cctype>
#include <cstdlib>

#include "net/gamestream_xml.h"

#ifdef ASE_HAVE_OPENSSL
#include "net/http_client.h"
#endif

namespace ase::net {

namespace {

// applist can take a moment on a host with many apps; matches REQUEST_TIMEOUT_MS in moonlight.
constexpr int kAppListTimeoutMs = 5000;

// Find the next `<App>` start tag at/after `from`, rejecting `<AppTitle>` and other "<App…"-prefixed
// siblings: the name must end at '>' or whitespace. Returns the index just past the '>' (the start of
// the element body), or npos.
size_t find_app_body(const std::string& xml, size_t from, size_t& open_out) {
  const std::string needle = "<App";
  for (size_t at = xml.find(needle, from); at != std::string::npos;
       at = xml.find(needle, at + 1)) {
    const size_t after = at + needle.size();
    if (after >= xml.size()) return std::string::npos;
    const char c = xml[after];
    if (c == '>' || c == '/' || std::isspace(static_cast<unsigned char>(c))) {
      const size_t gt = xml.find('>', at);
      if (gt == std::string::npos) return std::string::npos;
      open_out = at;
      return gt + 1;
    }
    // else: a longer name like <AppTitle> — keep scanning.
  }
  return std::string::npos;
}

bool all_digits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

bool iequals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::vector<AppInfo> parse_app_list(const std::string& xml) {
  std::vector<AppInfo> apps;
  size_t pos = 0;
  for (;;) {
    size_t open = 0;
    const size_t body = find_app_body(xml, pos, open);
    if (body == std::string::npos) break;
    // <App> elements don't nest, so the next </App> closes this one; tolerate a missing close on the
    // final entry by taking the rest of the document.
    const size_t close = xml.find("</App>", body);
    const size_t end = (close == std::string::npos) ? xml.size() : close;
    const std::string block = xml.substr(body, end - body);

    AppInfo a;
    a.title = xml_text(block, "AppTitle");
    a.id = std::atoi(xml_text(block, "ID").c_str());
    a.hdrSupported = xml_text(block, "IsHdrSupported") == "1";
    if (a.id > 0 && !a.title.empty()) apps.push_back(std::move(a));

    pos = (close == std::string::npos) ? xml.size() : close + 6;  // past "</App>"
  }
  return apps;
}

int resolve_app_id(const std::vector<AppInfo>& apps, const std::string& ref) {
  if (all_digits(ref)) {
    const int v = std::atoi(ref.c_str());
    if (v > 0) return v;  // numeric passthrough
  }
  for (const auto& a : apps) {
    if (iequals(a.title, ref)) return a.id;
  }
  return 0;
}

std::vector<AppInfo> fetch_app_list(const AppListTransport& transport, std::string& err) {
  err.clear();
  const std::string xml = transport();
  if (xml.empty() || xml_attr(xml, "root", "status_code") != "200") {
    err = "host returned no status-200 applist response";
    return {};
  }
  return parse_app_list(xml);
}

#ifdef ASE_HAVE_OPENSSL
std::vector<AppInfo> fetch_app_list(NvHttpClient& client, std::string& err) {
  AppListTransport transport = [&client]() {
    return client.get_https("applist", "", kAppListTimeoutMs);
  };
  return fetch_app_list(transport, err);
}
#endif

}  // namespace ase::net
