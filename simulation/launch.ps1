# UTF-8 BOM用于兼容Windows PowerShell 5.1的中文注释。
param([switch]$CheckOnly, [string]$EntryPoint = 'start_app.py')
$ErrorActionPreference = 'Stop'
$simulationRoot = $PSScriptRoot
$reportDirectory = if ($env:SIMULATION_RUNTIME_ROOT) { [IO.Path]::GetFullPath($env:SIMULATION_RUNTIME_ROOT) } else { Join-Path $simulationRoot 'runtime' }
New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
$launcherLog = Join-Path $reportDirectory 'launcher.log'
$runtimeCache = Join-Path $reportDirectory 'launcher-runtime.json'
# 兼容旧位置的发现缓存；只读取，成功探测后写入新的运行目录。
$legacyCaches = @(
    (Join-Path $simulationRoot 'reports/launcher-runtime.json'),
    (Join-Path $simulationRoot '../outputs/reports/simulation-history-2026-10-14/launcher-runtime.json')
)
$candidates = New-Object 'System.Collections.Generic.List[string]'
$seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$attempts = New-Object 'System.Collections.Generic.List[object]'
function Add-Candidate([string]$candidate) {
    if (-not $candidate) { return }
    $candidate = [Environment]::ExpandEnvironmentVariables($candidate.Trim('"'))
    if ($candidate -match '\\Microsoft\\WindowsApps\\') { return }
    if ((Test-Path -LiteralPath $candidate -PathType Leaf) -and $seen.Add($candidate)) { $candidates.Add($candidate) }
}
$probe = @'
import sys,struct,json,importlib.util,importlib.metadata
packages={name:importlib.util.find_spec(name) is not None for name in ('tkinter','_tkinter','numpy','PIL')}
versions={name:importlib.metadata.version(name) for name in ('numpy','Pillow') if packages['numpy' if name=='numpy' else 'PIL']}
print(json.dumps({'executable':sys.executable,'version':sys.version.split()[0],'bits':struct.calcsize('P')*8,'packages':packages,'versions':versions}))
'@
function Find-Runtime {
    foreach ($candidate in $candidates.ToArray()) {
        $process = New-Object System.Diagnostics.Process
        $process.StartInfo.FileName = $candidate
        $process.StartInfo.Arguments = '-B -c "' + $probe.Replace("`r", '').Replace("`n", ';') + '"'
        $process.StartInfo.UseShellExecute = $false
        $process.StartInfo.CreateNoWindow = $true
        $process.StartInfo.RedirectStandardOutput = $true
        $process.StartInfo.RedirectStandardError = $true
        try {
            [void]$process.Start()
            if (-not $process.WaitForExit(5000)) { $process.Kill(); throw 'Interpreter probe timed out.' }
            $output = $process.StandardOutput.ReadToEnd()
            $failure = $process.StandardError.ReadToEnd()
            if ($process.ExitCode -ne 0) { throw $failure }
            $info = $output | ConvertFrom-Json
            if ($info.bits -ne 64) { throw 'A 64-bit Python interpreter is required.' }
            if ([version]$info.version -lt [version]'3.10') { throw 'Python 3.10 or newer is required.' }
            foreach ($name in @('tkinter','_tkinter','numpy','PIL')) {
                if (-not $info.packages.$name) { throw ('Missing dependency: ' + $name) }
            }
            if ([version]$info.versions.Pillow -lt [version]'9.1') { throw 'Pillow 9.1 or newer is required.' }
            $attempts.Add(@{ path=$candidate; accepted=$true; info=$info })
            return $info
        } catch {
            $attempts.Add(@{ path=$candidate; accepted=$false; reason=$_.Exception.Message })
        } finally { $process.Dispose() }
    }
    $candidates.Clear()
    return $null
}
try {
    $env:PYTHONUTF8 = '1'
    $env:PYTHONDONTWRITEBYTECODE = '1'
    Add-Candidate $env:SIMULATION_PYTHON
    foreach ($relative in @('.venv\Scripts\python.exe','runtime\python.exe','python\python.exe')) { Add-Candidate (Join-Path $simulationRoot $relative) }
    if (Test-Path -LiteralPath $runtimeCache) {
        try { Add-Candidate ((Get-Content -LiteralPath $runtimeCache -Raw | ConvertFrom-Json).executable) } catch {}
    }
    foreach ($legacyCache in $legacyCaches) {
        if (Test-Path -LiteralPath $legacyCache) {
            try { Add-Candidate ((Get-Content -LiteralPath $legacyCache -Raw | ConvertFrom-Json).executable) } catch {}
        }
    }
    $selected = Find-Runtime
    if (-not $selected) {
        $pythonLauncher = Get-Command py.exe -ErrorAction SilentlyContinue
        if ($pythonLauncher) {
            try {
                foreach ($line in (& $pythonLauncher.Source -0p 2>$null)) {
                    if ($line -match '([A-Za-z]:\\.*python(?:\d+(?:\.\d+)*)?\.exe)\s*$') { Add-Candidate $matches[1] }
                }
            } catch { $attempts.Add(@{path=$pythonLauncher.Source;accepted=$false;reason='Launcher enumeration failed; continuing with PATH and registry.'}) }
        }
        foreach ($name in @('python.exe','python3.exe')) {
            foreach ($command in @(Get-Command $name -All -ErrorAction SilentlyContinue)) { Add-Candidate $command.Source }
        }
        foreach ($registry in @('HKCU:\Software\Python\PythonCore','HKLM:\Software\Python\PythonCore','HKLM:\Software\WOW6432Node\Python\PythonCore')) {
            if (Test-Path $registry) {
                foreach ($versionKey in @(Get-ChildItem $registry -ErrorAction SilentlyContinue | Sort-Object PSChildName -Descending)) {
                    $installKey = Get-Item (Join-Path $versionKey.PSPath 'InstallPath') -ErrorAction SilentlyContinue
                    if ($installKey) {
                        Add-Candidate ($installKey.GetValue('ExecutablePath'))
                        $installDirectory = $installKey.GetValue('')
                        if ($installDirectory) { Add-Candidate (Join-Path $installDirectory 'python.exe') }
                    }
                }
            }
        }
        if ($env:LOCALAPPDATA) {
            foreach ($directory in @(Get-ChildItem (Join-Path $env:LOCALAPPDATA 'Programs\Python') -Directory -Filter 'Python*' -ErrorAction SilentlyContinue | Sort-Object Name -Descending)) { Add-Candidate (Join-Path $directory.FullName 'python.exe') }
        }
        $selected = Find-Runtime
    }
    ConvertTo-Json -InputObject @($attempts.ToArray()) -Depth 6 | Set-Content -LiteralPath (Join-Path $reportDirectory 'launcher-dependencies.json') -Encoding UTF8
    if (-not $selected) { throw ('No usable 64-bit Python was found. Install Python with Tkinter, NumPy and Pillow, or provide simulation/.venv or SIMULATION_PYTHON. See ' + (Join-Path $reportDirectory 'launcher-dependencies.json')) }
    $selected | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $runtimeCache -Encoding UTF8
    if ($CheckOnly) { $selected | ConvertTo-Json -Depth 5; exit 0 }
    $entry = [IO.Path]::GetFullPath((Join-Path $simulationRoot $EntryPoint))
    if (-not $entry.StartsWith($simulationRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Entry point must be inside simulation.' }
    if (-not (Test-Path -LiteralPath $entry -PathType Leaf)) { throw 'Entry point was not found.' }
    $env:SIMULATION_LAUNCH_LOG = Join-Path $reportDirectory 'startup_error.log'
    $targetPython = $selected.executable
    $windowPython = Join-Path (Split-Path $targetPython -Parent) 'pythonw.exe'
    if (Test-Path -LiteralPath $windowPython) { $targetPython = $windowPython }
    # 用户启动入口请求可见仿真窗口。
    $appProcess = Start-Process -FilePath $targetPython -ArgumentList @('-B', ('"' + $entry + '"')) -WorkingDirectory $simulationRoot -WindowStyle Normal -PassThru
    ('Started simulator. PID=' + $appProcess.Id + '; PowerShell=' + $PSVersionTable.PSVersion + '; Python=' + $targetPython) | Set-Content -LiteralPath $launcherLog -Encoding UTF8
    $appProcess.Id | Set-Content -LiteralPath (Join-Path $reportDirectory 'launcher-pid.txt') -Encoding ASCII
    exit 0
} catch {
    ($_ | Out-String) | Set-Content -LiteralPath $launcherLog -Encoding UTF8
    Write-Error -Message ('Startup failed. See ' + $launcherLog) -ErrorAction Continue
    exit 1
}
