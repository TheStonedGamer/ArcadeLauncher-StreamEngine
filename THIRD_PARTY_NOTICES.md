# Third-Party Notices

ArcadeLauncher Stream Engine is a combined/derivative work. It incorporates and modifies the
following GPL-3.0 projects, vendored as git submodules under `vendor/` (forks of upstream).
Their copyright notices and licenses are retained in their respective subtrees and apply to
this work as a whole, which is therefore distributed under **GPL-3.0-or-later**.

## Sunshine  (host mode)
- Upstream: https://github.com/LizardByte/Sunshine
- Copyright: © LizardByte and Sunshine contributors
- License: GPL-3.0-only
- Vendored at: `vendor/sunshine` (fork)
- Role here: capture, hardware encode, GameStream server, pairing, virtual-gamepad injection.

## Moonlight  (client mode)
- Upstream: https://github.com/moonlight-stream/moonlight-qt
  (with submodule https://github.com/moonlight-stream/moonlight-common-c)
- Copyright: © Cameron Gutman and Moonlight Game Streaming Project contributors
- License: GPL-3.0-only
- Vendored at: `vendor/moonlight` (fork)
- Role here: connection, video/audio depacketization + FEC, decode/render, controller capture.

## Transitive dependencies
Both projects pull in further libraries (FFmpeg, SDL2, libav*, Opus, openssl/mbedtls, etc.).
Their individual licenses (LGPL/BSD/MIT/etc.) and notices are carried within the vendored
subtrees and their build outputs. This file is updated as concrete dependencies are pinned
during the build bring-up.

## Compliance checklist (distribution)
- [x] Full corresponding source of this engine is published (this repository, public).
- [x] GPL-3.0 `LICENSE` included at the repo root.
- [x] Upstream copyright + license notices retained in `vendor/` subtrees.
- [ ] The launcher installer that bundles this engine ships, alongside it, the engine's
      LICENSE and a written offer / link to this source repository (aggregation).
- [ ] Per-dependency notices for the final pinned transitive set enumerated above.
