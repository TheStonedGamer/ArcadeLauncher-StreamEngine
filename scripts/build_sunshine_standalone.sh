#!/usr/bin/env bash
# Milestone 2 (host half): build the vendored Sunshine fork STANDALONE under MSYS2 UCRT64
# to prove the host toolchain + deps before linking it into the engine (ASE_LINK_SUNSHINE).
# Run from the "MSYS2 UCRT64" shell:  bash scripts/build_sunshine_standalone.sh
# Does NOT touch the engine build. See docs/BUILD.md "Bring-up milestones".
set -uo pipefail

REPO="/c/Users/BrianTheMint/source/repos/ArcadeLauncher-StreamEngine"
SRC="$REPO/vendor/sunshine"
BUILD="$REPO/build/sunshine-standalone"
LOG="$REPO/build/sunshine-build.log"
mkdir -p "$REPO/build"
# Mirror everything to a logfile the host side can tail.
exec > >(tee "$LOG") 2>&1

echo "=================================================================="
echo " Sunshine standalone build  ($(uname -s), MSYSTEM=${MSYSTEM:-?})"
echo "=================================================================="

if [[ "${MSYSTEM:-}" != "UCRT64" ]]; then
  echo "!! Not in the UCRT64 environment (MSYSTEM=${MSYSTEM:-unset}). Start 'MSYS2 UCRT64'." >&2
  exit 2
fi

TOOLCHAIN="ucrt-x86_64"
deps=(
  git
  "mingw-w64-${TOOLCHAIN}-toolchain"
  "mingw-w64-${TOOLCHAIN}-cmake"
  "mingw-w64-${TOOLCHAIN}-ninja"
  "mingw-w64-${TOOLCHAIN}-boost"
  "mingw-w64-${TOOLCHAIN}-cppwinrt"
  "mingw-w64-${TOOLCHAIN}-curl-winssl"
  "mingw-w64-${TOOLCHAIN}-miniupnpc"
  "mingw-w64-${TOOLCHAIN}-onevpl"
  "mingw-w64-${TOOLCHAIN}-openssl"
  "mingw-w64-${TOOLCHAIN}-opus"
  "mingw-w64-${TOOLCHAIN}-MinHook"
  "mingw-w64-${TOOLCHAIN}-nodejs"
  "mingw-w64-${TOOLCHAIN}-nsis"
)

echo ">> [1/3] refresh package db + install deps (this is the long part)"
# Refresh DB only (avoid a full -Syu that can restart the shell mid-script on a fresh install).
pacman -Sy --noconfirm
pacman -S  --needed --noconfirm "${deps[@]}" || { echo "!! dependency install failed" >&2; exit 1; }

echo ">> [2/3] configure  ($BUILD)"
# BUILD_DOCS=OFF: we don't need Sunshine's Doxygen docs for the host backend (avoids the
# doxygen+graphviz dep). The engine drives the host over IPC; docs are irrelevant here.
cmake -B "$BUILD" -G Ninja -S "$SRC" -DCMAKE_BUILD_TYPE=Release -DBUILD_DOCS=OFF \
  || { echo "!! cmake configure failed" >&2; exit 1; }

echo ">> [3/3] build"
cmake --build "$BUILD" \
  || { echo "!! build failed" >&2; exit 1; }

echo ""
echo "=================================================================="
echo " OK - Sunshine builds standalone under UCRT64. Host deps proven."
ls -la "$BUILD"/sunshine.exe 2>/dev/null || echo " (sunshine.exe path differs - check $BUILD)"
echo "=================================================================="
