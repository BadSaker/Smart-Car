param(
    [string]$Compiler = '',
    [string]$LogName = 'algorithm-tests.log'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$workspaceRoot = Split-Path -Parent $projectRoot
if (-not $Compiler) {
    $gccCommand = Get-Command gcc -ErrorAction SilentlyContinue
    if ($gccCommand) { $Compiler = $gccCommand.Source }
    elseif (Test-Path -LiteralPath 'D:\Xilinx\Vivado\2022.2\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe') {
        $Compiler = 'D:\Xilinx\Vivado\2022.2\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe'
    }
    else { throw 'Set -Compiler to a native Windows GCC executable.' }
}
if ([IO.Path]::GetFileName($LogName) -ne $LogName) { throw 'LogName must be a filename.' }
$buildPath = Join-Path $workspaceRoot 'tmp\algorithm-tests'
$logPath = Join-Path $workspaceRoot ('logs\' + $LogName)
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
