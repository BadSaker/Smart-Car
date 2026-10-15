# ASCII only: this entry point must parse in Windows PowerShell 5.1.
$ErrorActionPreference = 'Stop'
$simulationRoot = $PSScriptRoot
$reportDirectory = Join-Path $simulationRoot 'reports'
New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
$launcherLog = Join-Path $reportDirectory 'launcher.log'
try {
    $simulationPython = 'C:\Users\Sumi\AppData\Local\Programs\Python\Python313\python.exe'
    $simulationPythonWindow = 'C:\Users\Sumi\AppData\Local\Programs\Python\Python313\pythonw.exe'
    if (-not (Test-Path -LiteralPath $simulationPython)) {
        throw 'Python was not found. Update the interpreter path in simulation/launch.ps1.'
    }
    $env:PYTHONUTF8 = '1'
    $env:PYTHONDONTWRITEBYTECODE = '1'
    $env:SIMULATION_LAUNCH_LOG = Join-Path $reportDirectory 'startup_error.log'
    $checkLog = Join-Path $reportDirectory 'launcher-dependencies.log'
    & $simulationPython -B -c 'import tkinter, numpy, PIL' *> $checkLog
    if ($LASTEXITCODE -ne 0) { throw ('Dependency check failed. See ' + $checkLog) }
    'Dependencies OK' | Add-Content -LiteralPath $checkLog -Encoding UTF8
    $targetPython = $simulationPython
    if (Test-Path -LiteralPath $simulationPythonWindow) { $targetPython = $simulationPythonWindow }
    # This is the interactive simulator: show its GUI window normally.
    $appProcess = Start-Process -FilePath $targetPython -ArgumentList @('-B', ('"' + (Join-Path $simulationRoot 'start_app.py') + '"')) -WorkingDirectory $simulationRoot -WindowStyle Normal -PassThru
    ('Started simulator. PID=' + $appProcess.Id + '; PowerShell=' + $PSVersionTable.PSVersion + '; Python=' + $targetPython) | Set-Content -LiteralPath $launcherLog -Encoding UTF8
    $appProcess.Id | Set-Content -LiteralPath (Join-Path $reportDirectory 'launcher-pid.txt') -Encoding ASCII
    exit 0
} catch {
    ($_ | Out-String) | Set-Content -LiteralPath $launcherLog -Encoding UTF8
    Write-Error -Message ('Startup failed. See ' + $launcherLog) -ErrorAction Continue
    exit 1
}
