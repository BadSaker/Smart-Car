param(
    [string]$Compiler = '',
    [string]$LogName = 'algorithm-tests.log'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$workspaceRoot = Split-Path -Parent $projectRoot
. (Join-Path $PSScriptRoot 'find-tool.ps1')
$Compiler = Find-AutocarTool 'Gcc' $Compiler
if ([IO.Path]::GetFileName($LogName) -ne $LogName) { throw 'LogName must be a filename.' }
$buildPath = Join-Path $workspaceRoot 'car\build\algorithm-tests'
$logPath = Join-Path $workspaceRoot ('car\build\' + $LogName)
New-Item -ItemType Directory -Path $buildPath -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path -Parent $logPath) -Force | Out-Null
$testExe = Join-Path $buildPath 'test-algorithms.exe'
$oldPath = $env:PATH
try {
    $env:PATH = (Split-Path -Parent $Compiler) + ';' + $env:PATH
    & $Compiler '-std=c99' '-Wall' '-Wextra' '-I' (Join-Path $projectRoot 'include') `
        (Join-Path $projectRoot 'tests\test_algorithms.c') `
        (Join-Path $projectRoot 'src\img_process.c') `
        (Join-Path $projectRoot 'src\pid.c') '-lm' '-o' $testExe 2>&1 | Tee-Object -FilePath $logPath
    if ($LASTEXITCODE -ne 0) { throw "Native test compilation failed. See $logPath" }
    & $testExe 2>&1 | Tee-Object -FilePath $logPath -Append
    $testExit = $LASTEXITCODE
} finally { $env:PATH = $oldPath }
if ($testExit -ne 0) { throw "Algorithm regression tests failed. See $logPath" }
