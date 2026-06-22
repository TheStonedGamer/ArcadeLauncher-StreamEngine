# Build & Dependencies

The engine is one CMake project (`CMakeLists.txt`) that builds a single binary. The current
tree builds the **CLI/IPC skeleton with no external dependencies**. The heavy dependencies
below only apply once the Sunshine/Moonlight forks are vendored and wired in.

> Upstream build docs are the source of truth for the forks' exact deps; this file lists the
> high-level set + the **engine-specific additions**. Verify against each fork's README.

## Toolchain (all platforms)
- **CMake ≥ 3.21**, a **C++17** compiler.
- Windows: MSVC (VS 2022 / Build Tools) — verified building locally with MSVC 19.44.
- Linux: GCC or Clang + Ninja.

## Fork integration strategy (important)
- **Client mode** links **`moonlight-common-c`** — the clean, CMake-friendly GameStream client
  *protocol* core (connection, control stream, A/V depacketization, FEC, pairing). The full
  `moonlight-qt` app is **qmake**-based and Qt-heavy; we do **not** swallow it. The engine owns
  a minimal renderer + input (SDL2 + platform HW decode), using `moonlight-qt` as reference.
  → vendored at `vendor/moonlight` (fork of `moonlight-qt`, which carries `moonlight-common-c`
    as its own submodule at `moonlight-common-c/moonlight-common-c`); we `add_subdirectory`
    that lib path (verified against the fork's `.gitmodules`, 2026-06-22).
- **Host mode** builds on the **Sunshine** fork (CMake). First milestone: build the fork
  **standalone** on each runner to prove the toolchain/deps, then carve out the host control
  surface the engine drives over IPC.

## Client deps (`moonlight-common-c` + engine renderer)
| | Windows | Linux |
|---|---|---|
| FFmpeg (libav* — HW decode) | `choco install ffmpeg` or vcpkg `ffmpeg` | `libavcodec-dev libavutil-dev libavformat-dev` |
| SDL2 (window/input/audio) | vcpkg `sdl2` | `libsdl2-dev` |
| OpenSSL (pairing crypto) | vcpkg `openssl` | `libssl-dev` |
| Opus (audio) | vcpkg `opus` | `libopus-dev` |
| Controller | SDL2 game controller (built in) | SDL2 + `libudev-dev` |

## Host deps (Sunshine fork)
| | Windows | Linux |
|---|---|---|
| Boost | vcpkg `boost` | `libboost-all-dev` |
| FFmpeg / OpenSSL / Opus | as above | as above |
| Capture | Desktop Duplication API + Windows SDK | KMS/X11/Wayland grab, `libdrm`, `libcap`, etc. |
| HW encode SDKs | **NVENC** (Video Codec SDK), **AMF**, **QSV** | NVENC / VAAPI |
| Virtual gamepad | **ViGEmBus** driver (runtime) | `uinput` (kernel) |
| UPnP (T12k-8 fallback) | miniupnpc | `libminiupnpc-dev` |

> NVENC/AMF/QSV SDK headers are licensed by their vendors; install per their terms on the
> runner. The runtime ViGEmBus driver is a host-machine prerequisite, not a build dep.

## Build
```sh
# once forks are vendored (scripts/vendor.sh):
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release      # Windows: add -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## CI
`.github/workflows/build.yml` builds on the launcher's self-hosted runners
(`arcade-win10` / `prox-pve`). The fork dep-install steps are present but commented until the
forks are vendored. Runners must have CMake ≥ 3.21.

## Bring-up milestones (step 2 → 5)
0. **IPC server done (fork-independent).** ✅ JSON + length-prefixed framing + hello handshake
   + req/res/event dispatch (`src/ipc/`), with host.*/client.* stub handlers wired in both
   modes. Covered by `ase_tests` (CTest) over an in-memory transport, plus a real named-pipe
   round-trip via `scripts/ipc_smoke.ps1`. The launcher can develop against this wire now.
1. **Forks build standalone** on each runner (deps proven).
   - **Client core ✅ (2026-06-22):** `moonlight-common-c` builds standalone on the Windows
     runner via `scripts/build_moonlight_standalone.ps1` — sole external dep is **OpenSSL**
     (vcpkg `openssl:x64-windows`, 3.6.3); `enet`/`nanors` are bundled. Verified with VS 2022
     + CMake 3.31.
   - **Host (Sunshine) ✅ (2026-06-22):** Sunshine builds standalone under **MSYS2 UCRT64**
     via `scripts/build_sunshine_standalone.sh` → `sunshine.exe` (348/348 targets incl. tests).
     Deps proven: gcc 16 (UCRT64), Boost, OpenSSL, Opus, oneVPL (`libvpl`), miniupnpc, cppwinrt,
     MinHook, nodejs (web UI), nsis; FFmpeg is auto-downloaded prebuilt by Sunshine's CMake.
     Build with `-DBUILD_DOCS=OFF` (skips the Doxygen+graphviz doc dep — irrelevant to the host
     backend). NOTE: must be the **UCRT64** shell, not MSVC/vcpkg — Sunshine is MinGW-only on
     Windows. If a prior `pacman` was interrupted, `scripts/repair_msys_cmake.sh` fixes a
     half-installed cmake (missing Modules / corrupt local DB `desc`).
2. `moonlight-common-c` links into the engine; `client.start` opens a connection.
   - **Link wiring ✅ (2026-06-22):** `-DASE_LINK_MOONLIGHT=ON` (with the vcpkg toolchain)
     builds `arcade-stream-engine.exe` with the fork embedded **statically** and `ase_tests`
     green. NOTE: moonlight-common-c defaults to a DLL; the engine forces `BUILD_SHARED_LIBS
     OFF` for that subdir so MSVC gets a linkable static lib (a no-export DLL emits no import
     `.lib` → LNK1181). `client.start` connection impl in progress (below).
   - **client.start settings validation ✅ (2026-06-22):** engine-side range-check of the stream
     `settings` (`src/client/stream_config.cpp`, `ase_client_tests`); runtime moonlight linkage
     proof via `LiGetStageName` at stream startup.
   - **NvHTTP port ✅ (complete):** GameStream pairing/serverinfo (NvHTTP) is **not** in
     moonlight-common-c — it lives in moonlight-qt (Qt-based). Ported **Qt-free** onto the OpenSSL
     we already link, keeping the single-binary design. Layered as: parse → crypto → TLS client →
     pairing state machine, then wired into the `client.pair` IPC method.
       - **Parse layer ✅:** `src/net/gamestream_xml.{h,cpp}` (Qt-free port of NvHTTP's
         `getXmlString`/`getXmlStringFromHex`/status) + hex helpers; `ase_net_tests`.
       - **Crypto layer ✅:** `src/net/pairing_crypto.{h,cpp}` (Qt-free port of
         `nvpairingmanager.cpp` crypto: AES-128-ECB no-pad, SHA-256/1, `derive_aes_key`
         salt+PIN→key, X509 signature extract, RSA sign/`EVP_DigestVerify`). OpenSSL-only, so it
         is gated behind a new `ASE_WITH_OPENSSL` option (auto-ON with `ASE_LINK_MOONLIGHT`; can be
         forced on alone against a standalone OpenSSL). KAT-tested (FIPS-180 SHA, FIPS-197 AES) +
         runtime-keypair sign/verify in `ase_crypto_tests`.
       - **TLS HTTP client ✅:** `src/net/http_client.{h,cpp}` (winsock/POSIX + OpenSSL): mutual
         TLS presenting the client cert and **pinning** the self-signed server cert (`X509_cmp`),
         plus pure, unit-tested request-target building and HTTP/chunked response parsing.
       - **Identity generator ✅:** `src/net/identity.{h,cpp}` — RSA-2048 self-signed
         CN="NVIDIA GameStream Client" cert/key + unique-id (port of `identitymanager.cpp`).
       - **Pairing state machine ✅:** `src/net/nv_pairing.{h,cpp}` — the 5-stage `pair()` with an
         **injected transport** so it is tested end-to-end against an in-process simulated
         GameStream server (real host-side crypto, no sockets): PAIRED / PinWrong /
         AlreadyInProgress / transport-failure all covered in `ase_pairing_tests`.
       - **Wired up:** `client.pair {host, pin}` runs serverinfo discovery + the handshake and
         returns the pinned server cert. Remaining: client-identity + paired-cert **persistence**
         (port of `identitymanager.cpp`'s settings store) so pairings survive across sessions.
   - **Live connection ✅ (2026-06-22):** `src/net/nv_launch.{h,cpp}` (launch/resume → `sessionUrl0`,
     KAT-tested) + `src/client/stream_session.{h,cpp}` drive `LiStartConnection` on a worker thread.
3. **Engine renderer ✅ (2026-06-22):** opt-in behind `-DASE_LINK_SDL=ON -DASE_LINK_FFMPEG=ON`.
   - `src/client/stream_window.{h,cpp}` — borderless SDL2 window, native handle (HWND/X11) emitted
     for reparent, SDL game-controller capture → `LiSendMultiControllerEvent`.
   - `src/client/video_renderer.{h,cpp}` — FFmpeg H.264 decode with HW accel (`d3d11va` on Windows,
     `vaapi` on Linux; software fallback), present to an SDL texture (NV12 / IYUV). Decode runs on
     moonlight's submit thread, present on the session worker; `stream.stats` HUD feed.
   - `src/client/audio_renderer.{h,cpp}` — Opus (`opus_multistream`) decode → SDL audio queue; opt-in
     behind `-DASE_LINK_OPUS=ON` (rides with the renderer build, needs SDL for output).
   - Deps: Windows via vcpkg (`sdl2`, `ffmpeg[avcodec,nvcodec]`, `opus`); Linux via apt (`libsdl2-dev`,
     `libavcodec-dev libavutil-dev libswscale-dev libopus-dev`). Both runners provisioned 2026-06-22;
     CI builds the full streaming engine. Without the flags the engine still builds + connects headless.
   - ⚠️ Live A/V not yet validated against a real host (no GameStream host in CI); builds + links
     green on both platforms (see [STREAMING_PLAN.md](STREAMING_PLAN.md) for the live bring-up plan).
4. Sunshine host control surface driven over IPC; `host.status`/`enable`/`syncApps`.
5. Controller pass-through end-to-end is wired on the client side (capture → control stream); host
   inject (ViGEm/uinput) is the Sunshine-side follow-up.
