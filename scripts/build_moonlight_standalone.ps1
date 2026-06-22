# Milestone 2 (step 1): build the vendored moonlight-common-c client core STANDALONE,
# to prove the toolchain + deps (OpenSSL) on this runner before linking it into the engine.
# Does NOT touch the engine build. See docs/BUILD.md "Bring-up milestones".
$ErrorActionPreference = "Stop"

Set-Location (Join-Path $PSScriptRoot "..")

# cmake: prefer PATH, else fall back to the VS 2022 bundled copy.
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
  $vs = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
  if (Test-Path $vs) { $cmake = $vs } else { throw "cmake not found on PATH or under VS 2022" }
}

# vcpkg toolchain (OpenSSL provider). Override with $env:VCPKG_ROOT.
$vcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { "C:\vcpkg" }
$toolchain = Join-Path $vcpkgRoot "scripts/buildsystems/vcpkg.cmake"
if (-not (Test-Path $toolchain)) { throw "vcpkg toolchain not found at $toolchain (set VCPKG_ROOT)" }

$src   = "vendor/moonlight/moonlight-common-c/moonlight-common-c"
$build = "build/moonlight-standalone"
if (-not (Test-Path "$src/CMakeLists.txt")) { throw "$src missing - run scripts/vendor.ps1 first" }

Write-Host ">> configuring moonlight-common-c standalone ($build)"
& $cmake -S $src -B $build -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DVCPKG_TARGET_TRIPLET=x64-windows
if ($LASTEXITCODE -ne 0) { throw "configure failed" }

Write-Host ">> building (Release)"
& $cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw "build failed" }

Write-Host ""
Write-Host "OK - moonlight-common-c builds standalone. Deps proven (OpenSSL via vcpkg)."
