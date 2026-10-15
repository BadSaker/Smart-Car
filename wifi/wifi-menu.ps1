# ASCII source and escaped UI text work in Windows PowerShell 5.1.
param(
    [switch]$StatusOnly,
    [string]$ConfigPath=(Join-Path $PSScriptRoot 'camera_debug_local.h'),
    [string]$ConfigBackups=(Join-Path $PSScriptRoot '../outputs/backups'),
    [ValidateRange(1,65535)][int]$Port=8080
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'configure-wifi.ps1') -OutputPath $ConfigPath -ServerPort $Port -BackupRoot $ConfigBackups
$script:WifiMenuText=ConvertFrom-Json @'
{
  "title": "\u70ed\u70b9\u914d\u7f6e",
  "read": "\u91cd\u65b0\u8bfb\u53d6\u70ed\u70b9\u4e0e\u8fde\u63a5\u72b6\u6001",
  "auto": "\u4f7f\u7528\u5f53\u524d\u7535\u8111\u70ed\u70b9\u66f4\u65b0\u672c\u5730\u914d\u7f6e",
  "manual": "\u624b\u52a8\u8f93\u5165\u70ed\u70b9\u914d\u7f6e",
  "exit": "\u9000\u51fa",
  "choice": "\u9009\u62e9",
  "unreadable": "\u65e0\u6cd5\u8bfb\u53d6\u70ed\u70b9\u4fe1\u606f\uff1b\u53ef\u68c0\u67e5 Windows \u8bbe\u7f6e\u6216\u4f7f\u7528\u624b\u52a8\u914d\u7f6e\u3002",
  "state": "\u70ed\u70b9\u72b6\u6001",
  "band": "\u914d\u7f6e\u9891\u6bb5",
  "password": "\u5bc6\u7801\u957f\u5ea6\uff08\u4e0d\u663e\u793a\u5bc6\u7801\uff09",
  "candidates": "\u70ed\u70b9 IPv4 \u5019\u9009",
  "none": "\u65e0\u53ef\u7528\u5019\u9009",
  "listeners": "TCP \u76d1\u542c",
  "config": "\u672c\u5730\u914d\u7f6e\u5df2\u5b58\u5728",
  "port": "\u76ee\u6807\u7aef\u53e3",
  "ipChoice": "\u9009\u62e9\u5730\u5740\u5e8f\u53f7\uff1b\u8f93\u5165 0 \u624b\u52a8\u8f93\u5165\uff0c\u7a7a\u767d\u53d6\u6d88",
  "ipInput": "\u76ee\u6807 IPv4\uff08\u5fc5\u987b\u662f\u5f53\u524d\u7535\u8111\u53ef\u8bbf\u95ee\u5730\u5740\uff09",
  "portInput": "\u76ee\u6807\u7aef\u53e3\uff08\u56de\u8f66\u4f7f\u7528\u9ed8\u8ba4\u503c\uff09",
  "ssidInput": "\u70ed\u70b9 SSID\uff08ASCII\uff09",
  "passwordInput": "\u70ed\u70b9\u5bc6\u7801\uff08\u9690\u85cf\u8f93\u5165\uff1b\u5f00\u653e\u7f51\u7edc\u76f4\u63a5\u56de\u8f66\uff09",
  "invalid": "\u8f93\u5165\u65e0\u6548\uff0c\u64cd\u4f5c\u53d6\u6d88\u3002",
  "cancelled": "\u5df2\u53d6\u6d88\uff0c\u914d\u7f6e\u672a\u4fee\u6539\u3002",
  "confirm": "\u786e\u8ba4\u5199\u5165\u672c\u5730\u914d\u7f6e\uff1f\u8f93\u5165 Y \u786e\u8ba4\uff0c\u5176\u4ed6\u8f93\u5165\u53d6\u6d88",
  "open": "\u8fd9\u662f\u5f00\u653e\u7f51\u7edc\uff0c\u6ca1\u6709\u8fde\u63a5\u5bc6\u7801\u3002\u786e\u8ba4\u7ee7\u7eed\uff1f\u8f93\u5165 Y",
  "changed": "\u672c\u5730\u914d\u7f6e\u5df2\u66f4\u65b0\u3002",
  "unchanged": "\u5185\u5bb9\u76f8\u540c\uff0c\u65e0\u9700\u66f4\u65b0\u3002",
  "backup": "\u65e7\u914d\u7f6e\u5907\u4efd",
  "boundary": "\u4ec5\u66f4\u65b0\u672c\u673a\u914d\u7f6e\uff1b\u8f66\u4e0a\u56fa\u4ef6\u672a\u6539\u53d8\u3002\u9700\u8981\u53e6\u884c\u7f16\u8bd1\u5e76\u7ecf\u6388\u6743\u5237\u5199\u3002",
  "failure": "\u64cd\u4f5c\u5931\u8d25\uff0c\u672a\u7ee7\u7eed\u66f4\u65b0\uff1a",
  "inactive": "\u70ed\u70b9\u672a\u5f00\u542f\u3002\u8bf7\u5728 Windows \u8bbe\u7f6e\u4e2d\u5f00\u542f\uff0c\u6216\u4f7f\u7528\u624b\u52a8\u914d\u7f6e\u3002",
  "unknownBand": "\u65e0\u6cd5\u786e\u8ba4\u914d\u7f6e\u4e3a 2.4 GHz\u3002\u8bf7\u68c0\u67e5 Windows \u70ed\u70b9\u8bbe\u7f6e\uff0c\u6216\u786e\u8ba4\u540e\u4f7f\u7528\u624b\u52a8\u914d\u7f6e\u3002",
  "stale": "\u70ed\u70b9\u4fe1\u606f\u6216\u63a5\u53e3\u5730\u5740\u5df2\u53d8\u5316\uff0c\u8bf7\u91cd\u65b0\u8bfb\u53d6\u5e76\u786e\u8ba4\u3002",
  "summary": "\u51c6\u5907\u5199\u5165",
  "unknown": "\u672a\u77e5",
  "readonly": "\u53ea\u8bfb\u68c0\u67e5\u5b8c\u6210\uff0c\u672a\u5199\u5165\u6587\u4ef6\u3002",
  "noListener": "\u672a\u68c0\u6d4b\u5230\u8be5\u7aef\u53e3\u76d1\u542c\uff1b\u76d1\u542c\u4e5f\u4e0d\u4ee3\u8868\u5c0f\u8f66\u5df2\u8fde\u63a5\u6216\u5df2\u6536\u56fe\u3002",
  "manualBand": "\u624b\u52a8\u914d\u7f6e\u524d\uff0c\u8bf7\u786e\u8ba4\u76ee\u6807\u70ed\u70b9\u5b9e\u9645\u5de5\u4f5c\u5728 2.4 GHz\u3002"
}
'@

