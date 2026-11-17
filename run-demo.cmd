@echo off
rem run-demo.cmd -- start the WSL server + Godot client without fighting
rem PowerShell's execution policy.
rem
rem Why this exists: demo.ps1 is a .ps1 file, and on a default Windows box
rem "restricted" execution policy blocks unsigned scripts:
rem     "demo.ps1 is not digitally signed ... cannot be run on this system"
rem This wrapper calls PowerShell with -ExecutionPolicy Bypass for this one
rem process only, so nothing machine-wide is changed.
rem
rem Usage:  double-click, or  run-demo.cmd [port]

setlocal
set PORT=%~1
if "%PORT%"=="" set PORT=9700

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0demo.ps1" -Port %PORT%
if errorlevel 1 (
    echo.
    echo [run-demo] demo.ps1 exited with an error. See the messages above.
    pause
)
endlocal
