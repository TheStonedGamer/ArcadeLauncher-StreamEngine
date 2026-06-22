// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Unit tests for the GameStream XML/hex parsing layer (src/net/gamestream_xml.cpp). Pure logic,
// no deps — runs in every build configuration. Same tiny CHECK harness as the other suites.
#include <cstdio>
#include <string>

#include "net/gamestream_xml.h"

using namespace ase::net;

static int g_failures = 0;
#define CHECK(cond, what)                                                       \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::fprintf(stderr, "FAIL: %s  (%s:%d)\n", (what), __FILE__, __LINE__);  \
      ++g_failures;                                                             \
    }                                                                          \
  } while (0)

// A representative GameStream pairing-stage-1 response.
static const char* kResp =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<root status_code=\"200\" status_message=\"OK\">"
    "<paired>1</paired>"
    "<plaincert>2d2d2d2d2d</plaincert>"
    "<HttpsPort>47984</HttpsPort>"
    "<empty></empty>"
    "<selfclose/>"
    "</root>";

static void test_text() {
  CHECK(xml_text(kResp, "paired") == "1", "reads <paired>");
  CHECK(xml_text(kResp, "HttpsPort") == "47984", "reads <HttpsPort>");
  CHECK(xml_text(kResp, "empty") == "", "empty element -> empty string");
  CHECK(xml_text(kResp, "selfclose") == "", "self-closing -> empty string");
  CHECK(xml_text(kResp, "missing") == "", "absent tag -> empty string");
}

static void test_no_false_prefix() {
  // "Http" must NOT match "<HttpsPort>" — name-boundary check.
  CHECK(xml_text(kResp, "Http") == "", "prefix tag does not match longer tag");
}

static void test_attr() {
  CHECK(xml_attr(kResp, "root", "status_code") == "200", "reads root status_code");
  CHECK(xml_attr(kResp, "root", "status_message") == "OK", "reads root status_message");
  CHECK(xml_attr(kResp, "root", "missing") == "", "absent attr -> empty");
}

static void test_entities() {
  const std::string xml = "<root><msg>a &amp; b &lt;c&gt; &quot;d&quot;</msg></root>";
  CHECK(xml_text(xml, "msg") == "a & b <c> \"d\"", "decodes XML entities");
}

static void test_hex() {
  CHECK(to_hex(std::string("\x2d\x2d\x2d\x2d\x2d", 5)) == "2d2d2d2d2d", "to_hex lowercases");
  CHECK(from_hex("2d2d2d2d2d") == "-----", "from_hex round-trips");
  CHECK(from_hex("ABCD") == from_hex("abcd"), "from_hex case-insensitive");
  CHECK(from_hex("xyz") == "", "non-hex -> empty");
  CHECK(from_hex("abc") == "", "odd length -> empty");
  // plaincert in kResp is hex for "-----" (PEM header start)
  CHECK(xml_text_hex(kResp, "plaincert") == "-----", "xml_text_hex decodes plaincert");
}

int main() {
  test_text();
  test_no_false_prefix();
  test_attr();
  test_entities();
  test_hex();

  if (g_failures == 0) {
    std::printf("all gamestream-xml tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "%d check(s) failed\n", g_failures);
  return 1;
}
