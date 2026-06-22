#!/usr/bin/env bash
# One-off repair: a previously interrupted pacman left mingw-w64-ucrt-x86_64-cmake
# half-extracted (missing CMake Modules + corrupt local DB desc). Force a clean
# reinstall so its module files + DB entry are restored. Safe to re-run.
set -uo pipefail

# The interrupted pacman left a corrupt local DB entry (missing 'desc'), so pacman
# refuses to reinstall over it ("invalid or corrupted package"). Drop the broken DB
# entry so cmake looks not-installed, then do a clean install.
DBDIR=/var/lib/pacman/local
for d in "$DBDIR"/mingw-w64-ucrt-x86_64-cmake-*; do
  if [ -d "$d" ] && [ ! -f "$d/desc" ]; then
    echo ">> removing corrupt local DB entry: $d"
    rm -rf "$d"
  fi
done

echo ">> installing cmake fresh (overwrite any partial files)"
pacman -S --noconfirm --overwrite '*' mingw-w64-ucrt-x86_64-cmake
echo ">> verifying a previously-missing module is now present"
ls -la /c/msys64/ucrt64/share/cmake/Modules/Internal/CMakeDetermineLinkerId.cmake \
       /c/msys64/ucrt64/share/cmake/Modules/Platform/Windows.cmake 2>&1
echo ">> cmake version"
/c/msys64/ucrt64/bin/cmake --version | head -1
echo "REPAIR_DONE"
