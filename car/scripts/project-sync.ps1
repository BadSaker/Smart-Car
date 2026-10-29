# Windows PowerShell 5.1; save this file as UTF-8 with BOM.
[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [switch]$LibraryOnly
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) { $ProjectRoot = Join-Path $PSScriptRoot '../..' }
$script:SyncRoot = [IO.Path]::GetFullPath($ProjectRoot)
$utf8 = New-Object Text.UTF8Encoding($false)
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8

function Invoke-SyncGit {
    param([string[]]$Arguments, [switch]$AllowFailure)
    # Encode each Windows argv element explicitly. PS 5.1 native splatting loses
    # literal quotes in user-entered commit messages and identity values.
    $argv = @('-C',$script:SyncRoot,'-c','core.quotepath=false') + $Arguments
    $encoded = foreach ($value in $argv) {
        '"' + (($value -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"'
    }
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = @(Get-Command git -CommandType Application -ErrorAction Stop)[0].Source
    $info.Arguments = $encoded -join ' '
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = New-Object Text.UTF8Encoding($false)
    $info.StandardErrorEncoding = New-Object Text.UTF8Encoding($false)
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    try {
        if (-not $process.Start()) { throw '无法启动 Git。' }
        # Drain both pipes concurrently so large diffs/progress cannot deadlock.
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $code = $process.ExitCode
        $output = $stdout.GetAwaiter().GetResult().TrimEnd([char[]]"`r`n")
        $errorText = $stderr.GetAwaiter().GetResult().TrimEnd([char[]]"`r`n")
    } finally { $process.Dispose() }
    # Successful machine-readable stdout must not be contaminated by warnings.
    if ($code -ne 0 -and $errorText) { $output = ($output + "`n" + $errorText).TrimStart([char[]]"`r`n") }
    $result = [pscustomobject]@{ Code = $code; Text = $output }
    if ($code -ne 0 -and -not $AllowFailure) { throw "Git $($Arguments[0]) 失败（$code）：$($result.Text)" }
    return $result
}

function Assert-SyncRepository {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw '未安装 Git。请先安装 Git for Windows，再重新打开本窗口。' }
    $top = (Invoke-SyncGit @('rev-parse','--show-toplevel')).Text
    if (-not [IO.Path]::GetFullPath($top).Equals($script:SyncRoot,[StringComparison]::OrdinalIgnoreCase)) { throw '入口必须位于克隆仓库根目录；下载 ZIP 不含 Git 历史，请先 clone。' }
}

function Assert-SyncWritableState {
    Assert-SyncRepository
    $branch = Invoke-SyncGit @('symbolic-ref','--quiet','--short','HEAD') -AllowFailure
    if ($branch.Code -ne 0 -or $branch.Text -ne 'main') { throw '此菜单仅支持 main 分支。其他分支或游离 HEAD 请交由熟悉 Git 的队友处理。' }
    Invoke-SyncGit @('rev-parse','--verify','HEAD') | Out-Null
    foreach ($marker in @('MERGE_HEAD','CHERRY_PICK_HEAD','REVERT_HEAD','rebase-merge','rebase-apply','sequencer','BISECT_START','index.lock')) {
        $path = (Invoke-SyncGit @('rev-parse','--git-path',$marker)).Text
        if (-not [IO.Path]::IsPathRooted($path)) { $path = Join-Path $script:SyncRoot $path }
        if (Test-Path -LiteralPath $path) { throw "检测到未完成操作或 Git 锁：$marker。请先处理，菜单未继续。" }
    }
    if ((Invoke-SyncGit @('ls-files','-u')).Text) { throw '存在未解决冲突，请先处理。' }
    if ((Invoke-SyncGit @('ls-files','--stage')).Text -match '(?m)^160000 ') { throw '此菜单不处理子模块，请由熟悉 Git 的队友操作。' }
    if ((Invoke-SyncGit @('config','--bool','core.sparseCheckout') -AllowFailure).Text -eq 'true') { throw '此菜单不支持稀疏检出。' }
    if ((Invoke-SyncGit @('ls-files','-v')).Text -cmatch '(?m)^[a-zS] ') { throw '检测到 assume-unchanged/skip-worktree 标记，无法保证保存完整修改，已停止。' }
}

function Update-SyncRemote {
    Assert-SyncRepository
    Write-Host '正在获取 origin/main 最新记录（不改变本地代码）……'
    Invoke-SyncGit @('fetch','--no-tags','origin','+refs/heads/main:refs/remotes/origin/main') | Out-Null
    (Invoke-SyncGit @('rev-parse','--verify','refs/remotes/origin/main^{commit}')).Text
}

function Show-SyncStatus {
    Assert-SyncRepository
    Write-Host "项目：$script:SyncRoot"
    Write-Host (Invoke-SyncGit @('status','--short','--branch','--untracked-files=all')).Text
    Write-Host '以上云端信息是本机缓存；本项不联网。'
    Write-Host '本地恢复分支：'
    Write-Host (Invoke-SyncGit @('for-each-ref','--format=%(refname:short) %(subject)','refs/heads/backup/sync-*')).Text
    Write-Host '本地暂存恢复点（stash）：'
    Write-Host (Invoke-SyncGit @('stash','list')).Text
}

function Set-SyncIdentity {
    Assert-SyncRepository
    Write-Host ('当前有效姓名：' + (Invoke-SyncGit @('config','--get','user.name') -AllowFailure).Text)
    Write-Host ('当前有效邮箱：' + (Invoke-SyncGit @('config','--get','user.email') -AllowFailure).Text)
    Write-Host '只配置当前仓库的提交作者，不等于 GitHub 登录。邮箱可使用 GitHub 设置中的隐私邮箱。'
    $name = Read-Host '输入姓名（留空取消）'
    if ([string]::IsNullOrWhiteSpace($name)) { return }
    $email = Read-Host '输入邮箱（留空取消）'
    if ([string]::IsNullOrWhiteSpace($email)) { return }
    if ($name -match '[\r\n<>]' -or $email -notmatch '^[^\s<>@]+@[^\s<>@]+$') { throw '姓名或邮箱格式不正确，未保存。' }
    if ((Read-Host "仅在此仓库保存：$name <$email>？输入 Y 确认") -ine 'Y') { return }
    Invoke-SyncGit @('config','--local','user.name',$name.Trim()) | Out-Null
    Invoke-SyncGit @('config','--local','user.email',$email.Trim()) | Out-Null
    Write-Host '已保存当前仓库的作者配置。'
}

function Assert-SyncIdentity {
    $name = (Invoke-SyncGit @('config','--get','user.name') -AllowFailure).Text
    $email = (Invoke-SyncGit @('config','--get','user.email') -AllowFailure).Text
    if ([string]::IsNullOrWhiteSpace($name) -or [string]::IsNullOrWhiteSpace($email)) {
        Write-Host '尚未配置作者，请先填写。'
        Set-SyncIdentity
        $name = (Invoke-SyncGit @('config','--get','user.name') -AllowFailure).Text
        $email = (Invoke-SyncGit @('config','--get','user.email') -AllowFailure).Text
        if ([string]::IsNullOrWhiteSpace($name) -or [string]::IsNullOrWhiteSpace($email)) { throw '作者配置未完成，未提交。' }
    }
}

function Save-SyncProgress {
    param([switch]$Push)
    Assert-SyncWritableState
    try { Update-SyncRemote | Out-Null } catch {
        if ($Push) { throw }
        Write-Host "获取失败：$($_.Exception.Message)"
        if ((Read-Host '云端状态未刷新。仍仅在本地保存？输入 Y 确认') -ine 'Y') { return }
    }
    Assert-SyncWritableState
    $changes = (Invoke-SyncGit @('status','--porcelain=v1','--untracked-files=all')).Text
    if ($changes) {
        Assert-SyncIdentity
        Write-Host '本次将提交以下全部未忽略的修改、新增和删除（包括已有暂存内容）：'
        Write-Host $changes
        Write-Host (Invoke-SyncGit @('diff','HEAD','--stat')).Text
        Write-Host '请检查文件内容，尤其是固件内嵌热点凭据、备份和大文件；不应分享的文件请先取消并处理。'
        if ((Read-Host '输入 D 查看已跟踪文件的完整差异，直接回车继续') -ieq 'D') {
            Write-Host (Invoke-SyncGit @('diff','HEAD','--no-ext-diff','--no-textconv')).Text
        }
        $message = Read-Host '请输入本次提交说明（留空取消）'
        if ([string]::IsNullOrWhiteSpace($message)) { Write-Host '未提交：说明为空。'; return }
        if ((Read-Host '确认将上述全部修改保存为本地提交？输入 Y 确认') -ine 'Y') { return }
        Invoke-SyncGit @('add','--all','--','.') | Out-Null
        $staged = Invoke-SyncGit @('diff','--cached','--quiet') -AllowFailure
        if ($staged.Code -eq 1) {
            try { Write-Host (Invoke-SyncGit @('commit','-m',$message)).Text } catch {
                throw "提交失败；文件和暂存内容保留，请检查 Git 提示：$($_.Exception.Message)"
            }
        } elseif ($staged.Code -ne 0) { throw "检查暂存内容失败：$($staged.Text)" }
        else { Write-Host '没有可提交的差异，未创建空提交。' }
    } else { Write-Host '没有待提交修改，未创建空提交。' }
    if (-not $Push) { Write-Host '本地保存步骤完成，未上传。'; return }
    $fetchUrls = (Invoke-SyncGit @('remote','get-url','--all','origin')).Text
    $pushUrls = (Invoke-SyncGit @('remote','get-url','--push','--all','origin')).Text
    if ($fetchUrls -ne $pushUrls -or $pushUrls.Contains("`n")) { throw 'origin 下载/上传地址不同或配置了多个地址，已停止上传，请检查远端配置。' }
    Write-Host '即将上传 main 中尚未上传的全部提交：'
    Write-Host (Invoke-SyncGit @('log','--oneline','origin/main..HEAD')).Text
    if ((Read-Host '上传到 origin/main？输入 Y 确认') -ine 'Y') { Write-Host '本地提交已保留，未上传。'; return }
    $result = Invoke-SyncGit @('push','origin','HEAD:refs/heads/main') -AllowFailure
    if ($result.Code -ne 0) { throw "上传失败；本地提交已保留。若双方有新提交，请协助合并后重试，不要强推。`n$($result.Text)" }
    Write-Host '上传成功。'
}

function Assert-SyncIgnoredSafe {
    param([string]$Target)
    $tree = (Invoke-SyncGit @('ls-tree','-r',$Target)).Text
    if ($tree -match '(?m)^160000 ') { throw '云端含子模块，此菜单不支持自动对齐。' }
    $ignoredText = (Invoke-SyncGit @('ls-files','--others','--ignored','--exclude-standard','-z')).Text
    $targetText = (Invoke-SyncGit @('ls-tree','-r','--name-only','-z',$Target)).Text
    $ignored = @($ignoredText.Split([char]0) | Where-Object { $_ })
    $paths = @($targetText.Split([char]0) | Where-Object { $_ })
    $files = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $directories = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($remote in $paths) {
        [void]$files.Add($remote)
        $parent = $remote
        while ($parent.LastIndexOf('/') -ge 0) {
            $parent = $parent.Substring(0,$parent.LastIndexOf('/'))
            [void]$directories.Add($parent)
        }
    }
    foreach ($local in $ignored) {
        $candidate = $local.TrimEnd('/')
        $collision = $directories.Contains($candidate)
        while ($candidate) {
            if ($files.Contains($candidate)) { $collision = $true; break }
            if ($candidate.LastIndexOf('/') -lt 0) { break }
            $candidate = $candidate.Substring(0,$candidate.LastIndexOf('/'))
        }
        if ($collision) { throw "云端路径与本机忽略内容冲突：$local；为保护本机配置已停止。" }
    }
}

function Reset-SyncToRemote {
    Assert-SyncWritableState
    $target = Update-SyncRemote
    Assert-SyncIgnoredSafe $target
    $original = (Invoke-SyncGit @('rev-parse','HEAD')).Text
    Write-Host '将对齐到本次 fetch 获取的 origin/main：'
    Write-Host (Invoke-SyncGit @('show','-s','--format=%h %s',$target)).Text
    Write-Host '本地独有提交（将保存在恢复分支）：'
    Write-Host (Invoke-SyncGit @('log','--oneline',"$target..HEAD")).Text
    Write-Host '未提交和未跟踪内容（将保存到 stash）：'
    Write-Host (Invoke-SyncGit @('status','--short','--untracked-files=all')).Text
    Write-Host '受跟踪文件的版本差异：'
    Write-Host (Invoke-SyncGit @('diff','--stat','HEAD',$target)).Text
    Write-Host '这会替换本地代码。被忽略的本机配置/缓存保留。请先保存并关闭编辑器，操作期间不要修改文件或运行其他 Git 操作。'
    if ((Read-Host '确认保存恢复点后对齐云端？输入 UPDATE 确认，其余取消') -cne 'UPDATE') { Write-Host '已取消，代码未改变。'; return }
    Assert-SyncWritableState
    if ((Invoke-SyncGit @('rev-parse','HEAD')).Text -ne $original) { throw '确认期间 HEAD 已变化，请重新检查。' }
    Assert-SyncIgnoredSafe $target
    $id = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
    $backup = "backup/sync-$id"
    Invoke-SyncGit @('branch',$backup,$original) | Out-Null
    Write-Host "已保留原提交：$backup"
    if ((Invoke-SyncGit @('rev-parse',"refs/heads/$backup")).Text -ne $original) { throw '恢复分支校验失败，未对齐。' }
    if ((Invoke-SyncGit @('status','--porcelain=v1','--untracked-files=all')).Text) {
        Assert-SyncIdentity
        $previous = (Invoke-SyncGit @('rev-parse','--verify','refs/stash') -AllowFailure).Text
        Invoke-SyncGit @('stash','push','--include-untracked','-m',"project-sync $id before $target") | Out-Null
        $saved = (Invoke-SyncGit @('rev-parse','--verify','refs/stash')).Text
        Write-Host "未提交内容恢复点：$saved（stash；恢复分支：$backup）"
        if ($saved -eq $previous) { throw '未创建新的 stash，未对齐。' }
        if ((Invoke-SyncGit @('status','--porcelain=v1','--untracked-files=all')).Text) { throw '保存后仍有未保存内容（可能存在嵌套仓库），已停止。请保留恢复分支和 stash。' }
    }
    # Recheck ignored files after stash, including restored .gitignore rules.
    Assert-SyncIgnoredSafe $target
    Invoke-SyncGit @('reset','--hard',$target) | Out-Null
    if ((Invoke-SyncGit @('rev-parse','HEAD')).Text -ne $target -or (Invoke-SyncGit @('status','--porcelain=v1','--untracked-files=all')).Text) {
        throw "对齐后校验未通过；保留恢复分支 $backup，请人工检查。"
    }
    Write-Host "已对齐本次获取的云端版本。恢复分支：$backup。未推送、未清理忽略文件。"
}

if (-not $LibraryOnly) {
    try {
        Assert-SyncRepository
        while ($true) {
            Write-Host "`n=== 项目同步 ==="
            Write-Host '1. 查看本地状态'
            Write-Host '2. 提交并上传我的修改'
            Write-Host '3. 本地配置（姓名、邮箱）'
            Write-Host '4. 仅保存本地提交（不上传）'
            Write-Host '5. 更新至云端版本（覆盖本地进展）'
            Write-Host '6. 获取云端最新记录（Fetch，不改本地代码）'
            Write-Host '0. 退出'
            $choice = Read-Host '请选择'
            if ($choice -eq '0' -or $null -eq $choice) { break }
            try {
                switch ($choice) {
                    '1' { Show-SyncStatus }
                    '2' { Save-SyncProgress -Push }
                    '3' { Set-SyncIdentity }
                    '4' { Save-SyncProgress }
                    '5' { Reset-SyncToRemote }
                    '6' {
                        Update-SyncRemote | Out-Null
                        Write-Host '云端记录已更新，本地分支和代码未改变。'
                        Show-SyncStatus
                    }
                    default { Write-Host '请输入 0 至 6。' }
                }
            } catch { Write-Host "操作停止：$($_.Exception.Message)" -ForegroundColor Yellow }
        }
    } catch { Write-Host "无法启动：$($_.Exception.Message)" -ForegroundColor Red; exit 1 }
}
