// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#include "ipc/server.h"

#include <string>

#include "ipc/frame.h"
#include "ipc/ipc.h"

#ifndef ENGINE_VERSION
#define ENGINE_VERSION "0.0.0-dev"
#endif

namespace ase::ipc {

bool Server::handshake(std::string& err) {
  json::Value hello = json::Value::object();
  hello.set("kind", json::Value::string(kind::kHello));
  hello.set("protocolVersion", json::Value::number(kProtocolVersion));
  hello.set("engineVersion", json::Value::string(ENGINE_VERSION));
  if (!write_frame(t_, json::dump(hello), err)) return false;

  std::string raw;
  if (!read_frame(t_, raw, err)) {
    if (err.empty()) err = "connection closed before handshake";
    return false;
  }
  auto peer = json::parse(raw);
  if (!peer || !peer->is_object()) { err = "malformed handshake frame"; return false; }
  if (peer->get_str("kind") != kind::kHello) { err = "expected hello, got '" + peer->get_str("kind") + "'"; return false; }
  const int peer_ver = static_cast<int>(peer->get_num("protocolVersion", -1));
  if (peer_ver != kProtocolVersion) {
    err = "protocol version mismatch (engine " + std::to_string(kProtocolVersion) +
          ", launcher " + std::to_string(peer_ver) + ")";
    return false;
  }
  return true;
}

void Server::emit(const std::string& event, const json::Value& data) {
  json::Value ev = json::Value::object();
  ev.set("kind", json::Value::string(kind::kEvent));
  ev.set("event", json::Value::string(event));
  ev.set("data", data);
  std::string err;
  write_frame(t_, json::dump(ev), err);  // best-effort; events are fire-and-forget
}

bool Server::run(std::string& err) {
  for (;;) {
    std::string raw;
    if (!read_frame(t_, raw, err)) return err.empty();  // clean EOF -> success

    auto msg = json::parse(raw);
    if (!msg || !msg->is_object()) continue;  // ignore unparseable frames, stay connected

    const std::string k = msg->get_str("kind");
    if (k != kind::kReq) continue;  // hello (post-handshake) / stray events: ignore

    const uint64_t id = static_cast<uint64_t>(msg->get_num("id", 0));
    const std::string method = msg->get_str("method");
    const json::Value* params = msg->find("params");
    const json::Value empty_params = json::Value::object();

    std::string ecode, emsg;
    json::Value result;
    auto it = handlers_.find(method);
    if (it == handlers_.end()) {
      ecode = "unsupported_method";
      emsg = "unknown method: " + method;
    } else {
      result = it->second(params ? *params : empty_params, ecode, emsg);
    }

    json::Value res = json::Value::object();
    res.set("kind", json::Value::string(kind::kRes));
    res.set("id", json::Value::number(static_cast<double>(id)));
    if (ecode.empty()) {
      res.set("ok", json::Value::boolean(true));
      res.set("result", std::move(result));
    } else {
      res.set("ok", json::Value::boolean(false));
      json::Value e = json::Value::object();
      e.set("code", json::Value::string(ecode));
      e.set("message", json::Value::string(emsg));
      res.set("error", std::move(e));
    }
    if (!write_frame(t_, json::dump(res), err)) return false;
  }
}

}  // namespace ase::ipc
