@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0car\scripts\project-sync.ps1"
set "sync_exit=%errorlevel%"
pause
exit /b %sync_exit%
