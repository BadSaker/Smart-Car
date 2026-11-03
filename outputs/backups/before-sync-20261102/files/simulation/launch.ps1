param([switch]$CheckOnly, [string]$EntryPoint = 'start_app.py')
$ErrorActionPreference = 'Stop'
$simulationRoot = $PSScriptRoot
$reportDirectory = if ($env:SIMULATION_RUNTIME_ROOT) { [IO.Path]::GetFullPath($env:SIMULATION_RUNTIME_ROOT) } else { Join-Path $simulationRoot 'runtime' }
New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
$launcherLog = Join-Path $reportDirectory 'launcher.log'
try {
    $python = Join-Path $simulationRoot 'python/python.exe'
    if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
        throw 'Project Python is missing. Run simulation/setup_python.ps1 first. External Python fallback is disabled.'
    }
    . (Join-Path $simulationRoot 'python_environment.ps1')
    Set-SimulationPythonEnvironment
    $probeOutput = & $python -E -s -B -X utf8 (Join-Path $simulationRoot 'runtime_check.py') 2>&1
    if ($LASTEXITCODE -ne 0) { throw ('Project Python check failed. Run simulation/setup_python.ps1. ' + ($probeOutput -join "`n")) }
    $info = ($probeOutput -join "`n") | ConvertFrom-Json
    # These are diagnostics only; never read them to select an interpreter.
    $info | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $reportDirectory 'launcher-runtime.json') -Encoding UTF8
    @(@{path=$python;accepted=$true;info=$info}) | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $reportDirectory 'launcher-dependencies.json') -Encoding UTF8
    if ($CheckOnly) { $info | ConvertTo-Json -Depth 6; exit 0 }
    $entry = [IO.Path]::GetFullPath((Join-Path $simulationRoot $EntryPoint))
    if (-not $entry.StartsWith($simulationRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Entry point must be inside simulation.' }
    if (-not (Test-Path -LiteralPath $entry -PathType Leaf)) { throw 'Entry point was not found.' }
    $env:SIMULATION_LAUNCH_LOG = Join-Path $reportDirectory 'startup_error.log'
    $windowPython = Join-Path $simulationRoot 'python/pythonw.exe'
    if (-not (Test-Path -LiteralPath $windowPython -PathType Leaf)) { throw 'Project pythonw.exe is missing. Run simulation/setup_python.ps1.' }
    $appProcess = Start-Process -FilePath $windowPython -ArgumentList @('-E','-s','-B','-X','utf8',('"' + $entry + '"')) -WorkingDirectory $simulationRoot -WindowStyle Hidden -PassThru
    ('Started simulator. PID=' + $appProcess.Id + '; Python=' + $windowPython) | Set-Content -LiteralPath $launcherLog -Encoding UTF8
    $appProcess.Id | Set-Content -LiteralPath (Join-Path $reportDirectory 'launcher-pid.txt') -Encoding ASCII
    exit 0
} catch {
    ($_ | Out-String) | Set-Content -LiteralPath $launcherLog -Encoding UTF8
    Write-Error -Message ('Startup failed. ' + $_.Exception.Message + ' See ' + $launcherLog) -ErrorAction Continue
    exit 1
}
