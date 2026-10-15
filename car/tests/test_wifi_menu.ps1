param([string]$TaskRoot=(Join-Path $PSScriptRoot '../../tmp/wifi-entry'))
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../../wifi/wifi-menu.ps1')
$task=Join-Path $TaskRoot ('menu-tests-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $task -Force | Out-Null
$target=Join-Path $task 'local.h'
$backups=Join-Path $task 'backups'
$script:checks=0
function Assert-Wifi([bool]$Condition,[string]$Message) {
    if(-not $Condition){throw $Message}
    $script:checks++
}
function Expect-WifiFailure([scriptblock]$Action) {
    $failed=$false
    try {& $Action | Out-Null} catch {$failed=$true}
    Assert-Wifi $failed 'Expected failure did not occur.'
}
$values=@{Name='Test';Secret='test-password';Address='192.168.137.1';Port=8080}
$cancel={param($prompt) $false};$accept={param($prompt) $true}
$result=Invoke-ConfirmedWifiUpdate $values $target $backups $cancel
Assert-Wifi $result.Cancelled 'Cancellation not reported.'
Assert-Wifi (-not(Test-Path -LiteralPath $target)) 'Cancel created configuration.'
Assert-Wifi (-not(Test-Path -LiteralPath $backups)) 'Cancel created backup directory.'
$invalid=$values.Clone();$invalid.Address='0.0.0.0'
Expect-WifiFailure {Invoke-ConfirmedWifiUpdate $invalid $target $backups $accept}
Assert-Wifi (-not(Test-Path -LiteralPath $target)) 'Invalid values created configuration.'
$result=Invoke-ConfirmedWifiUpdate $values $target $backups $accept
Assert-Wifi $result.Changed 'New configuration not written.'
$before=(Get-FileHash -LiteralPath $target).Hash
$time=(Get-Item -LiteralPath $target).LastWriteTimeUtc
$result=Invoke-ConfirmedWifiUpdate $values $target $backups $accept
Assert-Wifi (-not $result.Changed) 'Identical values rewritten.'
Assert-Wifi ((Get-Item -LiteralPath $target).LastWriteTimeUtc -eq $time) 'Identical write changed timestamp.'
Assert-Wifi (-not(Test-Path -LiteralPath $backups)) 'Identical write created backup.'
$replacement=$values.Clone();$replacement.Name='Replacement'
$result=Invoke-ConfirmedWifiUpdate $replacement $target $backups $accept
Assert-Wifi $result.Changed 'Replacement not written.'
Assert-Wifi ((Get-FileHash -LiteralPath $result.BackupPath).Hash -eq $before) 'Backup does not match original.'
$before=(Get-FileHash -LiteralPath $target).Hash
$blocked=Join-Path $task 'blocked-backup-root'
[IO.File]::WriteAllText($blocked,'not a directory')
Expect-WifiFailure {Invoke-ConfirmedWifiUpdate $values $target $blocked $accept}
Assert-Wifi ((Get-FileHash -LiteralPath $target).Hash -eq $before) 'Backup failure changed original.'

$reader=${function:Get-Hotspot};$candidates=${function:Get-HotspotAddressCandidates}
$prompt=${function:Read-Host}
try {
    function Get-Hotspot {throw 'Mock hotspot API failure.'}
    function Get-HotspotAddressCandidates {return @()}
    $status=Get-WifiStatus -Target $target
    Assert-Wifi (-not $status.HotspotReadable) 'Read failure reported as readable.'
    Assert-Wifi ($null -eq $status.State) 'Read failure misreported as Off.'
    Assert-Wifi ($status.ApiError -eq 'Mock hotspot API failure.') 'Read failure lost diagnosis.'
    function Get-Hotspot {return @{Ssid='Test';Password='test-password';Band='TwoPointFourGigahertz';State='On'}}
    $items=@(
        [pscustomobject]@{InterfaceName='First';InterfaceIndex=11;IPAddress='192.168.137.1';Source='WiFiDirect'},
        [pscustomobject]@{InterfaceName='Second';InterfaceIndex=12;IPAddress='192.168.138.1';Source='WiFiDirect'}
    )
    function Get-HotspotAddressCandidates {return $items}
    $status=Get-WifiStatus -Target $target
    Assert-Wifi ($status.Candidates.Count -eq 2) 'Candidates lost interface metadata.'
    Assert-Wifi ($null -eq $status.ServerIP) 'Ambiguous IP silently selected.'
    Assert-Wifi ($null -eq $status.PSObject.Properties['Password']) 'Status exposes password.'
    Assert-Wifi (($status | ConvertTo-Json -Depth 5) -notmatch 'test-password') 'Status serialization exposes password.'
    function Read-Host {return '2'}
    Assert-Wifi ((Select-WifiAddress $items) -eq '192.168.138.1') 'Explicit candidate not selected.'
    function Read-Host {return ''}
    Assert-Wifi ($null -eq (Select-WifiAddress $items)) 'Blank selection not cancelled.'
    function Read-Host {return '9'}
    Expect-WifiFailure {Select-WifiAddress $items}
    function Read-Host {return '70000'}
    Expect-WifiFailure {Read-WifiPort}
    function Read-Host {return 'invalid'}
    Expect-WifiFailure {Read-WifiPort}
    $fingerprint=Get-WifiFingerprint (Get-Hotspot) $items
    function Get-Hotspot {return @{Ssid='Changed';Password='test-password';Band='TwoPointFourGigahertz';State='On'}}
    Expect-WifiFailure {Invoke-ConfirmedWifiUpdate $values $target $backups $accept $fingerprint}
    Assert-Wifi ((Get-FileHash -LiteralPath $target).Hash -eq $before) 'Hotspot drift overwrote original.'
    function Get-Hotspot {return @{Ssid='Test';Password='test-password';Band='TwoPointFourGigahertz';State='On'}}
    $changedAddress=@([pscustomobject]@{InterfaceName='First';InterfaceIndex=11;IPAddress='192.168.139.1';Source='WiFiDirect'})
    function Get-HotspotAddressCandidates {return $changedAddress}
    Expect-WifiFailure {Invoke-ConfirmedWifiUpdate $values $target $backups $accept $fingerprint}
    Assert-Wifi ((Get-FileHash -LiteralPath $target).Hash -eq $before) 'Address drift overwrote original.'
    function Get-HotspotAddressCandidates {return $items}
    $result=Invoke-ConfirmedWifiUpdate $values $target $backups $accept $fingerprint
    Assert-Wifi $result.Changed 'Stable hotspot update rejected.'
    $open=$values.Clone();$open.Secret=''
    $script:prompts=0
    $openCancel={param($text) $script:prompts++;$false}
    $result=Invoke-ConfirmedWifiUpdate $open $target $backups $openCancel
    Assert-Wifi ($result.Cancelled -and $script:prompts -eq 1) 'Open network bypassed extra confirmation.'
    $script:prompts=0
    $openAccept={param($text) $script:prompts++;$true}
    $result=Invoke-ConfirmedWifiUpdate $open $target $backups $openAccept
    Assert-Wifi ($result.Changed -and $script:prompts -eq 2) 'Open network missing two confirmations.'

    $ConfigPath=Join-Path $task 'menu-flow.h';$ConfigBackups=$backups
    $script:TestNetwork=@{Ssid='Test';Password='test-password';Band='TwoPointFourGigahertz';State='On'}
    function Get-Hotspot {return $script:TestNetwork}
    function Read-Host {
        param([string]$Prompt,[switch]$AsSecureString)
        if(-not $script:Answers.Count){throw 'Menu requested unexpected input.'}
        $answer=$script:Answers.Dequeue()
        if($AsSecureString){return ConvertTo-SecureString $answer -AsPlainText -Force}
        return $answer
    }
    function Set-TestAnswers([string[]]$Answers) {
        $script:Answers=New-Object 'System.Collections.Generic.Queue[string]'
        foreach($answer in $Answers){$script:Answers.Enqueue($answer)}
    }
    foreach($band in @('FiveGigahertz','Unknown')) {
        $script:TestNetwork.Band=$band;Set-TestAnswers @('2','0')
        Start-WifiMenu
        Assert-Wifi (-not(Test-Path -LiteralPath $ConfigPath)) 'Unsupported band wrote menu target.'
        Assert-Wifi ($script:Answers.Count -eq 0) 'Unsupported band did not return to menu.'
    }
    $script:TestNetwork.Band='TwoPointFourGigahertz';$script:TestNetwork.State='Off'
    Set-TestAnswers @('2','0');Start-WifiMenu
    Assert-Wifi (-not(Test-Path -LiteralPath $ConfigPath)) 'Inactive hotspot wrote menu target.'
    $script:TestNetwork.State='On'
    Set-TestAnswers @('2','2','','n','0');Start-WifiMenu
    Assert-Wifi (-not(Test-Path -LiteralPath $ConfigPath)) 'Menu cancellation wrote target.'
    Set-TestAnswers @('2','1','','Y','0');Start-WifiMenu
    Assert-Wifi (Test-Path -LiteralPath $ConfigPath) 'Automatic menu update failed.'
    Assert-Wifi ([IO.File]::ReadAllText($ConfigPath) -match '192.168.137.1') 'Automatic menu address mismatch.'
    $flowHash=(Get-FileHash -LiteralPath $ConfigPath).Hash
    Set-TestAnswers @('3','ManualTest','manual-password','2','9000','Y','0');Start-WifiMenu
    Assert-Wifi ([IO.File]::ReadAllText($ConfigPath) -match '"ManualTest"') 'Manual menu SSID mismatch.'
    Assert-Wifi ([IO.File]::ReadAllText($ConfigPath) -match '"9000"') 'Manual menu port mismatch.'
    Assert-Wifi (@(Get-ChildItem $backups -Recurse -File | Where-Object {(Get-FileHash -LiteralPath $_.FullName).Hash -eq $flowHash}).Count -gt 0) 'Manual menu update did not preserve previous configuration.'
    $flowHash=(Get-FileHash -LiteralPath $ConfigPath).Hash
    $count=@(Get-ChildItem -LiteralPath $task -Recurse -File).Count
    $StatusOnly=$true;Start-WifiMenu;$StatusOnly=$false
    Assert-Wifi ((Get-FileHash -LiteralPath $ConfigPath).Hash -eq $flowHash) 'Read-only entry changed target.'
    Assert-Wifi (@(Get-ChildItem -LiteralPath $task -Recurse -File).Count -eq $count) 'Read-only entry wrote files.'
} finally {
    Set-Item Function:Get-Hotspot -Value $reader
    Set-Item Function:Get-HotspotAddressCandidates -Value $candidates
    if($prompt){Set-Item Function:Read-Host -Value $prompt}else{Remove-Item Function:Read-Host -ErrorAction SilentlyContinue}
}
Assert-Wifi (@(Get-ChildItem -LiteralPath $task -File -Filter '*.tmp').Count -eq 0) 'Atomic write left temporary files.'
Assert-Wifi (@(Get-ChildItem -LiteralPath $task -File -Filter '*.old').Count -eq 0) 'Atomic write left old staging files.'
Write-Output ('Wi-Fi menu: '+$script:checks+' checks passed; real local configuration was not used.')
