@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\launch.ps1" -EntryPoint "tools/history/profile_interactive_ui.py"
if errorlevel 1 (
    echo Startup failed. See "%~dp0..\reports\launcher.log".
    pause
    exit /b 1
)
exit /b 0

