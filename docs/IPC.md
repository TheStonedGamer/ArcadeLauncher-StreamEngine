# Launcher ↔ Engine IPC Protocol

**Protocol version: 1**

The single contract between the proprietary ArcadeLauncher launcher and this GPL engine. Two
separate programs speaking a documented protocol over a local socket = *mere aggregation*; the
launcher never links the engine. Keep it that way.

## Transport
- **Windows:** named pipe `\\.\pipe\arcade-stream-engine-<token>`.
- **Unix:** stream domain socket `$XDG_RUNTIME_DIR/arcade-stream-engine-<token>.sock`
  (fallback `/tmp/...`).
- The **launcher** creates the pipe/socket (it is the listener) and spawns the engine with
  `--ipc <token>`. The **engine connects** on startup. One control connection per engine
  process. **Never** a TCP port — local IPC only.

## Framing
Each message is a **4-byte little-endian unsigned length** prefix followed by that many bytes
of **UTF-8 JSON** (exactly one JSON object per frame). Max frame size 8 MiB (reject larger).

## Handshake
Immediately on connect, **both** sides send a `hello`:

```json
{ "kind": "hello", "protocolVersion": 1, "engineVersion": "0.1.0" }
```

If the peer's `protocolVersion` differs from what the launcher expects, the launcher closes the
connection and surfaces a clear "engine/launcher version mismatch" error (mirrors the
client↔server major.minor lockstep). After a successful exchange, normal messages flow.

## Message kinds
| kind    | direction          | shape |
|---------|--------------------|-------|
| `req`   | launcher → engine  | `{ "kind":"req", "id":<u64>, "method":"<name>", "params":{…} }` |
| `res`   | engine → launcher  | `{ "kind":"res", "id":<u64>, "ok":true, "result":{…} }` or `{ "kind":"res","id":<u64>,"ok":false,"error":{ "code":"…","message":"…" } }` |
| `event` | engine → launcher  | `{ "kind":"event", "event":"<name>", "data":{…} }` |

`id` is launcher-assigned, monotonic, and echoed verbatim in the matching `res`. Requests may
be answered out of order. Events are unsolicited and carry no `id`.

## Methods — host mode
| method            | params | result |
|-------------------|--------|--------|
| `host.status`     | — | `{ installed, running, configured, gpuCapable, appsCount }` |
| `host.enable`     | `{ on: bool }` | `{ running: bool }` — start/stop hosting, write creds, open firewall |
| `host.syncApps`   | `{ games: [{ id, name, coverPath, launchCmd }] }` | `{ added, removed, updated }` — diff library vs registered Sunshine apps |
| `host.listApps`   | — | `{ apps: [{ gameKey, name, coverRef }] }` — the host's currently registered/streamable games, so the launcher can publish them to the server for the "My PCs" tab (T12k-9) |
| `host.pairAccept` | `{ pin: "NNNN" }` | `{ ok, certFingerprint }` — accept an inbound pairing PIN |
| `host.deviceInfo` | — | `{ deviceId, lanAddr, meshAddr, certFingerprint }` — for gateway registration (T12k-7/8) |

## Methods — client mode
| method          | params | result |
|-----------------|--------|--------|
| `client.hosts`  | — | `{ hosts: [{ name, address, paired, state }] }` |
| `client.apps`   | `{ host }` | `{ apps: [{ name }] }` |
| `client.pair`   | `{ host, pin }` | `{ paired: true, serverCert }` — `serverCert` is the pinned host cert as hex |
| `client.start`  | `{ host, app, settings:{ width,height,fps,bitrateKbps,displayMode,hdr }, embedWindow?:bool }` | `{ nativeWindow? }` — if `embedWindow`, the child window handle (HWND / X11 id / NSView ptr as string) for the launcher to reparent |
| `client.stop`   | — | `{ stopped: true }` |

**`client.pair`** (engine-side, Qt-free NvHTTP port — `src/net/{identity,http_client,nv_pairing,
pairing_crypto,gamestream_xml}.cpp`, OpenSSL build only): fetches `serverinfo` over HTTP to learn
the host's HTTPS port + generation, generates a client identity, then runs the 5-stage GameStream
pairing handshake. Error codes: `bad_params` (missing host/pin), `host_unreachable` (no serverinfo),
`pin_wrong`, `already_pairing`, `pairing_failed`. NOTE: client identity + paired-cert persistence
(moonlight's IdentityManager settings store) is the remaining wiring — a fresh identity is currently
minted per call, so pairings are not yet reused across sessions.

**`client.start` settings validation** (engine-side, `src/client/stream_config.cpp`): the engine
range-checks `settings` before opening the stream and returns `bad_params` (with the offending
field in the message) for anything out of bounds. Omitted fields take defaults
(1920×1080, 60 fps, 20000 kbps, `fullscreen`, hdr off); `settings` itself may be omitted.
Bounds: `width` 256–7680, `height` 144–4320, `fps` 10–240, `bitrateKbps` 500–500000,
`displayMode` ∈ {`windowed`,`fullscreen`,`borderless`}.

## Events (engine → launcher)
| event                   | data |
|-------------------------|------|
| `stream.state`          | `{ phase: "connecting"|"streaming"|"paused"|"ended"|"error", reason? }` |
| `stream.stats`          | `{ fps, bitrateKbps, rttMs, decodeMs, packetLoss }` — drives the in-app HUD |
| `pair.result`           | `{ ok, certFingerprint? }` |
| `host.appsChanged`      | `{ appsCount }` |
| `input.controllerCount` | `{ n }` |

## Error codes (`res.error.code`)
`unsupported_method`, `bad_params`, `not_paired`, `gpu_unsupported`, `engine_busy`,
`host_unreachable`, `pairing_failed`, `not_installed`, `elevation_required`, `internal`.

## Versioning rule
Any change that breaks launcher↔engine compatibility (method/param/result/event shape,
handshake) **bumps `kProtocolVersion`** (in `src/ipc/ipc.h`, cross-checked by the CMake
`ASE_IPC_PROTOCOL_VERSION` macro). Additive, backward-compatible fields do not.
