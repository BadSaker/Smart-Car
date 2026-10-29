# Install only into simulation/python. No registry, PATH or system Python changes.
param([switch]$CheckOnly)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$manifest = Get-Content -LiteralPath (Join-Path $root 'python-runtime.json') -Raw | ConvertFrom-Json
$pythonRoot = Join-Path $root 'python'
$python = Join-Path $pythonRoot 'python.exe'
if ($CheckOnly) {
    & (Join-Path $root 'launch.ps1') -CheckOnly
    exit $LASTEXITCODE
}
$cache = Join-Path $root 'runtime/downloads'
New-Item -ItemType Directory -Force -Path $cache | Out-Null
function Get-VerifiedArtifact([string]$url, [string]$name, [string]$sha256) {
    $target = Join-Path $cache $name
    if (-not (Test-Path -LiteralPath $target)) {
        Write-Host ('Downloading ' + $name)
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $target
    }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $sha256) {
        throw ('SHA256 mismatch. Remove the invalid cached file and retry: ' + $target)
    }
    return $target
}
if (-not (Test-Path -LiteralPath $pythonRoot)) {
    $archive = Get-VerifiedArtifact $manifest.url $manifest.archive $manifest.sha256
    & (Join-Path $env:SystemRoot 'System32/tar.exe') -xzf $archive -C $root
    if ($LASTEXITCODE -ne 0) { throw 'Python extraction failed. Inspect simulation/python before retrying.' }
} elseif (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
    throw 'Existing simulation/python is incomplete; it was not overwritten. Move it aside and retry.'
}
. (Join-Path $root 'python_environment.ps1')
Set-SimulationPythonEnvironment
# --isolated alone still reads global/PIP_CONFIG_FILE settings (target/prefix).
$env:PIP_CONFIG_FILE = 'nul'
$version = & $python -E -s -B -c 'import sys; print(sys.version.split()[0])'
if ($LASTEXITCODE -ne 0 -or $version -ne $manifest.version) { throw 'Existing Python version differs from manifest; it was not overwritten.' }
foreach ($wheel in $manifest.wheels) { $null = Get-VerifiedArtifact $wheel.url $wheel.filename $wheel.sha256 }
& $python -E -s -B -X utf8 -m pip --isolated --no-cache-dir install --no-index --find-links $cache --only-binary=:all: --require-hashes --no-deps --no-warn-script-location --no-compile -r (Join-Path $root 'requirements.lock')
if ($LASTEXITCODE -ne 0) { throw 'Pinned dependency installation failed.' }
& $python -E -s -B -X utf8 -m pip --isolated check
if ($LASTEXITCODE -ne 0) { throw 'Dependency check failed.' }
& (Join-Path $root 'launch.ps1') -CheckOnly
exit $LASTEXITCODE
