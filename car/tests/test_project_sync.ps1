param()
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$implementation = Join-Path $root 'car/scripts/project-sync.ps1'
if (-not (Test-Path -LiteralPath $implementation)) { throw 'FAIL: project-sync implementation is missing' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $implementation -LibraryOnly
if ($LASTEXITCODE -ne 0) { throw 'FAIL: default -File startup' }
$scratch = Join-Path $root ('tmp/project-sync-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$script:answers = New-Object 'System.Collections.Generic.Queue[string]'
function Read-Host { param([string]$Prompt) if ($script:answers.Count -eq 0) { throw "Unexpected prompt: $Prompt" }; $script:answers.Dequeue() }
function Answer { param([string[]]$Values) $script:answers.Clear(); foreach ($v in $Values) { $script:answers.Enqueue($v) } }
function G { param([string]$At, [string[]]$ArgsList)
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $out = & git -C $At @ArgsList 2>&1; $code = $LASTEXITCODE; $ErrorActionPreference = $old
    if ($code -ne 0) { throw "Git failed: $ArgsList : $out" }; ($out | ForEach-Object { "$_" }) -join "`n"
}
$script:checks = 0
function Check { param([bool]$Condition, [string]$Name) if (-not $Condition) { throw "FAIL: $Name" }; $script:checks++; Write-Host "PASS: $Name" }
function Reject { param([scriptblock]$Action, [string]$Pattern) $caught = ''; try { & $Action } catch { $caught = $_.Exception.Message }; Check ($caught -match $Pattern) "reject $Pattern ($caught)" }
try {
    $remote = Join-Path $scratch 'remote.git'; $a = Join-Path $scratch '队友 甲'; $b = Join-Path $scratch '队友 乙'
    G $scratch @('init','--bare',$remote) | Out-Null
    G $scratch @('init','-b','main',$a) | Out-Null
    foreach ($setting in @(@('user.name','Tester'),@('user.email','tester@example.com'),@('commit.gpgsign','false'))) { G $a (@('config','--local') + $setting) | Out-Null }
    Set-Content -LiteralPath (Join-Path $a '.gitignore') -Value "private/`n*.local" -Encoding ascii
    Set-Content -LiteralPath (Join-Path $a 'code.txt') -Value 'base' -Encoding ascii
    G $a @('add','.') | Out-Null; G $a @('commit','-m','base') | Out-Null
    G $a @('remote','add','origin',$remote) | Out-Null; G $a @('push','-u','origin','main') | Out-Null
    G $scratch @('clone','-b','main',$remote,$b) | Out-Null
    G $b @('config','user.name','Other') | Out-Null; G $b @('config','user.email','other@example.com') | Out-Null; G $b @('config','commit.gpgsign','false') | Out-Null
    . $implementation -ProjectRoot $a -LibraryOnly
    foreach ($literal in @('fix "quoted" text','路径 with spaces\','反斜杠\"quoted"')) {
        Check ((Invoke-SyncGit @('-c',"test.value=$literal",'config','--get','test.value')).Text -ceq $literal) "native literal roundtrip: $literal"
    }
    $globalBefore = & git config --global --get user.name
    Answer @('队友甲','teammate@example.com','Y'); Set-SyncIdentity
    Check ((G $a @('config','--local','user.name')) -eq '队友甲') 'local identity'
    Check ((& git config --global --get user.name) -eq $globalBefore) 'global identity unchanged'
    Set-Content (Join-Path $a 'publish.txt') 'publish'
    $beforeCancel = G $a @('rev-parse','HEAD')
    Answer @('',''); Save-SyncProgress
    Check ((G $a @('rev-parse','HEAD')) -eq $beforeCancel) 'empty message cancels commit'
    Check ((G $a @('diff','--cached','--name-only')) -eq '') 'cancel leaves index unchanged'
    Answer @('','首次上传','Y','Y'); Save-SyncProgress -Push
    Check ((G $remote @('rev-parse','refs/heads/main')) -eq (G $a @('rev-parse','HEAD'))) 'normal push updates remote'
    G $b @('pull','--ff-only') | Out-Null
    $base = G $a @('rev-parse','HEAD')
    Set-Content (Join-Path $b 'remote.txt') 'remote'; G $b @('add','.') | Out-Null; G $b @('commit','-m','remote') | Out-Null; G $b @('push') | Out-Null
    Set-Content (Join-Path $a 'code.txt') 'unfinished'
    Answer @('6','0'); . $implementation -ProjectRoot $a
    Check ((G $a @('rev-parse','origin/main')) -eq (G $b @('rev-parse','HEAD'))) 'menu fetch refreshes remote tracking'
    Check ((G $a @('rev-parse','HEAD')) -eq $base) 'fetch preserves HEAD'
    Check ((Get-Content (Join-Path $a 'code.txt')) -eq 'unfinished') 'fetch preserves dirty file'
    Answer @('','本地进展','Y'); Save-SyncProgress
    $local = G $a @('rev-parse','HEAD')
    Check ((G $a @('log','-1','--format=%s')) -eq '本地进展') 'typed commit message'
    Check ((G $a @('rev-parse','origin/main')) -ne $local) 'local save does not push'
    Answer @('Y'); Reject { Save-SyncProgress -Push } '上传失败'
    Check ((G $a @('rev-parse','HEAD')) -eq $local) 'rejected push preserves commit'
    Set-Content (Join-Path $a 'code.txt') 'staged'; G $a @('add','code.txt') | Out-Null
    Set-Content (Join-Path $a 'code.txt') 'unstaged'
    Set-Content (Join-Path $a '新增.txt') 'new'
    Set-Content (Join-Path $a 'settings.local') 'secret'
    Answer @('NO'); Reset-SyncToRemote
    Check ((G $a @('rev-parse','HEAD')) -eq $local) 'cancel reset preserves HEAD'
    Answer @('UPDATE'); Reset-SyncToRemote
    Check ((G $a @('rev-parse','HEAD')) -eq (G $a @('rev-parse','origin/main'))) 'reset aligns main'
    Check ((Get-Content (Join-Path $a 'settings.local')) -eq 'secret') 'ignored config preserved'
    $savedBranch = G $a @('for-each-ref','--format=%(refname:short)','refs/heads/backup/sync-*')
    Check ((G $a @('rev-parse',$savedBranch)) -eq $local) 'backup branch retains local commit'
    G $a @('switch',$savedBranch) | Out-Null; G $a @('stash','apply','--index','stash@{0}') | Out-Null
    Check ((Get-Content (Join-Path $a 'code.txt')) -eq 'unstaged') 'stash restores worktree'
    Check ((G $a @('show',':code.txt')) -eq 'staged') 'stash restores index'
    Check (Test-Path (Join-Path $a '新增.txt')) 'stash restores untracked file'
    Reject { Reset-SyncToRemote } 'main'
    G $a @('stash','push','-u') | Out-Null; G $a @('switch','main') | Out-Null
    G $a @('remote','set-url','origin',(Join-Path $scratch 'missing.git')) | Out-Null
    $before = G $a @('rev-parse','HEAD')
    Reject { Reset-SyncToRemote } 'fetch'
    Check ((G $a @('rev-parse','HEAD')) -eq $before) 'offline reset does not move HEAD'
    Set-Content (Join-Path $a 'offline.txt') 'offline'
    Reject { Save-SyncProgress -Push } 'fetch'
    Check ((G $a @('rev-parse','HEAD')) -eq $before) 'offline upload does not commit'
    Answer @('N'); Save-SyncProgress
    Check ((G $a @('rev-parse','HEAD')) -eq $before) 'declining offline save preserves HEAD'
    Answer @('Y','','离线保存','Y'); Save-SyncProgress
    Check ((G $a @('log','-1','--format=%s')) -eq '离线保存') 'offline local save'
    G $a @('remote','set-url','origin',$remote) | Out-Null
    Set-Content (Join-Path $b 'settings.local') 'remote config'; G $b @('add','-f','settings.local') | Out-Null; G $b @('commit','-m','collision') | Out-Null; G $b @('push') | Out-Null
    Reject { Reset-SyncToRemote } '忽略'
    Check ((Get-Content (Join-Path $a 'settings.local')) -eq 'secret') 'ignored collision does not overwrite'
    G $b @('rm','settings.local') | Out-Null; G $b @('commit','-m','remove collision') | Out-Null; G $b @('push') | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $a 'private') | Out-Null
    Set-Content (Join-Path $a 'private/secret.txt') 'private secret'
    Set-Content (Join-Path $b 'private') 'remote file'; G $b @('add','-f','private') | Out-Null; G $b @('commit','-m','directory collision') | Out-Null; G $b @('push') | Out-Null
    Reject { Reset-SyncToRemote } '忽略'
    Check ((Get-Content (Join-Path $a 'private/secret.txt')) -eq 'private secret') 'ignored directory collision preserved'
    G $b @('rm','private') | Out-Null; G $b @('commit','-m','remove directory collision') | Out-Null; G $b @('push') | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $b 'settings.local') | Out-Null
    Set-Content (Join-Path $b 'settings.local/child.txt') 'remote nested file'
    G $b @('add','-f','settings.local/child.txt') | Out-Null; G $b @('commit','-m','reverse collision') | Out-Null; G $b @('push') | Out-Null
    Reject { Reset-SyncToRemote } '忽略'
    Check ((Get-Content (Join-Path $a 'settings.local')) -eq 'secret') 'ignored file blocks remote directory'
    G $b @('rm','-r','settings.local') | Out-Null; G $b @('commit','-m','remove reverse collision') | Out-Null; G $b @('push') | Out-Null
    G $a @('update-index','--assume-unchanged','code.txt') | Out-Null
    Reject { Reset-SyncToRemote } 'assume-unchanged'
    G $a @('update-index','--no-assume-unchanged','code.txt') | Out-Null
    Set-Content (Join-Path $a '.git/MERGE_HEAD') $base
    Reject { Save-SyncProgress } '未完成'
    Remove-Item -LiteralPath (Join-Path $a '.git/MERGE_HEAD')
    Set-Content (Join-Path $a 'hook-test.txt') 'pending'
    [IO.File]::WriteAllText((Join-Path $a '.git/hooks/pre-commit'), "#!/bin/sh`nexit 1`n", (New-Object Text.UTF8Encoding($false)))
    $beforeHook = G $a @('rev-parse','HEAD')
    Answer @('','hook failure','Y'); Reject { Save-SyncProgress } '提交失败'
    Check ((G $a @('rev-parse','HEAD')) -eq $beforeHook) 'hook failure does not create commit'
    Check ((G $a @('diff','--cached','--name-only')) -match 'hook-test.txt') 'hook failure preserves staged data'
    Remove-Item -LiteralPath (Join-Path $a '.git/hooks/pre-commit')
    # A lock blocks saving before any destructive operation.
    Set-Content (Join-Path $a '.git/index.lock') ''
    Reject { Reset-SyncToRemote } 'Git 锁'
    Remove-Item -LiteralPath (Join-Path $a '.git/index.lock')
    Set-Content (Join-Path $a '.git/refs/stash.lock') ''
    $beforeStashFailure = G $a @('rev-parse','HEAD')
    Answer @('UPDATE'); Reject { Reset-SyncToRemote } 'stash'
    Check ((G $a @('rev-parse','HEAD')) -eq $beforeStashFailure) 'stash failure never resets HEAD'
    Check ((Get-Content (Join-Path $a 'hook-test.txt')) -eq 'pending') 'stash failure preserves progress'
    Remove-Item -LiteralPath (Join-Path $a '.git/refs/stash.lock')
    Answer @('UPDATE'); Reset-SyncToRemote
    Check ((G $a @('status','--porcelain')) -eq '') 'final align has clean tracked state'
    Write-Host "SUCCESS: $script:checks checks"
} finally {
    $resolved = [IO.Path]::GetFullPath($scratch)
    $allowed = [IO.Path]::GetFullPath((Join-Path $root 'tmp')) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe cleanup path' }
    $links = @(Get-Item -LiteralPath $scratch) + @(Get-ChildItem -LiteralPath $scratch -Force -Recurse)
    if ($links | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw 'Cleanup refused: reparse point' }
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
