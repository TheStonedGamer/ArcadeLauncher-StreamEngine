// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Unit tests for the GameStream app-list port (src/net/nv_applist.cpp): parsing an `applist` XML
// document into apps, resolving a name/appid reference to a numeric appid, and the fetch flow against
// an injected transport (no sockets). Pure — dependency-free, runs in every build.
#include <cstdio>
#include <string>
#include <vector>

#include "net/nv_applist.h"

using namespace ase;
using namespace ase::net;

static int g_failures = 0;
#define CHECK(cond, what)                                                       \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (what), __FILE__, __LINE__);  \
      ++g_failures;                                                             \
    }                                                                          \
  } while (0)

// A representative applist: attributes on <App>, reordered fields, an HDR flag, XML entities in a
// title, and a trailing entry whose </App> is the last close. Mirrors the GFE/Sunshine wire shape.
static const char* kApplist =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<root status_code=\"200\">"
    "<App><IsHdrSupported>0</IsHdrSupported><AppTitle>Desktop</AppTitle><ID>1</ID></App>"
    "<App><AppTitle>Steam Big Picture</AppTitle><ID>881448767</ID><IsHdrSupported>1</IsHdrSupported></App>"
    "<App><AppTitle>Halo &amp; Friends</AppTitle><ID>42</ID></App>"
    "</root>";

static void test_parse_basic() {
  const std::vector<AppInfo> apps = parse_app_list(kApplist);
  CHECK(apps.size() == 3, "parses all three apps");
  if (apps.size() == 3) {
    CHECK(apps[0].title == "Desktop" && apps[0].id == 1, "first app id+title");
    CHECK(!apps[0].hdrSupported, "first app HDR off");
    CHECK(apps[1].title == "Steam Big Picture" && apps[1].id == 881448767, "second app id+title");
    CHECK(apps[1].hdrSupported, "second app HDR on");
    CHECK(apps[2].title == "Halo & Friends" && apps[2].id == 42, "entity-decoded title + last app");
  }
}

static void test_parse_skips_incomplete_and_empty() {
  // An <App> with no <ID> and one with no <AppTitle> are both unusable and dropped.
  const std::string xml =
      "<root status_code=\"200\">"
      "<App><AppTitle>NoId</AppTitle></App>"
      "<App><ID>7</ID></App>"
      "<App><AppTitle>Good</AppTitle><ID>9</ID></App>"
      "</root>";
  const std::vector<AppInfo> apps = parse_app_list(xml);
  CHECK(apps.size() == 1, "drops the two malformed entries");
  if (!apps.empty()) CHECK(apps[0].id == 9 && apps[0].title == "Good", "keeps the valid one");

  CHECK(parse_app_list("<root status_code=\"200\"></root>").empty(), "empty applist -> no apps");
}

static void test_resolve_numeric_passthrough() {
  const std::vector<AppInfo> apps = parse_app_list(kApplist);
  CHECK(resolve_app_id(apps, "881448767") == 881448767, "numeric passes through verbatim");
  // Numeric passthrough must not require the id to exist in the list (launcher may know it already).
  CHECK(resolve_app_id(apps, "555") == 555, "unknown-but-numeric still passes through");
  CHECK(resolve_app_id(apps, "0") == 0, "zero is not a valid appid");
}

static void test_resolve_by_name() {
  const std::vector<AppInfo> apps = parse_app_list(kApplist);
  CHECK(resolve_app_id(apps, "Desktop") == 1, "exact name match");
  CHECK(resolve_app_id(apps, "steam big picture") == 881448767, "case-insensitive name match");
  CHECK(resolve_app_id(apps, "Halo & Friends") == 42, "matches the decoded title");
  CHECK(resolve_app_id(apps, "Minecraft") == 0, "unknown name -> 0");
  CHECK(resolve_app_id(apps, "") == 0, "empty ref -> 0");
}

static void test_fetch_flow() {
  std::string err;
  AppListTransport ok = []() { return std::string(kApplist); };
  const std::vector<AppInfo> apps = fetch_app_list(ok, err);
  CHECK(err.empty(), "successful fetch leaves err empty");
  CHECK(apps.size() == 3, "fetch parses the body");

  AppListTransport empty = []() { return std::string(); };
  err.clear();
  CHECK(fetch_app_list(empty, err).empty() && !err.empty(), "empty response -> err");

  AppListTransport bad = []() { return std::string("<root status_code=\"401\"></root>"); };
  err.clear();
  CHECK(fetch_app_list(bad, err).empty() && !err.empty(), "non-200 status -> err");

  // A host with zero apps is success, not an error.
  AppListTransport none = []() { return std::string("<root status_code=\"200\"></root>"); };
  err.clear();
  CHECK(fetch_app_list(none, err).empty() && err.empty(), "200 + no apps -> empty, no err");
}

int main() {
  test_parse_basic();
  test_parse_skips_incomplete_and_empty();
  test_resolve_numeric_passthrough();
  test_resolve_by_name();
  test_fetch_flow();

  if (g_failures == 0) {
    std::printf("all nv_applist tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "%d check(s) failed\n", g_failures);
  return 1;
}
