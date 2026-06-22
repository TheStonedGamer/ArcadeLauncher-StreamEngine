// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// The GameStream app-list port — the Qt-free port of moonlight-qt's NvHTTP::getAppList(). The host
// exposes its streamable apps (Desktop, Steam Big Picture, each game) as an `applist` XML document;
// the launcher needs it both to show a chooser (client.apps) and to let client.start accept a human
// app *name* instead of the opaque numeric appid (name->id resolution). The XML parse + the
// title->id resolver are pure (unit-tested); the network fetch is transport-injected so the flow can
// be checked against a captured applist with no sockets. The live fetch rides the pinned-cert HTTPS
// channel established at pairing time (applist is an authenticated command), so the NvHttpClient
// convenience is OpenSSL-only.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace ase::net {

class NvHttpClient;

// One streamable app as the host advertises it.
struct AppInfo {
  int id = 0;              // <ID> — the appid client.start launches
  std::string title;       // <AppTitle> — the human name
  bool hdrSupported = false;  // <IsHdrSupported>
};

// Parse a GameStream `applist` response into its apps. Pure; tolerant of attributes, reordered
// fields, and extra per-<App> tags. Entries missing a positive <ID> or a non-empty <AppTitle> are
// skipped (a half-written entry is not a usable app).
std::vector<AppInfo> parse_app_list(const std::string& xml);

// Resolve a user-supplied app reference to a GameStream appid. A positive all-digits `ref` is
// returned verbatim (numeric passthrough — the launcher may already know the id). Otherwise `ref`
// is matched case-insensitively against the app titles. Returns 0 if nothing matches.
int resolve_app_id(const std::vector<AppInfo>& apps, const std::string& ref);

// Fetch + parse the app list. `transport` issues the `applist` HTTPS GET and returns the body ("" on
// failure). On a transport failure or a non-200 status `err` is set and an empty vector is returned;
// a successful fetch of a host with no apps returns empty with `err` left empty.
using AppListTransport = std::function<std::string()>;
std::vector<AppInfo> fetch_app_list(const AppListTransport& transport, std::string& err);

#ifdef ASE_HAVE_OPENSSL
// Convenience over a live pinned-cert client (applist requires the authenticated HTTPS channel, so
// the client must already have the server cert pinned and its HTTPS port set).
std::vector<AppInfo> fetch_app_list(NvHttpClient& client, std::string& err);
#endif

}  // namespace ase::net
