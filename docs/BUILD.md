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
    as its own submodule under `app/moonlight-common-c`); we `add_subdirectory` that lib path.
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
1. **Forks build standalone** on each runner (deps proven). ← step 2
2. `moonlight-common-c` links into the engine; `client.start` opens a connection.
3. Sunshine host control surface driven over IPC; `host.status`/`enable`/`syncApps`.
4. Engine renderer (SDL2 + HW decode) → child window handle returned for reparent.
5. Controller pass-through end-to-end (capture → control stream → ViGEm/uinput inject).
