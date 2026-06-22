# Architecture

One GPL-3.0 binary, two modes, driven by the proprietary launcher over IPC ([`IPC.md`](IPC.md)).

```
                     proprietary launcher (Tauri/Rust)
                                  |
                     IPC (local pipe/socket, JSON)        <-- the license boundary
                                  |
        +---------------------------------------------------+
        |            arcade-stream-engine (GPL-3.0)         |
        |   src/main.cpp  -> dispatch host | stream         |
        |                                                   |
        |  host mode                 client mode            |
        |  (vendor/sunshine)         (vendor/moonlight)     |
        |  capture/encode/serve      connect/decode/render  |
        |  pair / gamepad inject     controller capture     |
        |                                                   |
        |  shared/: config, logging, protocol consts, ipc/  |
        +---------------------------------------------------+
```

## Why a separate process (not a library)
The launcher must stay proprietary; the engine must be GPL (it incorporates Sunshine +
Moonlight). Linking them would make the whole launcher a GPL derivative. Running the engine as
a **separate process** behind a documented IPC protocol is aggregation — each keeps its own
license. See `README.md` → "License".

## Build bring-up order (matches the engine bring-up plan)
1. **CLI + IPC skeleton** (this scaffold): mode dispatch, `--version`, `hello` handshake. ✅
2. **Vendor the forks** (`scripts/vendor.sh`) and get each building standalone on the runners.
3. **IPC server**: implement framing + the `hello` exchange + `host.status` / `client.start`.
4. **Host mode**: drive the Sunshine fork (status/enable/syncApps/pairAccept/deviceInfo).
5. **Client mode**: drive the Moonlight fork (pair/start/stop) + child-window handle return.
6. **Reparent + HUD** on the launcher side; controller pass-through end-to-end.

## In-app rendering
Client mode renders into its **own borderless child window** and returns the native handle in
`client.start`. The launcher reparents it (`SetParent` / XEmbed / `addSubview`) into the stream
view and overlays a HUD fed by `stream.stats`. Decode-here/render-there frame-passing is a
later option; reparent is the proven first path.

## Build & CI
Builds on the same native runners as the launcher (Windows VM 111 / Linux leg). Heavy C/C++
surface: NVENC/AMF/QSV, FFmpeg, SDL2. The launcher's `release.yml` pulls the engine release
artifact and bundles it as an `externalBin` sidecar. Engine versioned independently (`VERSION`).
