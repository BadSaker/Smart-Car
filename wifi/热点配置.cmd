@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0wifi-menu.ps1" %*
if errorlevel 1 (
    echo Wi-Fi configuration entry failed. No firmware was built or flashed.
    pause
    exit /b 1
)
exit /b 0
