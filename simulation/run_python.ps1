# Keep Python switches in $args; PowerShell parameter binding consumes -m/-c.
$ErrorActionPreference = 'Stop'
$python = Join-Path $PSScriptRoot 'python/python.exe'
if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
    throw 'Project Python is missing. Run simulation/setup_python.ps1 first.'
}
. (Join-Path $PSScriptRoot 'python_environment.ps1')
Set-SimulationPythonEnvironment
& $python -E -s -B -X utf8 @args
exit $LASTEXITCODE
