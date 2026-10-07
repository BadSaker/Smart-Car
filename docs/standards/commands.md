# 命令执行与临时清理

## 执行原则

使用 PowerShell。执行前说明命令目的、工作目录与涉及文件；工具调用显式设置 `workdir` 为本次任务目录。独立的只读查询可以合并执行，修改、构建、审批和依赖前一步结果的操作按顺序执行。

文本检索优先 `rg`，列文件优先 `rg --files`；如不可用再采用 PowerShell 原生工具。路径使用 `-LiteralPath` 避免通配符误匹配；避免把未校验的文本拼接成命令。不以 `JSON.stringify` 代替 shell 转义。

以下命令示例均以 `C:\Users\Lynn\Desktop\Files\Autodrive` 为工作目录。真实构建使用 [environment-and-build.md](../environment-and-build.md) 中已核实的工具路径、参数与输出位置，不根据示例猜测编译器。

```powershell
rg --files car docs
rg -n 'frame_ready|motor_set_pwm' car/src car/include
Get-Content -LiteralPath 'docs/environment-and-build.md' -Encoding utf8
```

## 结果检查

外部程序运行后立即检查 `$LASTEXITCODE`，并检查输出文件及诊断内容。工具调用同时检查返回的 `exit_code`；PowerShell 原生命令错误不能只靠 `$LASTEXITCODE` 判断，可设置 `$ErrorActionPreference = 'Stop'` 并使错误中止当前脚本。

```powershell
rg --files car
$taskExit = $LASTEXITCODE
if ($taskExit -ne 0) { throw "文件检索失败，退出码：$taskExit" }
```

`rg` 检索内容时退出码 1 可能仅表示未匹配，须按目的解释，不能一律视为工具损坏。构建成功须有明确成功输出和所需产物；“命令已发出”不等于构建通过。日志只记录必要诊断，不输出凭据或敏感环境变量。

## 临时目录清理

临时材料只放入 `tmp/<本任务目录>/`。清理前确认绝对路径仍处于批准工作区的本任务临时目录内，确认该目录不含最终交付物，然后使用同一个 PowerShell 进程和原生命令删除。

下例只授权清理固定任务目录 `tmp/project-docs/`；用于其他任务时先把任务名和批准范围一致地改好并核对。

```powershell
$ErrorActionPreference = 'Stop'
$taskWorkspace = [IO.Path]::GetFullPath('C:\Users\Lynn\Desktop\Files\Autodrive')
$taskTempRoot = [IO.Path]::GetFullPath((Join-Path $taskWorkspace 'tmp'))
$taskCleanup = [IO.Path]::GetFullPath((Join-Path $taskTempRoot 'project-docs'))
$taskAllowed = [IO.Path]::GetFullPath((Join-Path $taskWorkspace 'tmp/project-docs'))
if (-not $taskCleanup.Equals($taskAllowed, [StringComparison]::OrdinalIgnoreCase)) {
    throw '清理目标不等于已批准的任务临时目录'
}
if (-not $taskCleanup.StartsWith($taskTempRoot + [IO.Path]::DirectorySeparatorChar,
                               [StringComparison]::OrdinalIgnoreCase)) {
    throw '清理目标超出 tmp'
}
foreach ($taskAncestor in @($taskWorkspace, $taskTempRoot)) {
    if ((Test-Path -LiteralPath $taskAncestor) -and
        ((Get-Item -LiteralPath $taskAncestor).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw '工作区或 tmp 为链接或重解析点，先核对实际目标'
    }
}
if (Test-Path -LiteralPath $taskCleanup) {
    $taskResolved = (Resolve-Path -LiteralPath $taskCleanup).Path
    if (-not $taskResolved.Equals($taskAllowed, [StringComparison]::OrdinalIgnoreCase)) {
        throw '解析后的目标发生变化'
    }
    $taskLinks = @(Get-Item -LiteralPath $taskCleanup) +
                 @(Get-ChildItem -LiteralPath $taskCleanup -Force -Recurse)
    if ($taskLinks | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
        throw '临时目录含链接或重解析点，先人工核对再清理'
    }
    Remove-Item -LiteralPath $taskCleanup -Recurse -Force
    if (Test-Path -LiteralPath $taskCleanup) { throw '任务临时目录未清理完成' }
}
```

保留空 `tmp/`。禁止删除工作区根目录、原始资料、其他任务临时目录或 `outputs/`；禁止先在 PowerShell 枚举路径再交给 `cmd /c`、批处理或其他 shell 拼接删除。持久目录的移动或清理若超出已批准范围，先提交明确目标与影响供用户审核。
