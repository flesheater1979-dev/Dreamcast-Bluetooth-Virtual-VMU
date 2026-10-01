@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Flash-Dreamcast-BT-VMU.ps1"
endlocal
