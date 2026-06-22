#!/usr/bin/env bash
# One-shot Linux build of the ArcadeLauncher Stream Engine on a fresh Debian CT.
# Mirrors the repo's build.yml `build-linux` job: moonlight-common-c linked
# (ASE_LINK_MOONLIGHT=ON, which auto-enables the OpenSSL pairing crypto), Sunshine
# skipped. Produces a pairing-capable single binary — same capability surface as
# the Windows v0.1.0 asset (video/host handlers are stubs on every platform today).
set -euo pipefail

REPO_URL="https://github.com/TheStonedGamer/ArcadeLauncher-StreamEngine.git"
SRC="${SRC:-/root/ArcadeLauncher-StreamEngine}"
REF="${REF:-main}"

echo "==== [1/6] apt deps ===="
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq cmake ninja-build build-essential pkg-config libssl-dev git >/dev/null
echo "cmake: $(cmake --version | head -1) | ninja: $(ninja --version) | g++: $(g++ --dumpversion)"

echo "==== [2/6] source ($REF) ===="
if [ -d "$SRC/.git" ]; then
  git -C "$SRC" fetch --tags --quiet origin
  git -C "$SRC" checkout --quiet "$REF"
  git -C "$SRC" pull --quiet --ff-only origin "$REF" || true
else
  git clone --quiet "$REPO_URL" "$SRC"
  git -C "$SRC" checkout --quiet "$REF"
fi
cd "$SRC"
echo "HEAD: $(git rev-parse --short HEAD) | VERSION: $(cat VERSION)"

echo "==== [3/6] submodules (moonlight + moonlight-common-c, skip Sunshine) ===="
git submodule update --init vendor/moonlight
git -C vendor/moonlight submodule update --init --recursive moonlight-common-c/moonlight-common-c

echo "==== [4/6] configure ===="
rm -rf build-linux
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DASE_LINK_MOONLIGHT=ON

echo "==== [5/6] build ===="
cmake --build build-linux

echo "==== [6/6] test + verify ===="
ctest --test-dir build-linux --output-on-failure
BIN="$SRC/build-linux/arcade-stream-engine"
echo "---- binary ----"
file "$BIN"
ls -la "$BIN"
echo "---- shared lib deps ----"
ldd "$BIN" || true
echo "---- run (no-arg usage) ----"
"$BIN" --version 2>&1 || "$BIN" 2>&1 | head -5 || true
echo "==== DONE: $BIN ===="
