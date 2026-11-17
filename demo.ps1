# demo.ps1 -- build (if needed) and start the C++ server on this machine, then launch
#             the Godot client.
#
#   .\demo.ps1
#   .\demo.ps1 -AiLevel hard
#   .\demo.ps1 -Godot "C:\path\to\godot.exe" -Port 9701
#
# History: this used to run the server inside WSL, because Device Guard on this
# machine refused to execute freshly built unsigned exes ("Application Control
# policy has blocked this file"). That is no longer true -- the Windows build
# runs fine (verified 2026-11-17, poller=select), so the default path is now
# native Windows and starts in ~2 seconds instead of ~40.
# WSL is still supported:  .\demo.ps1 -Wsl
#
# NOTE: keep this file ASCII-only. Windows PowerShell 5.1 reads .ps1 as ANSI when
# there is no BOM, which corrupts any non-ASCII byte and breaks parsing -- and a
# mangled comment can even swallow the next line.
param(
    [int]$Port = 9700,
    [ValidateSet("easy", "normal", "hard")]
    [string]$AiLevel = "normal",
    [string]$Godot = "D:\steam\steamapps\common\Godot Engine\godot.windows.opt.tools.64.exe",
    [switch]$Fast,
    [switch]$Wsl
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$serverExe = Join-Path $root 'build\bin\kingdom_fight_server.exe'
$buildScript = Join-Path $root 'build.ps1'

function Fail([string]$message) {
    Write-Host $message -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $Godot)) {
    Fail "Godot not found: $Godot`nPass it explicitly: .\demo.ps1 -Godot <path-to-godot.exe>"
}

# ---- 1) server: build if missing, then start it ---------------------------
$serverArgs = @("--port", "$Port", "--ai-level", $AiLevel)
if ($Fast) { $serverArgs += "--fast" }

if ($Wsl) {
    # Legacy path: build + run inside WSL (uses the real epoll backend).
    $wslPath = "/mnt/" + $root.Substring(0, 1).ToLower() + $root.Substring(2).Replace('\', '/')
    Write-Host "[demo] server in WSL: $wslPath" -ForegroundColor DarkGray
    $server = Start-Process -FilePath "wsl" `
        -ArgumentList @("bash", "-lc", "cd '$wslPath' && ./tools/serve_wsl.sh $Port $AiLevel") `
        -PassThru -NoNewWindow
} else {
    if (-not (Test-Path $serverExe)) {
        Write-Host "[demo] server binary missing -- building (first time takes ~1-2 min)..." -ForegroundColor Cyan
        & powershell -NoProfile -ExecutionPolicy Bypass -File $buildScript build
        if (-not (Test-Path $serverExe)) { Fail "[demo] build did not produce $serverExe" }
    }
    Write-Host "[demo] server: $serverExe $($serverArgs -join ' ')" -ForegroundColor DarkGray
    $server = Start-Process -FilePath $serverExe -ArgumentList $serverArgs -PassThru -NoNewWindow
}

# ---- 2) wait for the port to accept connections ---------------------------
$ready = $false
for ($i = 0; $i -lt 100; $i++) {
    if ($server.HasExited) { break }
    try {
        $probe = New-Object System.Net.Sockets.TcpClient
        $probe.Connect('127.0.0.1', $Port)
        $probe.Close()
        $ready = $true
        break
    } catch {
        Start-Sleep -Milliseconds 100
    }
}

if (-not $ready) {
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue }
    Fail "[demo] server did not open port $Port. Check the error above (port already in use?)."
}
Write-Host "[demo] server up on 127.0.0.1:$Port (ai=$AiLevel)" -ForegroundColor Green

# ---- 3) client -----------------------------------------------------------
try {
    Write-Host "[demo] launching Godot client..." -ForegroundColor Cyan
    & $Godot --path (Join-Path $root 'client\godot') -- "--port=$Port"
} finally {
    # Godot closed -> take the server down with it, so the next run is not blocked by a busy port
    if (-not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
        Write-Host "[demo] server stopped." -ForegroundColor DarkGray
    }
}
