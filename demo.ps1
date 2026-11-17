# demo.ps1 -- start the C++ server in WSL, then launch the Godot client.
#
#   .\demo.ps1
#   .\demo.ps1 -Godot "C:\path\to\godot.exe"
#   .\demo.ps1 -Port 9701
#
# Why the server runs in WSL: this machine enforces Device Guard, which blocks
# freshly built unsigned exes ("Application Control policy has blocked this file").
# Linux binaries are not affected by that policy.
#
# NOTE: keep this file ASCII-only. Windows PowerShell 5.1 reads .ps1 as ANSI when
# there is no BOM, which corrupts any non-ASCII byte and breaks parsing -- and a
# mangled comment can even swallow the next line.
param(
    [int]$Port = 9700,
    [ValidateSet("easy", "normal", "hard")]
    [string]$AiLevel = "normal",
    [string]$Godot = "D:\steam\steamapps\common\Godot Engine\godot.windows.opt.tools.64.exe"
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

if (-not (Test-Path $Godot)) {
    Write-Host "Godot not found: $Godot" -ForegroundColor Red
    Write-Host "Pass it explicitly: .\demo.ps1 -Godot <path-to-godot.exe>" -ForegroundColor Yellow
    exit 1
}

# D:\Code\... -> /mnt/d/Code/...
$wslPath = "/mnt/" + $root.Substring(0, 1).ToLower() + $root.Substring(2).Replace('\', '/')

Write-Host "[demo] project (WSL): $wslPath" -ForegroundColor DarkGray
Write-Host "[demo] building + starting the server in WSL..." -ForegroundColor Cyan

$serverJob = Start-Job -ScriptBlock {
    param($path, $port, $level)
    wsl bash -lc "cd '$path' && ./tools/serve_wsl.sh $port $level"
} -ArgumentList $wslPath, $Port, $AiLevel

# Fast port probe: Test-NetConnection costs hundreds of ms per call.
function Test-TcpPort([int]$TargetPort) {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $task = $client.ConnectAsync('127.0.0.1', $TargetPort)
        if ($task.Wait(300) -and $client.Connected) { return $true }
        return $false
    } catch {
        return $false
    } finally {
        $client.Dispose()
    }
}

# First build takes 10-20s; after that the binary is up to date and it starts in ~1s.
$ready = $false
for ($i = 0; $i -lt 200; $i++) {
    Start-Sleep -Milliseconds 300
    if (Test-TcpPort $Port) { $ready = $true; break }
}

if (-not $ready) {
    Write-Host "[demo] server did not come up. last log lines:" -ForegroundColor Red
    Receive-Job $serverJob
    Stop-Job $serverJob -ErrorAction SilentlyContinue
    Remove-Job $serverJob -Force -ErrorAction SilentlyContinue
    exit 1
}
Write-Host "[demo] server ready on 127.0.0.1:$Port" -ForegroundColor Green

try {
    # Pass the port through: the client defaults to 9700, so if the server
    # listens somewhere else the client would connect to the wrong place.
    & $Godot --path "$root/client/godot" -- "--port=$Port"
} finally {
    # The Steam build detaches: this call can return before the game window
    # even exists. Wait for the window to show up, then keep the server alive
    # until it is gone -- otherwise the client lands on "not connected".
    function Get-GameWindow {
        # Match by process name only: MainWindowTitle can be empty depending on
        # how the process was started, which made this wait exit immediately.
        @(Get-Process | Where-Object { $_.ProcessName -like 'godot*' })
    }

    $appeared = $false
    for ($i = 0; $i -lt 40; $i++) {
        if ((Get-GameWindow).Count -gt 0) { $appeared = $true; break }
        Start-Sleep -Seconds 1
    }

    if ($appeared) {
        $deadline = (Get-Date).AddHours(8)
        while ((Get-Date) -lt $deadline -and (Get-GameWindow).Count -gt 0) {
            Start-Sleep -Seconds 2
        }
    }

    Write-Host "[demo] stopping the server..." -ForegroundColor DarkGray
    wsl -e pkill -f "kingdom_fight_server --port" 2>$null | Out-Null
    Stop-Job $serverJob -ErrorAction SilentlyContinue
    Remove-Job $serverJob -Force -ErrorAction SilentlyContinue
}
