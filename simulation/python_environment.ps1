# Dot-source only in a launcher/setup subprocess. Never changes user/system settings.
function Set-SimulationPythonEnvironment {
    foreach ($item in @(Get-ChildItem Env:)) {
        if ($item.Name -match '^(PYTHON|CONDA|_CONDA)' -or $item.Name -in @('TCL_LIBRARY','TK_LIBRARY','TCLLIBPATH','VIRTUAL_ENV','SIMULATION_PYTHON')) {
            Remove-Item -LiteralPath ('Env:' + $item.Name) -ErrorAction SilentlyContinue
        }
    }
    $pythonRoot = Join-Path $PSScriptRoot 'python'
    $env:PATH = (@($pythonRoot, (Join-Path $pythonRoot 'Scripts'),
        (Join-Path $PSScriptRoot 'toolchain/mingw64/bin'),
        (Join-Path $env:SystemRoot 'System32'), $env:SystemRoot,
        (Join-Path $env:SystemRoot 'System32/Wbem'),
        (Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0')) -join ';')
    $env:TCL_LIBRARY = Join-Path $pythonRoot 'tcl/tcl8.6'
    $env:TK_LIBRARY = Join-Path $pythonRoot 'tcl/tk8.6'
}
