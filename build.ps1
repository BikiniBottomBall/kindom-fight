# build.ps1 -- offline build script (no cmake on this machine, uses g++ directly)
#
#   powershell -ExecutionPolicy Bypass -File build.ps1          build all targets
#   powershell -ExecutionPolicy Bypass -File build.ps1 test     build and run unit tests
#   powershell -ExecutionPolicy Bypass -File build.ps1 run      build and run the AI demo
#   powershell -ExecutionPolicy Bypass -File build.ps1 play     build and play: you vs AI
#   powershell -ExecutionPolicy Bypass -File build.ps1 clean    remove build/
#
# Toolchain: MSYS2 UCRT64 g++ 15.2 (already on PATH)
# Switch back to CMake once cmake is available (WSL / CI) -- see CMakeLists.txt
#
# NOTE: keep this file ASCII-only. Windows PowerShell 5.1 reads .ps1 as ANSI when
# there is no BOM, which corrupts any non-ASCII byte and breaks parsing.

param(
    [ValidateSet('build', 'test', 'run', 'play', 'clean', 'all')]
    [string]$Target = 'all'
)

# 'Continue': g++ writes warnings to stderr, and with 'Stop' PowerShell turns any
# stderr line from a native command into a terminating NativeCommandError.
# Failures are detected explicitly through $LASTEXITCODE instead.
$ErrorActionPreference = 'Continue'
$root   = $PSScriptRoot
$build  = Join-Path $root 'build'
$objDir = Join-Path $build 'obj'
$binDir = Join-Path $build 'bin'

$CXX      = 'g++'
$WARN     = @('-std=c++20', '-Wall', '-Wextra', '-Wpedantic', '-Wshadow')
$INCLUDES = @("-I$(Join-Path $root 'src')")
$CXXFLAGS = $WARN + $INCLUDES + @('-O2', '-g')
$LDFLAGS  = @('-static', '-static-libgcc', '-static-libstdc++', '-lws2_32')

function Remove-BuildOutput {
    if (Test-Path $build) { Remove-Item -Recurse -Force $build }
    Write-Host '[clean] removed build/'
}

function Get-CppSources([string]$relativeDir, [string[]]$excludeRelative = @()) {
    $dir = Join-Path $root $relativeDir
    if (-not (Test-Path $dir)) { return @() }
    Get-ChildItem -Path $dir -Filter *.cpp -Recurse -File |
        ForEach-Object { $_.FullName.Substring($root.Length + 1).Replace('\', '/') } |
        Where-Object { $excludeRelative -notcontains $_ } |
        Sort-Object
}

$script:compiled = @{}

function Invoke-Compile([string]$relativeSource) {
    $objPath = Join-Path $objDir ($relativeSource.Replace('/', '_').Replace('.cpp', '') + '.o')
    if ($script:compiled.ContainsKey($objPath)) { return $objPath }
    Write-Host "[cxx] $relativeSource"
    & $CXX @CXXFLAGS -c (Join-Path $root $relativeSource) -o $objPath
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $relativeSource" }
    $script:compiled[$objPath] = $true
    return $objPath
}

function Invoke-Link([string[]]$objects, [string]$exeName) {
    $exePath = Join-Path $binDir $exeName
    Write-Host "[ld ] $exeName"
    & $CXX @objects @LDFLAGS -o $exePath
    if ($LASTEXITCODE -ne 0) { throw "link failed: $exeName" }
    return $exePath
}

function Invoke-Build {
    New-Item -ItemType Directory -Force -Path $objDir | Out-Null
    New-Item -ItemType Directory -Force -Path $binDir | Out-Null

    # 1) core library: every .cpp under src/ except src/main.cpp
    $coreObjects = @()
    foreach ($src in (Get-CppSources 'src' @('src/main.cpp'))) {
        $coreObjects += Invoke-Compile $src
    }

    # 2) AI demo
    $mainObject = Invoke-Compile 'src/main.cpp'
    $demoExe = Invoke-Link ($coreObjects + $mainObject) 'kingdom_fight.exe'

    # 3) terminal client (S10): you vs AI
    $cliExe = $null
    if (Test-Path (Join-Path $root 'client/cli/main.cpp')) {
        $cliObject = Invoke-Compile 'client/cli/main.cpp'
        $cliExe = Invoke-Link ($coreObjects + $cliObject) 'kingdom_fight_cli.exe'
    }

    # 4) net 层验收用 echo 服务端 (S12)
    $echoExe = $null
    if (Test-Path (Join-Path $root 'tools/echo_server.cpp')) {
        $echoObject = Invoke-Compile 'tools/echo_server.cpp'
        $echoExe = Invoke-Link ($coreObjects + $echoObject) 'kingdom_fight_echo.exe'
    }

    # 5) unit tests
    $testObjects = @()
    foreach ($src in (Get-CppSources 'tests')) {
        $testObjects += Invoke-Compile $src
    }
    $testExe = Invoke-Link ($coreObjects + $testObjects) 'kingdom_fight_tests.exe'

    Write-Host ''
    Write-Host 'build ok:'
    Write-Host "  demo : $demoExe"
    if ($cliExe) { Write-Host "  play : $cliExe" }
    if ($echoExe) { Write-Host "  echo : $echoExe" }
    Write-Host "  tests: $testExe"
    return @{ Demo = $demoExe; Cli = $cliExe; Echo = $echoExe; Tests = $testExe }
}

function Invoke-Tests([string]$testExe) {
    Write-Host ''
    Write-Host '--- run unit tests ---'
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw "unit tests failed (exit $LASTEXITCODE)" }
}

if ($Target -eq 'clean') { Remove-BuildOutput; return }

$products = Invoke-Build

switch ($Target) {
    'test' { Invoke-Tests $products.Tests }
    'run'  { & $products.Demo }
    'play' { if ($products.Cli) { & $products.Cli } else { Write-Host 'client/cli/main.cpp not found' } }
    'all'  { Invoke-Tests $products.Tests }
}
