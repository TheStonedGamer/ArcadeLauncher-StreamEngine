# ArcadeLauncher Stream Engine — GPL-3.0-or-later
# Dev smoke test: act as the launcher (named-pipe listener), spawn the engine, and exercise the
# real wire protocol (hello handshake + a req/res round-trip) over an actual Windows pipe.
param(
  [string]$Engine = "$PSScriptRoot\..\build\Debug\arcade-stream-engine.exe",
  [string]$Mode   = "stream",
  [string]$Method = "client.hosts"
)
$ErrorActionPreference = 'Stop'
$token = "smoke$([System.Diagnostics.Process]::GetCurrentProcess().Id)"

function Write-Frame($stream, [string]$json) {
  $bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
  $len = [System.BitConverter]::GetBytes([uint32]$bytes.Length)  # LE on x86/x64
  $stream.Write($len, 0, 4)
  $stream.Write($bytes, 0, $bytes.Length)
  $stream.Flush()
}
function Read-Frame($stream) {
  $hdr = New-Object byte[] 4
  $n = 0; while ($n -lt 4) { $r = $stream.Read($hdr, $n, 4 - $n); if ($r -le 0) { return $null }; $n += $r }
  $len = [System.BitConverter]::ToUInt32($hdr, 0)
  $buf = New-Object byte[] $len
  $n = 0; while ($n -lt $len) { $r = $stream.Read($buf, $n, $len - $n); if ($r -le 0) { return $null }; $n += $r }
  return [System.Text.Encoding]::UTF8.GetString($buf)
}

$pipeName = "arcade-stream-engine-$token"
$server = New-Object System.IO.Pipes.NamedPipeServerStream(
  $pipeName, [System.IO.Pipes.PipeDirection]::InOut, 1,
  [System.IO.Pipes.PipeTransmissionMode]::Byte, [System.IO.Pipes.PipeOptions]::Asynchronous)

Write-Host "[launcher] listening on \\.\pipe\$pipeName; launching engine '$Mode'..."
$proc = Start-Process -FilePath $Engine -ArgumentList @($Mode, "--ipc", $token) -PassThru -NoNewWindow

$wait = $server.BeginWaitForConnection($null, $null)
if (-not $wait.AsyncWaitHandle.WaitOne(5000)) { throw "engine did not connect within 5s" }
$server.EndWaitForConnection($wait)
Write-Host "[launcher] engine connected."

$engineHello = Read-Frame $server
Write-Host "[launcher] <- engine hello: $engineHello"
Write-Frame $server '{"kind":"hello","protocolVersion":1,"launcherVersion":"smoke"}'

Write-Frame $server "{`"kind`":`"req`",`"id`":1,`"method`":`"$Method`"}"
$res = Read-Frame $server
Write-Host "[launcher] <- res: $res"

$server.Dispose()                       # EOF -> engine run() returns, exits 0
$proc.WaitForExit(3000) | Out-Null
Write-Host "[launcher] engine exit code: $($proc.ExitCode)"

if ($engineHello -notlike '*"kind":"hello"*') { throw "no engine hello" }
if ($res -notlike '*"id":1*' -or $res -notlike '*"kind":"res"*') { throw "bad response" }
Write-Host "[launcher] OK: handshake + req/res round-trip over a real named pipe."
