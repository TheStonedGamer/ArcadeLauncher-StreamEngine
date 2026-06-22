# Add the Sunshine + Moonlight forks as git submodules under vendor/.
# Fork upstream on GitHub FIRST, then point these at YOUR forks.
$ErrorActionPreference = "Stop"

$Sunshine  = if ($env:SUNSHINE_FORK)  { $env:SUNSHINE_FORK }  else { "https://github.com/TheStonedGamer/Sunshine.git" }
$Moonlight = if ($env:MOONLIGHT_FORK) { $env:MOONLIGHT_FORK } else { "https://github.com/TheStonedGamer/moonlight-qt.git" }

Set-Location (Join-Path $PSScriptRoot "..")

if (-not (Test-Path "vendor/sunshine/.git")) {
  Write-Host ">> adding vendor/sunshine  <- $Sunshine"
  git submodule add $Sunshine vendor/sunshine
}
if (-not (Test-Path "vendor/moonlight/.git")) {
  Write-Host ">> adding vendor/moonlight <- $Moonlight"
  git submodule add $Moonlight vendor/moonlight
}

Write-Host ">> initializing submodules recursively (pulls moonlight-common-c)"
git submodule update --init --recursive

Write-Host ""
Write-Host "Done. Next: pin commits with 'git -C vendor/<fork> checkout <sha>' then commit the pointers."
