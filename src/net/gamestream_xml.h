// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Minimal, dependency-free parsing for GameStream (NvHTTP) responses — the Qt-free port of
// moonlight-qt's NvHTTP::getXmlString / getXmlStringFromHex / verifyResponseStatus helpers.
// GameStream replies are tiny flat XML (`<root status_code=...><paired>1</paired>…`), so a full
// XML parser is overkill; this extracts the few tags/attrs the pairing + serverinfo flow needs.
#pragma once
#include <string>

namespace ase::net {

// Text content of the first <tag>…</tag> element (attributes on the tag are tolerated). Returns
// "" if the tag is absent, self-closing, or empty. Decodes the basic XML entities.
std::string xml_text(const std::string& xml, const std::string& tag);

// Value of attribute `attr` on the first <tag …> start element (e.g. root's status_code).
// Returns "" if the tag or attribute is absent. Handles single- or double-quoted values.
std::string xml_attr(const std::string& xml, const std::string& tag, const std::string& attr);

// Bytes decoded from the hex-encoded text of <tag> (certs/challenges are lowercase hex on the
// wire). Equivalent to QByteArray::fromHex(getXmlString(...)). "" if absent or malformed.
std::string xml_text_hex(const std::string& xml, const std::string& tag);

// Hex helpers. to_hex is lowercase; from_hex returns "" on odd length or non-hex input.
std::string to_hex(const std::string& bytes);
std::string from_hex(const std::string& hex);

}  // namespace ase::net
