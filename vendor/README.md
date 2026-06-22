# vendor/ — the Sunshine + Moonlight forks

The engine's host and client modes are built from **forks** of two upstream GPL-3.0 projects,
added here as **git submodules** so this repo carries exact, pinned source for GPL compliance
and reproducible builds.

| Path | Fork of | Role |
|------|---------|------|
| `vendor/sunshine`  | `LizardByte/Sunshine`              | host mode (capture/encode/server/pairing/gamepad inject) |
| `vendor/moonlight` | `moonlight-stream/moonlight-qt`    | client mode (connect/decode/render/controller capture)   |

`moonlight-common-c` comes in transitively as a submodule **of** `moonlight-qt`.

## One-time setup

1. **Fork upstream on GitHub** into your org (e.g. `TheStonedGamer/Sunshine`,
   `TheStonedGamer/moonlight-qt`). Forking — not vendoring upstream directly — is what lets
   you commit engine-specific changes while keeping them GPL and rebaseable on upstream.
2. Add them as submodules (edit the URLs to your forks first):

   ```sh
   SUNSHINE_FORK=https://github.com/<you>/Sunshine.git \
   MOONLIGHT_FORK=https://github.com/<you>/moonlight-qt.git \
   ./scripts/vendor.sh
   ```

   (Windows: `scripts\vendor.ps1` with `$env:SUNSHINE_FORK` / `$env:MOONLIGHT_FORK`.)

3. **Pin** to known-good commits and commit the submodule pointers:

   ```sh
   git -C vendor/sunshine  checkout <sha> && git add vendor/sunshine
   git -C vendor/moonlight checkout <sha> && git add vendor/moonlight
   git commit -m "Vendor Sunshine + Moonlight forks (pinned)"
   ```

## Rebasing on upstream (security fixes)

Periodically pull upstream into your fork, resolve against your engine patches, re-pin the
submodule here, and rebuild. Track engine-specific patches as commits on top of a clearly
labelled upstream base so the delta stays auditable.
