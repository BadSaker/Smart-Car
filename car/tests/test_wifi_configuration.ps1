param([string]$TaskRoot=(Join-Path $PSScriptRoot '../../tmp/wifi-entry'))
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../../wifi/configure-wifi.ps1')
$cases=@(
    @('Test','test-password','192.168.137.1',8080,$true),
    @('','test-password','192.168.137.1',8080,$false),
    @(('x'*32),('y'*27),'192.168.137.1',8080,$true),
    @(('x'*32),('y'*28),'192.168.137.1',8080,$false),
    @('Test',"bad`npassword",'192.168.137.1',8080,$false),
    @('Test','password','0.0.0.0',8080,$false),
    @('Test','password','999.1.2.3',8080,$false),
    @('Test','password','224.1.2.3',8080,$false)
)
foreach($case in $cases){$accepted=$true;try{Test-WifiValues $case[0] $case[1] $case[2] $case[3]}catch{$accepted=$false};if($accepted -ne $case[4]){throw 'Validation boundary failed.'}}
if((ConvertTo-CString 'a"b\c') -cne 'a\"b\\c'){throw 'C escaping failed.'}
$task=Join-Path $TaskRoot 'wifi-tests'
New-Item -ItemType Directory -Path $task -Force | Out-Null
$target=Join-Path $task 'local.h'
$secret=ConvertTo-SecureString 'test-password' -AsPlainText -Force
& (Join-Path $PSScriptRoot '../../wifi/configure-wifi.ps1') -Manual -Ssid 'Test' -Password $secret -ServerIP '192.168.137.1' -OutputPath $target -BackupRoot (Join-Path $task 'backups')
$before=(Get-Item -LiteralPath $target).LastWriteTimeUtc
& (Join-Path $PSScriptRoot '../../wifi/configure-wifi.ps1') -Manual -Ssid 'Test' -Password $secret -ServerIP '192.168.137.1' -OutputPath $target -BackupRoot (Join-Path $task 'backups')
if((Get-Item -LiteralPath $target).LastWriteTimeUtc -ne $before){throw 'Unchanged configuration was rewritten.'}
& (Join-Path $PSScriptRoot '../../wifi/configure-wifi.ps1') -Manual -Ssid 'Test2' -Password $secret -ServerIP '192.168.137.1' -OutputPath $target -BackupRoot (Join-Path $task 'backups')
if([IO.File]::ReadAllText($target) -notmatch '"Test2"'){throw 'Existing configuration was not replaced.'}
$hash=(Get-FileHash -LiteralPath $target).Hash
try{& (Join-Path $PSScriptRoot '../../wifi/configure-wifi.ps1') -Manual -Ssid 'Test' -Password $secret -ServerIP '0.0.0.0' -OutputPath $target;throw 'Invalid address accepted.'}catch{if($_.Exception.Message -eq 'Invalid address accepted.'){throw}}
if((Get-FileHash -LiteralPath $target).Hash -ne $hash){throw 'Failure changed previous configuration.'}
Write-Output 'Wi-Fi configuration: 8 boundaries, escaping, unchanged write and failure preservation passed.'
