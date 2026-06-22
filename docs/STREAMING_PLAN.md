# Streaming completion plan

Status of the client streaming path and the work to take it from "builds + unit-tests clean
cross-platform" to "point it at a host and play with picture **and** sound." Living doc — check items
off as they land. See [docs/BUILD.md](BUILD.md) milestones and the in-repo memory for context.

## Where we are (2026-06-22)
- ✅ Pairing → launch/resume → `LiStartConnection` → FFmpeg HW-decode → SDL present → gamepad capture.
- ✅ Cross-platform parity: Windows (ffmpeg 8, d3d11va) + Linux (ffmpeg 59, vaapi), 7/7 ctest both.
- ✅ Audio: **Opus → SDL** decode wired (Phase 1 done 2026-06-22); 7/7 ctest both platforms.
- ⚠️ Live A/V **never run against a real host** — build/link/test is the ceiling without one.
- ✅ `app` accepts a numeric appid **or** an app name (name→id via `applist`); `client.apps` lists them.
- ✅ Sunshine host control surface backend landed (`host.status`/`enable`/`syncApps`/`listApps`).
- ✅ **Released as v0.2.0** (2026-06-22) — first streaming-capable build; CI green both OSes, win+linux
  assets published. **Bundled into ArcadeLauncher v0.12.0**, which drives it via `client.start`.

## Phase 1 — Audio (Opus → SDL out)   ✅ DONE 2026-06-22
Make the stream not silent. Mirror the `video_renderer` pattern.
- [x] Provision **Opus** dep: vcpkg `opus` (local ✅ + VM 111 launched); apt `libopus-dev` (CT 130/128 ✅).
- [x] CMake: `ASE_LINK_OPUS` → `ASE_HAVE_OPUS`; `find_package(Opus CONFIG)` (Win) / pkg-config (Linux);
      `find_path` adds `.../include/opus` so bare `<opus_multistream.h>` resolves on both.
- [x] `src/client/audio_renderer.{h,cpp}` (ASE_HAVE_OPUS && ASE_HAVE_SDL): `init` →
      `opus_multistream_decoder_create` from the negotiated config + `SDL_OpenAudioDevice` (queue API,
      S16); `decode_and_play` → `opus_multistream_decode` → `SDL_QueueAudio` (moonlight audio thread);
      `cleanup`.
- [x] Wired into `stream_session.cpp`: `AudioRenderer` + file-scope `g_audio`, lifecycle in the
      renderer branch; null sink otherwise.
- [x] Build + ctest **7/7 green on both platforms** (Win opus.dll bundled; Linux links libopus.so.0);
      CI workflows + BUILD.md + IPC.md + release notes updated.
- ⏳ Live verification of actual sound output deferred to Phase 3 (needs a host).

## Phase 2 — App name→id resolution   ✅ DONE 2026-06-22
- [x] `src/net/nv_applist.{h,cpp}` — Qt-free port of moonlight's `NvHTTP::getAppList`: pure
      `parse_app_list` (repeated `<App>`→`{id,title,hdrSupported}`) + `resolve_app_id`
      (numeric passthrough, else case-insensitive title match) + transport-injected `fetch_app_list`;
      NvHttpClient `applist`-over-HTTPS convenience (OpenSSL-gated).
- [x] In `client.start`, if `app` is non-numeric, fetch the app list and resolve title→appid; numeric
      passthrough avoids the round-trip. New `app_not_found` error code.
- [x] `client.apps` now returns the real list (`{id,name,hdrSupported}`) via the pinned HTTPS channel;
      both it and `client.start` share a `connect_paired_host` helper. Skeleton build → empty list.
- [x] `ase_applist_tests` (parse/resolve/fetch vs a simulated transport) — pure, runs in every build.
      **Verified:** skeleton 4/4 ctest, engine-openssl 8/8 ctest, engine-full (moonlight) links.

## Phase 3 — Live A/V bring-up (needs the user's host + GPU)   ◀ NEEDS YOU
Cannot be done in CI. Run together against a homelab Sunshine host.
- [ ] Confirm a paired host (`client.pair`) and a known appid.
- [ ] `client.start` → watch `stream.state` reach `streaming`; confirm a window + a decoded frame.
- [ ] Verify HW decode actually engages (stats `hardware:true`); else debug d3d11va/vaapi fallback.
- [ ] Verify audio plays and gamepad input reaches the host.
- [ ] Capture a `stream.stats` sample (fps/RTT) and note any tuning (packetSize, bitrate, vsync).

## Phase 4 — Sunshine host control surface   ✅ BACKEND LANDED 2026-06-22
Resolved the MSVC/MinGW split by **not linking** Sunshine in-process: the engine drives the bundled
fork's `sunshine[.exe]` as a managed child process (portable, single-toolchain-agnostic).
- [x] `src/host/host_apps.{h,cpp}` — model + (de)serialize the Sunshine `apps.json` the host streams,
      read/write the engine-managed catalog, and the publish diff. Pure (json + std::filesystem).
- [x] `src/host/sunshine_backend.{h,cpp}` — resolve the bundled `sunshine` binary (`$ARCADE_SUNSHINE`
      or beside the engine), and start/stop/running it as a child (CreateProcess / fork+exec).
- [x] `host.status`/`syncApps`/`listApps` are **real in every build** (file-backed catalog);
      `host.enable` starts/stops the child (`not_installed` if no binary bundled). `host_main.cpp`
      delegates to the backend; IPC.md updated.
- [x] `ase_host_apps_tests` (serialize/parse/round-trip/diff + pure binary-resolve/arg helpers).
      **Verified:** skeleton 5/5 ctest (MSVC); Linux build on CT 128.
- [ ] Live host bring-up: build the vendored Sunshine fork, stage `sunshine` beside the engine, flip
      `host.enable {on:true}`, confirm a client can pair + stream from it. **Needs you** (GPU host).
- [ ] Source `host.deviceInfo`/`host.pairAccept` from the running Sunshine (cert fingerprint, PIN).

## Phase 5 — Commit + push + release   ✅ DONE 2026-06-22
- [x] Committed the renderer + audio + host-control work in coherent chunks.
- [x] Pushed; CI built the full streaming engine on both runners.
- [x] Tagged **v0.2.0** → release CI published win-x64 + linux-x64 assets. Bundled into the launcher.

---
**Done:** Phase 1 (audio) + Phase 2 (app name→id) + Phase 4 backend (Sunshine host control surface)
+ Phase 5 (commit/push/**release v0.2.0**, bundled in launcher v0.12.0).
**Blocked on user:** Phase 3 (live A/V test) + Phase 4 live host bring-up — both need a GPU host.