function Get-WifiFingerprint($Network,$Candidates) {
    # Include credentials only inside the hash, never in display or logs.
    $value=[ordered]@{Ssid=$Network.Ssid;Password=$Network.Password;Band=$Network.Band;State=$Network.State;Addresses=@($Candidates | Sort-Object InterfaceIndex,IPAddress | Select-Object InterfaceIndex,IPAddress,Source)}
    $bytes=[Text.Encoding]::UTF8.GetBytes(($value | ConvertTo-Json -Depth 5 -Compress))
    $hash=[Security.Cryptography.SHA256]::Create()
    try {return [BitConverter]::ToString($hash.ComputeHash($bytes))} finally {$hash.Dispose()}
}
function Show-WifiStatus($Status) {
    $t=$script:WifiMenuText
    Write-Host ('--- '+$t.title+' ---')
    if($Status.HotspotReadable) {
        Write-Host ('SSID: '+$Status.Ssid)
        Write-Host ($t.state+': '+$Status.State)
        Write-Host ($t.band+': '+$Status.Band)
        Write-Host ($t.password+': '+$Status.PasswordLength)
    } else {Write-Host $t.unreadable;Write-Host ($t.state+': '+$t.unknown)}
    Write-Host ($t.candidates+':')
    if(@($Status.Candidates).Count) {
        foreach($item in $Status.Candidates){Write-Host ('  '+$item.InterfaceName+' [#'+$item.InterfaceIndex+'] '+$item.IPAddress+' ('+$item.Source+')')}
    } else {Write-Host ('  '+$t.none)}
    Write-Host ($t.port+': '+$Status.Port)
    if($Status.ListenerError){Write-Host ($t.listeners+': '+$t.unknown)}
    elseif(@($Status.Listeners).Count){foreach($item in $Status.Listeners){Write-Host ($t.listeners+': '+$item.LocalAddress+':'+$item.LocalPort)}}
    else{Write-Host ($t.listeners+': '+$t.noListener)}
    Write-Host ($t.config+': '+$Status.LocalConfigExists)
}
function Select-WifiAddress($Candidates,[switch]$ManualInput) {
    $t=$script:WifiMenuText;$items=@($Candidates)
    if($items.Count -eq 1 -and -not $ManualInput){return $items[0].IPAddress}
    for($i=0;$i -lt $items.Count;$i++){Write-Host (('{0}. {1} [#{2}] {3}' -f ($i+1),$items[$i].InterfaceName,$items[$i].InterfaceIndex,$items[$i].IPAddress))}
    if(-not $items.Count){return (Read-Host $t.ipInput).Trim()}
    $raw=Read-Host $t.ipChoice
    if(-not $raw){return $null}
    $index=0
    if(-not [int]::TryParse($raw,[ref]$index) -or $index -lt 0 -or $index -gt $items.Count){throw $t.invalid}
    if($index -eq 0){return (Read-Host $t.ipInput).Trim()}
    return $items[$index-1].IPAddress
}
function Read-WifiPort([int]$Default=8080) {
    $raw=Read-Host ($script:WifiMenuText.portInput+' ['+$Default+']')
    if(-not $raw){return $Default}
    $number=0
    if(-not [int]::TryParse($raw,[ref]$number) -or $number -lt 1 -or $number -gt 65535){throw $script:WifiMenuText.invalid}
    return $number
}
function Invoke-ConfirmedWifiUpdate([hashtable]$Values,[string]$TargetPath,[string]$Backups,[scriptblock]$Confirm,[string]$ExpectedFingerprint='') {
    $t=$script:WifiMenuText
    Test-WifiValues $Values.Name $Values.Secret $Values.Address $Values.Port
    Write-Host ($t.summary+': SSID='+$Values.Name+'; IPv4='+$Values.Address+'; '+$t.port+'='+$Values.Port+'; '+$t.password+'='+$Values.Secret.Length)
    if(($Values.Secret.Length -eq 0 -and -not (& $Confirm $t.open)) -or -not (& $Confirm $t.confirm)){
        return [pscustomobject]@{Cancelled=$true;Changed=$false;BackupPath=$null}
    }
    if($ExpectedFingerprint) {
        $current=Get-Hotspot
        $currentCandidates=@(Get-HotspotAddressCandidates)
        if((Get-WifiFingerprint $current $currentCandidates) -ne $ExpectedFingerprint){throw $t.stale}
    }
    $result=Write-WifiLocalConfiguration -Name $Values.Name -Secret $Values.Secret -Address $Values.Address -Port $Values.Port -Target $TargetPath -Backups $Backups
    $result | Add-Member -NotePropertyName Cancelled -NotePropertyValue $false
    return $result
}
function Start-WifiMenu {
    $t=$script:WifiMenuText
    $status=Get-WifiStatus -Port $Port -Target $ConfigPath
    Show-WifiStatus $status
    if($StatusOnly){Write-Host $t.readonly;return}
    while($true) {
        Write-Host ''
        Write-Host ('1. '+$t.read);Write-Host ('2. '+$t.auto);Write-Host ('3. '+$t.manual);Write-Host ('0. '+$t.exit)
        $choice=Read-Host $t.choice
        if($choice -eq '0'){return}
        try {
            if($choice -eq '1'){$status=Get-WifiStatus -Port $Port -Target $ConfigPath;Show-WifiStatus $status;continue}
            if($choice -notin @('2','3')){Write-Host $t.invalid;continue}
            $expected='';$values=@{}
            if($choice -eq '2') {
                $network=Get-Hotspot
                if($network.State -ne 'On'){throw $t.inactive}
                if($network.Band -ne 'TwoPointFourGigahertz'){throw $t.unknownBand}
                $addresses=@(Get-HotspotAddressCandidates)
                $values=@{Name=$network.Ssid;Secret=[string]$network.Password}
                $expected=Get-WifiFingerprint $network $addresses
                $values.Address=Select-WifiAddress $addresses
            } else {
                Write-Host $t.manualBand
                $values.Name=Read-Host $t.ssidInput
                $secure=Read-Host $t.passwordInput -AsSecureString
                $pointer=[Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
                try {$values.Secret=[Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)}
                finally {[Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer);$secure.Dispose()}
                $status=Get-WifiStatus -Port $Port -Target $ConfigPath
                $values.Address=Select-WifiAddress $status.Candidates -ManualInput
            }
            if(-not $values.Address){Write-Host $t.cancelled;continue}
            $values.Port=Read-WifiPort $Port
            $confirm={param($prompt) (Read-Host $prompt) -ieq 'Y'}
            $result=Invoke-ConfirmedWifiUpdate -Values $values -TargetPath $ConfigPath -Backups $ConfigBackups -Confirm $confirm -ExpectedFingerprint $expected
            if($result.Cancelled){Write-Host $t.cancelled}
            elseif($result.Changed){Write-Host $t.changed;if($result.BackupPath){Write-Host ($t.backup+': '+$result.BackupPath)};Write-Host $t.boundary}
            else{Write-Host $t.unchanged;Write-Host $t.boundary}
        } catch {Write-Warning ($t.failure+$_.Exception.Message)}
        finally {if($values){$values.Secret=''};$network=$null}
    }
}
if($MyInvocation.InvocationName -ne '.') {Start-WifiMenu}

