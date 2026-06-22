// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// Minimal, dependency-free JSON for the launcher<->engine control protocol (docs/IPC.md).
// Messages are small; this is deliberately a compact tagged-union value, not a fast/streaming
// parser. No external deps keeps the GPL engine's build surface honest and self-contained.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ase::json {

// Recursive tagged value. vector/map of the (then-incomplete) Value as members is well-formed
// — the type is complete at the closing brace. Avoids the std::variant-of-incomplete-type trap.
struct Value {
  enum class Type { Null, Bool, Num, Str, Arr, Obj };
  Type type = Type::Null;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<Value> arr;
  std::map<std::string, Value> obj;

  Value() = default;

  static Value null() { return Value{}; }
  static Value boolean(bool v) { Value x; x.type = Type::Bool; x.b = v; return x; }
  static Value number(double v) { Value x; x.type = Type::Num; x.num = v; return x; }
  static Value string(std::string v) { Value x; x.type = Type::Str; x.str = std::move(v); return x; }
  static Value array() { Value x; x.type = Type::Arr; return x; }
  static Value object() { Value x; x.type = Type::Obj; return x; }

  bool is_object() const { return type == Type::Obj; }
  bool is_array() const { return type == Type::Arr; }
  bool is_string() const { return type == Type::Str; }
  bool is_number() const { return type == Type::Num; }
  bool is_bool() const { return type == Type::Bool; }
  bool is_null() const { return type == Type::Null; }

  // Object builders / accessors (no-op-safe: set() coerces this into an object).
  Value& set(const std::string& k, Value v) { type = Type::Obj; obj[k] = std::move(v); return *this; }
  Value& push(Value v) { type = Type::Arr; arr.push_back(std::move(v)); return *this; }

  const Value* find(const std::string& k) const {
    if (type != Type::Obj) return nullptr;
    auto it = obj.find(k);
    return it == obj.end() ? nullptr : &it->second;
  }
  std::string get_str(const std::string& k, const std::string& d = "") const {
    const Value* v = find(k);
    return (v && v->is_string()) ? v->str : d;
  }
  double get_num(const std::string& k, double d = 0) const {
    const Value* v = find(k);
    return (v && v->is_number()) ? v->num : d;
  }
  bool get_bool(const std::string& k, bool d = false) const {
    const Value* v = find(k);
    return (v && v->is_bool()) ? v->b : d;
  }
};

// Serialize to compact UTF-8 JSON. Integral numbers print without a decimal point.
std::string dump(const Value& v);

// Parse UTF-8 JSON. Returns nullopt on any malformed input (no partial results).
std::optional<Value> parse(const std::string& text);

}  // namespace ase::json
