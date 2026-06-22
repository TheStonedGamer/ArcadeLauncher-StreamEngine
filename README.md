# ArcadeLauncher Stream Engine

The game-streaming engine for [ArcadeLauncher](https://github.com/TheStonedGamer/ArcadeLauncher-Server).
A single binary that does **both ends** of a stream — host and client — by forking and
combining two upstream GameStream projects:

- **Host mode** (`arcade-stream-engine host`) — forks [**Sunshine**](https://github.com/LizardByte/Sunshine):
  desktop/game capture, hardware encode (NVENC/AMF/QSV/VideoToolbox), the GameStream server
  (RTSP + control + RTP A/V), pairing, and host-side virtual-gamepad injection.
- **Client mode** (`arcade-stream-engine stream`) — forks [**Moonlight**](https://github.com/moonlight-stream/moonlight-qt)
  (`moonlight-qt` + `moonlight-common-c`): connection, decode/render, and controller capture.

Controllers are handled **entirely inside the engine** (capture client-side → control stream
→ inject host-side, including rumble).

## License — GPL-3.0, and why this is a separate program

This engine is licensed **GPL-3.0-or-later** because it incorporates GPL-3.0 code from
Sunshine and Moonlight. It is published as a **standalone, open-source program**.

The proprietary **ArcadeLauncher launcher** does **not** link this engine. It runs the engine
as a **separate process** and communicates over an arm's-length IPC protocol
(see [`docs/IPC.md`](docs/IPC.md)). Two separate programs communicating over a documented
protocol is *mere aggregation* under the GPL — so the launcher remains proprietary while this
engine remains GPL.

> **Do not break the seam.** Nothing in the launcher may statically link, dynamically link,
> `dlopen`, FFI-bind, or share in-process data structures with this engine. The only contract
> between them is the IPC protocol. (This is not legal advice.)

Every modification made to this engine — including to the vendored Sunshine/Moonlight forks —
is also GPL-3.0 and must be published. See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Layout

```
src/
  main.cpp            CLI entry — dispatches `host` / `stream` / `--version`
  host/               host-mode glue around the Sunshine fork
  client/             client-mode glue around the Moonlight fork
  ipc/                the launcher<->engine IPC server (protocol in docs/IPC.md)
  shared/             code common to both modes (config, logging, protocol consts)
vendor/               git submodules: the Sunshine + Moonlight forks (see vendor/README.md)
docs/                 IPC.md (protocol), ARCHITECTURE.md
scripts/              vendor.sh / vendor.ps1 — add the fork submodules
cmake/                toolchain / module helpers
```

## Build (skeleton)

```sh
# 1. Vendor the forks (one-time; edit the URLs to point at YOUR forks first):
./scripts/vendor.sh          # or scripts\vendor.ps1 on Windows

# 2. Configure + build:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The current tree builds a working **CLI + IPC skeleton** (mode dispatch, `--version`, the
protocol handshake). The Sunshine/Moonlight forks are wired in as the host/client
implementations land — see `docs/ARCHITECTURE.md`.

## Versioning

`VERSION` is the single source of truth (mirrors the server's convention). The engine is
versioned **independently** of the launcher; the launcher records which engine version it
ships in its installer.
