// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "host/client_trust.h"

#include "ipc/json.h"

namespace ase::host {

using json::Value;

namespace {

// Borrow (creating if absent) the object/array child `key` of `parent`. `parent` is coerced to an
// object; the returned child is coerced to the requested kind. Returns a live reference into the
// tree so the caller can mutate in place.
Value& child_object(Value& parent, const std::string& key) {
  if (!parent.is_object()) parent = Value::object();
  Value& c = parent.obj[key];
  if (!c.is_object()) c = Value::object();
  return c;
}

Value& child_array(Value& parent, const std::string& key) {
  if (!parent.is_object()) parent = Value::object();
  Value& c = parent.obj[key];
  if (!c.is_array()) c = Value::array();
  return c;
}

// The state JSON parsed into a mutable object tree (a fresh object on empty/invalid input).
Value parse_state(const std::string& currentJson) {
  if (currentJson.empty()) return Value::object();
  auto parsed = json::parse(currentJson);
  if (!parsed || !parsed->is_object()) return Value::object();
  return *parsed;
}

}  // namespace

bool has_trusted_client(const std::string& currentJson, const std::string& certPem) {
  if (certPem.empty()) return false;
  Value doc = parse_state(currentJson);
  const Value* root = doc.find("root");
  if (!root) return false;
  const Value* devices = root->find("named_devices");
  if (!devices || !devices->is_array()) return false;
  for (const Value& d : devices->arr) {
    if (d.get_str("cert") == certPem) return true;
  }
  return false;
}

std::string add_trusted_client(const std::string& currentJson, const std::string& name,
                               const std::string& certPem, const std::string& uuid) {
  Value doc = parse_state(currentJson);
  Value& root = child_object(doc, "root");
  Value& devices = child_array(root, "named_devices");

  // Idempotent: never add the same cert twice (re-seeding on every host start is expected).
  for (const Value& d : devices.arr) {
    if (d.get_str("cert") == certPem) return json::dump(doc);
  }

  Value entry = Value::object();
  entry.set("name", Value::string(name));
  entry.set("cert", Value::string(certPem));
  entry.set("uuid", Value::string(uuid));
  // Sunshine persists `enabled` as the string "true"/"false" (ptree put(bool)); match that on-disk
  // form so its get<bool> read is unambiguous across versions.
  entry.set("enabled", Value::string("true"));
  devices.arr.push_back(std::move(entry));
  return json::dump(doc);
}

}  // namespace ase::host
