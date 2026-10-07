param([string]$KeilPath = 'D:\Keil_v5\UV4\UV4.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$workspaceRoot = Split-Path -Parent $projectRoot
$projectPath = Join-Path $projectRoot 'autocar.uvprojx'
$buildPath = Join-Path $workspaceRoot 'tmp\build-autocar'
$outputPath = Join-Path $workspaceRoot 'outputs\firmware'
$logPath = Join-Path $workspaceRoot 'logs\keil-build.log'
if (-not (Test-Path -LiteralPath $KeilPath)) { throw "Keil executable not found: $KeilPath" }
$fromElf = Join-Path (Split-Path -Parent (Split-Path -Parent $KeilPath)) 'ARM\ARMCLANG\bin\fromelf.exe'
if (-not (Test-Path -LiteralPath $fromElf)) { throw "Required BIN exporter not found: $fromElf. Previous outputs were preserved." }
foreach ($path in @($buildPath, (Join-Path $buildPath 'objects'), (Join-Path $buildPath 'listings'), $outputPath, (Split-Path -Parent $logPath))) {
    New-Item -ItemType Directory -Path $path -Force | Out-Null
}
if (Test-Path -LiteralPath $logPath) {
    $archiveName = 'keil-build-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.log'
    Copy-Item -LiteralPath $logPath -Destination (Join-Path (Split-Path -Parent $logPath) $archiveName)
    Remove-Item -LiteralPath $logPath
}
$buildStarted = Get-Date
$arguments = '-r "' + $projectPath + '" -t "autocar" -o "' + $logPath + '"'
$process = Start-Process -FilePath $KeilPath -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru -Wait
if (-not (Test-Path -LiteralPath $logPath)) { throw "Keil returned $($process.ExitCode) without a build log." }
$buildText = [IO.File]::ReadAllText($logPath)
Get-Content -LiteralPath $logPath -Tail 12
$summary = [regex]::Match($buildText, '(\d+) Error\(s\),\s*(\d+) Warning\(s\)')
if ($process.ExitCode -notin @(0, 1) -or -not $summary.Success -or [int]$summary.Groups[1].Value -ne 0) {
    throw "Keil build failed (exit $($process.ExitCode)). See $logPath"
}
foreach ($extension in @('axf', 'hex')) {
    $artifact = Join-Path $buildPath ('objects\autocar.' + $extension)
    if (-not (Test-Path -LiteralPath $artifact) -or (Get-Item -LiteralPath $artifact).Length -eq 0 -or (Get-Item -LiteralPath $artifact).LastWriteTime -lt $buildStarted) {
        throw "Missing or stale build artifact: $artifact"
    }
}
$stagedBin = Join-Path $buildPath 'objects\autocar.bin'
& $fromElf --bin --output $stagedBin (Join-Path $buildPath 'objects\autocar.axf')
if ($LASTEXITCODE -ne 0) { throw 'fromelf binary export failed. Previous outputs were preserved.' }
if (-not (Test-Path -LiteralPath $stagedBin) -or (Get-Item -LiteralPath $stagedBin).Length -eq 0 -or (Get-Item -LiteralPath $stagedBin).LastWriteTime -lt $buildStarted) {
    throw 'BIN export is missing, empty or stale. Previous outputs were preserved.'
}
foreach ($extension in @('axf', 'hex', 'bin')) {
    Copy-Item -LiteralPath (Join-Path $buildPath ('objects\autocar.' + $extension)) -Destination $outputPath -Force
}
Write-Output "Verified build: $($summary.Groups[1].Value) errors, $($summary.Groups[2].Value) warnings. Artifacts: $outputPath"
