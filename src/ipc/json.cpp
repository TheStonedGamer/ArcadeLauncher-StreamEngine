// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "ipc/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ase::json {

namespace {

void escape_to(const std::string& s, std::string& out) {
  out.push_back('"');
  for (unsigned char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));  // pass UTF-8 bytes through verbatim
        }
    }
  }
  out.push_back('"');
}

void dump_to(const Value& v, std::string& out) {
  switch (v.type) {
    case Value::Type::Null: out += "null"; break;
    case Value::Type::Bool: out += v.b ? "true" : "false"; break;
    case Value::Type::Num: {
      char buf[32];
      if (std::isfinite(v.num) && v.num == static_cast<double>(static_cast<long long>(v.num)) &&
          std::fabs(v.num) < 9e15) {
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v.num));
      } else {
        std::snprintf(buf, sizeof buf, "%.17g", v.num);
      }
      out += buf;
      break;
    }
    case Value::Type::Str: escape_to(v.str, out); break;
    case Value::Type::Arr: {
      out.push_back('[');
      bool first = true;
      for (const Value& e : v.arr) {
        if (!first) out.push_back(',');
        first = false;
        dump_to(e, out);
      }
      out.push_back(']');
      break;
    }
    case Value::Type::Obj: {
      out.push_back('{');
      bool first = true;
      for (const auto& kv : v.obj) {
        if (!first) out.push_back(',');
        first = false;
        escape_to(kv.first, out);
        out.push_back(':');
        dump_to(kv.second, out);
      }
      out.push_back('}');
      break;
    }
  }
}

// Recursive-descent parser. ok flips false on the first malformed token and stays false.
struct Parser {
  const std::string& s;
  size_t i = 0;
  bool ok = true;
  explicit Parser(const std::string& src) : s(src) {}

  void ws() {
    while (i < s.size()) {
      char c = s[i];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i;
      else break;
    }
  }
  char peek() const { return i < s.size() ? s[i] : '\0'; }

  void encode_utf8(unsigned cp, std::string& out) {
    if (cp <= 0x7f) {
      out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ff) {
      out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
      out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
      out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
  }

  unsigned hex4() {
    unsigned v = 0;
    for (int n = 0; n < 4; ++n) {
      if (i >= s.size()) { ok = false; return 0; }
      char c = s[i++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
      else { ok = false; return 0; }
    }
    return v;
  }

  std::string str() {
    std::string out;
    if (peek() != '"') { ok = false; return out; }
    ++i;
    while (i < s.size()) {
      char c = s[i++];
      if (c == '"') return out;
      if (c == '\\') {
        if (i >= s.size()) { ok = false; return out; }
        char e = s[i++];
        switch (e) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            unsigned cp = hex4();
            if (!ok) return out;
            if (cp >= 0xd800 && cp <= 0xdbff) {  // high surrogate -> expect low
              if (i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                i += 2;
                unsigned lo = hex4();
                if (!ok) return out;
                if (lo >= 0xdc00 && lo <= 0xdfff) {
                  cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                } else { ok = false; return out; }
              } else { ok = false; return out; }
            }
            encode_utf8(cp, out);
            break;
          }
          default: ok = false; return out;
        }
      } else {
        out.push_back(c);
      }
    }
    ok = false;  // unterminated string
    return out;
  }

  Value number() {
    size_t start = i;
    if (peek() == '-') ++i;
    while (i < s.size()) {
      char c = s[i];
      if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') ++i;
      else break;
    }
    if (i == start) { ok = false; return {}; }
    std::string tok = s.substr(start, i - start);
    char* end = nullptr;
    double d = std::strtod(tok.c_str(), &end);
    if (end != tok.c_str() + tok.size()) { ok = false; return {}; }
    return Value::number(d);
  }

  bool literal(const char* lit) {
    size_t n = std::strlen(lit);
    if (s.compare(i, n, lit) == 0) { i += n; return true; }
    ok = false;
    return false;
  }

  Value object() {
    Value v = Value::object();
    ++i;  // '{'
    ws();
    if (peek() == '}') { ++i; return v; }
    for (;;) {
      ws();
      if (peek() != '"') { ok = false; return v; }
      std::string key = str();
      if (!ok) return v;
      ws();
      if (peek() != ':') { ok = false; return v; }
      ++i;
      Value child = value();
      if (!ok) return v;
      v.obj[key] = std::move(child);
      ws();
      char c = peek();
      if (c == ',') { ++i; continue; }
      if (c == '}') { ++i; return v; }
      ok = false;
      return v;
    }
  }

  Value array() {
    Value v = Value::array();
    ++i;  // '['
    ws();
    if (peek() == ']') { ++i; return v; }
    for (;;) {
      Value child = value();
      if (!ok) return v;
      v.arr.push_back(std::move(child));
      ws();
      char c = peek();
      if (c == ',') { ++i; continue; }
      if (c == ']') { ++i; return v; }
      ok = false;
      return v;
    }
  }

  Value value() {
    ws();
    if (i >= s.size()) { ok = false; return {}; }
    char c = peek();
    switch (c) {
      case '{': return object();
      case '[': return array();
      case '"': { Value v; v.type = Value::Type::Str; v.str = str(); return v; }
      case 't': return literal("true") ? Value::boolean(true) : Value{};
      case 'f': return literal("false") ? Value::boolean(false) : Value{};
      case 'n': return literal("null") ? Value::null() : Value{};
      default:
        if (c == '-' || (c >= '0' && c <= '9')) return number();
        ok = false;
        return {};
    }
  }
};

}  // namespace

std::string dump(const Value& v) {
  std::string out;
  dump_to(v, out);
  return out;
}

std::optional<Value> parse(const std::string& text) {
  Parser p(text);
  Value v = p.value();
  if (!p.ok) return std::nullopt;
  p.ws();
  if (p.i != text.size()) return std::nullopt;  // trailing garbage
  return v;
}

}  // namespace ase::json
