# 只读核对项目入口、工程引用及仿真共享源码，不加载凭据或运行构建。
param([string]$ProjectRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
foreach ($relative in @('AGENTS.md', 'CLAUDE.md', 'car/autocar.uvprojx', 'simulation/source_layout.py')) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $relative) -PathType Leaf)) {
        throw "项目根目录缺少入口：$relative；请指定 Autodrive 目录。"
    }
}
if ((Get-Content -LiteralPath (Join-Path $root 'CLAUDE.md') -Raw -Encoding UTF8) -notmatch '(?m)^@AGENTS\.md\s*$') {
    throw 'CLAUDE.md未导入共享AGENTS.md。'
}
[xml]$project = Get-Content -LiteralPath (Join-Path $root 'car/autocar.uvprojx') -Raw
$references = @($project.SelectNodes('//FilePath'))
foreach ($reference in $references) {
    if (-not (Test-Path -LiteralPath (Join-Path (Join-Path $root 'car') $reference.InnerText) -PathType Leaf)) {
        throw "工程引用不存在：$($reference.InnerText)"
    }
}
$sources = @(
    'src/track_vision.c', 'src/autonomous_drive.c', 'src/control_pid.c',
    'include/track_vision.h', 'include/autonomous_drive.h', 'include/control_pid.h',
    'config/autonomous_config.h', 'config/vision_config.h', 'config/encoder_config.h',
    'config/vehicle_geometry.h', 'config/servo_config.h'
)
foreach ($relative in $sources) {
    if (-not (Test-Path -LiteralPath (Join-Path (Join-Path $root 'car') $relative) -PathType Leaf)) {
        throw "共享算法来源不存在：$relative"
    }
}
[pscustomobject]@{
    ProjectRoot = $root
    CurrentDirectoryMatches = (Get-Location).Path -eq $root
    EngineeringReferences = $references.Count
    SharedSourceFiles = $sources.Count
    RulesEntry = 'AGENTS.md'
    ReadOnly = $true
}
