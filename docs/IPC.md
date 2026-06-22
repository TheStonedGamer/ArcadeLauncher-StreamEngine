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
| `host.enable`     | `{ on: bool }` | `{ running: bool }` — start/stop the bundled Sunshine child |
| `host.syncApps`   | `{ games: [{ name, cmd, image?, workingDir?, elevated?, autoDetach?, waitAll?, exitTimeout? }] }` | `{ added, removed, updated, total }` — publish the library into the host catalog; diff vs what was registered |
| `host.listApps`   | — | `{ apps: [{ name, cmd, imagePath }] }` — the host's currently registered/streamable games, so the launcher can publish them to the server for the "My PCs" tab (T12k-9) |
| `host.pairAccept` | `{ pin: "NNNN" }` | `unsupported` — Sunshine PIN pairing is confirmed via its own web UI; engine-side accept not wired yet |
| `host.deviceInfo` | — | `{ deviceId, lanAddr, meshAddr, certFingerprint }` — for gateway registration (T12k-7/8); fields empty until sourced from the running Sunshine |

## Methods — client mode
| method          | params | result |
|-----------------|--------|--------|
| `client.hosts`  | — | `{ hosts: [{ name, address, paired, state }] }` |
| `client.apps`   | `{ host }` | `{ apps: [{ id, name, hdrSupported }] }` — the host's GameStream app list (live `applist` over the pinned HTTPS channel) |
| `client.pair`   | `{ host, pin }` | `{ paired: true, serverCert }` — `serverCert` is the pinned host cert as hex |
| `client.start`  | `{ host, app, settings:{ width,height,fps,bitrateKbps,displayMode,hdr }, embedWindow?:bool }` | `{ started: true }`. `app` is either a numeric GameStream appid or an app **name** (resolved case-insensitively against the host's app list). Connection proceeds async — watch `stream.state` events; the reparent handle arrives in the `window`-phase event, not the result. |
| `client.stop`   | — | `{ stopped: true }` |

**`client.pair`** (engine-side, Qt-free NvHTTP port — `src/net/{identity,http_client,nv_pairing,
pairing_crypto,gamestream_xml}.cpp`, OpenSSL build only): fetches `serverinfo` over HTTP to learn
the host's HTTPS port + generation, generates a client identity, then runs the 5-stage GameStream
pairing handshake. Error codes: `bad_params` (missing host/pin), `host_unreachable` (no serverinfo),
`pin_wrong`, `already_pairing`, `pairing_failed`. The client identity (cert/key/uniqueid) and each
paired host's pinned cert are persisted in the engine store (`src/net/identity_store.cpp`, the
Qt-free port of moonlight's IdentityManager) under a per-user data dir, so the identity is generated
once and a host stays paired across engine restarts. `client.hosts` returns that persisted registry;
`client.start` looks the host up there (`not_paired` if absent).

**`client.apps` + app name resolution** (engine-side, `src/net/nv_applist.cpp`, OpenSSL build only —
the Qt-free port of moonlight's `NvHTTP::getAppList`): `client.apps` fetches the host's GameStream
`applist` over the pinned-cert HTTPS channel and returns each app as `{ id, name, hdrSupported }`.
`client.start` accepts either a numeric appid (passed through with no extra round-trip) or an app
**name** in `app`; a name is matched case-insensitively against that same app list and resolved to
its appid. Error codes: `bad_params` (missing host/app), `not_paired`, `host_unreachable` (no
serverinfo/applist), `app_not_found` (no app by that name on the host). The skeleton/no-OpenSSL build
returns an empty list from `client.apps` (it can't reach a host).

**`client.start` settings validation** (engine-side, `src/client/stream_config.cpp`): the engine
range-checks `settings` before opening the stream and returns `bad_params` (with the offending
field in the message) for anything out of bounds. Omitted fields take defaults
(1920×1080, 60 fps, 20000 kbps, `fullscreen`, hdr off); `settings` itself may be omitted.
Bounds: `width` 256–7680, `height` 144–4320, `fps` 10–240, `bitrateKbps` 500–500000,
`displayMode` ∈ {`windowed`,`fullscreen`,`borderless`}.

**`client.start` live connection** (engine-side, `src/net/nv_launch.cpp` + `src/client/stream_session.cpp`,
moonlight+OpenSSL build): after validating settings and confirming the host is paired, the engine
fetches `serverinfo` (HTTP→HTTPS over the pinned cert), negotiates a remote-input AES key, issues a
GameStream `launch`/`resume` (resume if `<currentgame>`≠0) to start the app and obtain `sessionUrl0`,
then runs `LiStartConnection` on a worker thread. Progress is reported via `stream.state` events
(`connecting`→`window`→`streaming`, or `error`/`ended`).

When SDL2 + FFmpeg are linked (`ASE_LINK_SDL` / `ASE_LINK_FFMPEG`), the worker also owns the video
pipeline (`src/client/stream_window.cpp` + `src/client/video_renderer.cpp`): it creates a borderless
SDL2 window (emitting its native handle in the `window` event for reparenting), decodes H.264 via
FFmpeg with hardware acceleration (`d3d11va` on Windows, `vaapi` on Linux; software fallback) on
moonlight's submit thread, presents to an SDL texture on the worker's event/present loop, captures
gamepads (→ `LiSendMultiControllerEvent`), and emits `stream.stats` ~1×/s. `embedWindow:true` keeps
the window hidden for the launcher to reparent; otherwise the engine shows it standalone. When Opus is
also linked (`ASE_LINK_OPUS`), audio is decoded (`opus_multistream`) and queued to an SDL audio device
(`src/client/audio_renderer.cpp`). Built without those deps, the video/audio sinks are **null** (frames
accepted and discarded) so the connection still works headless. `client.stop`
interrupts/tears down the connection. Error codes: `bad_params`, `not_paired`, `host_unreachable`,
`engine_busy` (already streaming), `internal` (launch failed / no moonlight linked).

**Host mode** (engine-side, `src/host/host_apps.cpp` + `src/host/sunshine_backend.cpp`): the engine
serves this PC by driving the **bundled Sunshine fork** (`vendor/sunshine`) as a managed child
process — it does *not* link Sunshine in-process (Sunshine is a full server with its own main/threads
and, on Windows, builds only under MinGW while the engine is MSVC). The engine ships `sunshine[.exe]`
beside itself (override with `$ARCADE_SUNSHINE`) and points it at an engine-managed `apps.json`
(default `<data-dir>/sunshine_apps.json`). `host.syncApps` writes that catalog from the launcher's
library and reports the diff; `host.listApps` reads it back; `host.status` reports `installed` (binary
bundled), `running` (child up), `configured` (catalog non-empty), and `appsCount`. Because the catalog
is plain file I/O, `syncApps`/`listApps`/`status` are functional in **every** build; `host.enable`
additionally starts/stops the child and returns `not_installed` when no Sunshine binary is bundled.
Error codes: `not_installed`, `internal`, `unsupported` (`host.pairAccept`).

## Events (engine → launcher)
| event                   | data |
|-------------------------|------|
| `stream.state`          | `{ phase: "connecting"|"window"|"streaming"|"ended"|"error", reason?, nativeWindow? }` — `nativeWindow` (HWND / X11 id as a decimal string) is present only on the `window` phase, emitted once the borderless render window exists so the launcher can reparent it |
| `stream.stats`          | `{ framesDecoded, framesPresented, decodeErrors, hardware, width, height, rttMs, rttVarMs }` — emitted ~1×/s while streaming; drives the in-app HUD |
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
