// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "net/gamestream_xml.h"

#include <cctype>

namespace ase::net {

namespace {

// True if `c` ends a tag name (so "<paired>" matches tag "paired" but "<pairedx>" does not).
bool ends_name(char c) { return c == '>' || c == '/' || std::isspace(static_cast<unsigned char>(c)); }

// Find the first start tag "<tag" whose name boundary is correct. Returns its '<' index, or npos.
size_t find_start_tag(const std::string& xml, const std::string& tag) {
  const std::string needle = "<" + tag;
  size_t from = 0;
  for (;;) {
    size_t at = xml.find(needle, from);
    if (at == std::string::npos) return std::string::npos;
    const size_t after = at + needle.size();
    if (after < xml.size() && ends_name(xml[after])) return at;
    from = at + 1;  // false prefix match (e.g. <HttpsPort> when looking for <Http>)
  }
}

// Decode the five predefined XML entities. GameStream cert/challenge values are hex (no entities),
// but status_message text can contain them, so decode for correctness.
std::string decode_entities(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '&') {
      if (s.compare(i, 5, "&amp;") == 0) { out += '&'; i += 5; continue; }
      if (s.compare(i, 4, "&lt;") == 0)  { out += '<'; i += 4; continue; }
      if (s.compare(i, 4, "&gt;") == 0)  { out += '>'; i += 4; continue; }
      if (s.compare(i, 6, "&quot;") == 0) { out += '"'; i += 6; continue; }
      if (s.compare(i, 6, "&apos;") == 0) { out += '\''; i += 6; continue; }
    }
    out += s[i++];
  }
  return out;
}

int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

std::string xml_text(const std::string& xml, const std::string& tag) {
  size_t start = find_start_tag(xml, tag);
  if (start == std::string::npos) return "";
  size_t gt = xml.find('>', start);
  if (gt == std::string::npos) return "";
  if (xml[gt - 1] == '/') return "";  // self-closing <tag/>
  size_t content = gt + 1;
  size_t close = xml.find("</" + tag, content);
  if (close == std::string::npos) return "";
  return decode_entities(xml.substr(content, close - content));
}

std::string xml_attr(const std::string& xml, const std::string& tag, const std::string& attr) {
  size_t start = find_start_tag(xml, tag);
  if (start == std::string::npos) return "";
  size_t gt = xml.find('>', start);
  if (gt == std::string::npos) return "";
  const std::string head = xml.substr(start, gt - start);  // "<tag a=... b=..."
  size_t a = head.find(attr + "=");
  if (a == std::string::npos) return "";
  size_t q = a + attr.size() + 1;
  if (q >= head.size() || (head[q] != '"' && head[q] != '\'')) return "";
  const char quote = head[q];
  size_t end = head.find(quote, q + 1);
  if (end == std::string::npos) return "";
  return decode_entities(head.substr(q + 1, end - (q + 1)));
}

std::string xml_text_hex(const std::string& xml, const std::string& tag) {
  return from_hex(xml_text(xml, tag));
}

std::string to_hex(const std::string& bytes) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (unsigned char c : bytes) {
    out += digits[c >> 4];
    out += digits[c & 0xf];
  }
  return out;
}

std::string from_hex(const std::string& hex) {
  if (hex.size() % 2 != 0) return "";
  std::string out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    int hi = hex_val(hex[i]), lo = hex_val(hex[i + 1]);
    if (hi < 0 || lo < 0) return "";
    out += static_cast<char>((hi << 4) | lo);
  }
  return out;
}

}  // namespace ase::net
