# ASCII script text is compatible with Windows PowerShell 5.1.
[CmdletBinding(DefaultParameterSetName='Check')]
param(
    [Parameter(ParameterSetName='Check')][switch]$CheckOnly,
    [Parameter(Mandatory,ParameterSetName='Auto')][switch]$Auto,
    [Parameter(Mandatory,ParameterSetName='Manual')][switch]$Manual,
    [string]$Ssid,
    [Security.SecureString]$Password,
    [string]$ServerIP,
    [int]$InterfaceIndex,
    [ValidateRange(1,65535)][int]$ServerPort=8080,
    [string]$OutputPath=(Join-Path $PSScriptRoot '../config/camera_debug_local.h')
)
$ErrorActionPreference='Stop'
function Test-WifiValues([string]$Name,[string]$Secret,[string]$Address,[int]$Port) {
    if ($Name -notmatch '^[\x20-\x7e]{1,32}$') { throw 'SSID must contain 1..32 printable ASCII bytes.' }
    if ($Secret -notmatch '^[\x20-\x7e]*$' -or ($Secret.Length -ne 0 -and $Secret.Length -lt 8)) { throw 'Password must be empty or contain at least 8 printable ASCII bytes.' }
    if ($Name.Length+$Secret.Length -gt 59) { throw 'SSID and password together exceed the driver limit of 59 bytes.' }
    if ($Address -notmatch '^(\d{1,3}\.){3}\d{1,3}$' -or @($Address.Split('.') | Where-Object {[int]$_ -gt 255}).Count -or $Address -in @('0.0.0.0','255.255.255.255') -or $Address.StartsWith('127.')) { throw 'ServerIP must be a reachable unicast IPv4 address.' }
    if ([int]$Address.Split('.')[0] -ge 224) { throw 'Multicast/reserved server addresses are not supported.' }
    if ($Port -lt 1 -or $Port -gt 65535) { throw 'Invalid server port.' }
}
function ConvertTo-CString([string]$Value) { return $Value.Replace('\','\\').Replace('"','\"') }
function Get-Hotspot {
    if ($PSVersionTable.PSEdition -eq 'Core') {
        $code='function Get-Hotspot {'+${function:Get-Hotspot}.ToString()+'}; Get-Hotspot | ConvertTo-Json -Compress'
        $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($code))
        $response=& "$env:SystemRoot/System32/WindowsPowerShell/v1.0/powershell.exe" -NoProfile -NonInteractive -EncodedCommand $encoded 2>$null
        if ($LASTEXITCODE -ne 0 -or -not $response) {throw 'Windows hotspot API failed. Use -Manual.'}
        $value=$response|ConvertFrom-Json
        return @{Ssid=$value.Ssid;Password=$value.Password;Band=$value.Band;State=$value.State}
    }
    $profile=[Windows.Networking.Connectivity.NetworkInformation,Windows.Networking.Connectivity,ContentType=WindowsRuntime]::GetInternetConnectionProfile()
    if (-not $profile) { throw 'No Internet connection profile. Use -Manual.' }
    $manager=[Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager,Windows.Networking.NetworkOperators,ContentType=WindowsRuntime]::CreateFromConnectionProfile($profile)
    $config=$manager.GetCurrentAccessPointConfiguration()
    $band='Unknown'
    try {$band=$config.Band.ToString()} catch {}
    return @{Ssid=$config.Ssid;Password=$config.Passphrase;Band=$band;State=$manager.TetheringOperationalState.ToString()}
}
function Get-ServerAddress {
    if ($ServerIP) { return $ServerIP }
    # Direct .NET enumeration works without the network-management CIM permission.
    $interfaces=@([Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces() | Where-Object {$_.OperationalStatus -eq 'Up' -and $_.Supports([Net.NetworkInformation.NetworkInterfaceComponent]::IPv4)})
    $direct=@(foreach($adapter in $interfaces) {
        $properties=$adapter.GetIPProperties()
        $selected=if($InterfaceIndex){$properties.GetIPv4Properties().Index -eq $InterfaceIndex}else{$adapter.Description -match 'Wi-Fi Direct|WiFi Direct'}
        if($selected){foreach($entry in $properties.UnicastAddresses){if($entry.Address.AddressFamily -eq 'InterNetwork' -and $entry.Address.ToString() -notlike '169.254.*'){[pscustomobject]@{IPAddress=$entry.Address.ToString()}}}}
    })
    if($direct.Count -eq 1){return $direct[0].IPAddress}
    if($direct.Count -gt 1){throw 'Multiple hotspot candidates; supply -ServerIP or -InterfaceIndex.'}
    if ($InterfaceIndex) {
        $addresses=@(Get-NetIPAddress -InterfaceIndex $InterfaceIndex -AddressFamily IPv4 | Where-Object {$_.IPAddress -notlike '169.254.*'})
    } else {
        # Use ICS identity, never translated adapter names or the usual subnet alone.
        $sharing=New-Object -ComObject HNetCfg.HNetShare
        $private=@(foreach($connection in $sharing.EnumEveryConnection()) {
            $configuration=$sharing.INetSharingConfigurationForINetConnection($connection)
            if ($configuration.SharingEnabled -and $configuration.SharingConnectionType -eq 1) {
                $properties=$sharing.NetConnectionProps($connection)
                Get-NetAdapter -IncludeHidden | Where-Object {$_.InterfaceGuid.ToString().Trim('{}') -eq $properties.Guid.Trim('{}')}
            }
        })
        $addresses=@(foreach($adapter in $private) {Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 | Where-Object {$_.IPAddress -notlike '169.254.*'}})
    }
    if ($addresses.Count -ne 1) { throw 'Cannot uniquely identify hotspot IPv4. Supply -ServerIP or -InterfaceIndex.' }
    return $addresses[0].IPAddress
}
function Invoke-WifiConfiguration {
    $secret='';$network=$null;$apiError=$null
    if (-not $Manual) {try {$network=Get-Hotspot} catch {$apiError=$_.Exception.Message}}
    if ($PSCmdlet.ParameterSetName -eq 'Check') {
        $address=$null;$addressError=$null
        try {$address=Get-ServerAddress} catch {$addressError=$_.Exception.Message}
        $listeners=@([Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners() | Where-Object {$_.Port -eq $ServerPort} | ForEach-Object {[pscustomobject]@{LocalAddress=$_.Address.ToString();LocalPort=$_.Port}})
        [pscustomobject]@{HotspotReadable=($null -ne $network);Ssid=$network.Ssid;PasswordLength=$network.Password.Length;Band=$network.Band;State=$network.State;ApiError=$apiError;ServerIP=$address;AddressError=$addressError;Port=$ServerPort;Listeners=$listeners;LocalConfigExists=(Test-Path -LiteralPath $OutputPath)}
        return
    }
    if ($Auto) {
        if (-not $network) { throw "Hotspot read failed: $apiError" }
        if ($network.State -ne 'On') {throw 'Hotspot is not active. Enable it in Windows or use -Manual with an explicit address.'}
        if ($network.Band -eq 'FiveGigahertz') { throw 'Select 2.4 GHz in Windows hotspot settings first.' }
        if ($network.Band -ne 'TwoPointFourGigahertz') { Write-Warning 'Actual 2.4 GHz operation must be checked in Windows settings.' }
        $Ssid=$network.Ssid;$secret=$network.Password
    } else {
        if (-not $Ssid) {$Ssid=Read-Host 'Hotspot SSID (ASCII)'}
        if (-not $Password) {$Password=Read-Host 'Hotspot password' -AsSecureString}
        $pointer=[Runtime.InteropServices.Marshal]::SecureStringToBSTR($Password)
        try {$secret=[Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)} finally {[Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)}
    }
    $address=Get-ServerAddress
    Test-WifiValues $Ssid $secret $address $ServerPort
    $content=@"
#ifndef AUTOCAR_CAMERA_DEBUG_LOCAL_H
#define AUTOCAR_CAMERA_DEBUG_LOCAL_H
#define CAMERA_DEBUG_WIFI_SSID "$(ConvertTo-CString $Ssid)"
#define CAMERA_DEBUG_WIFI_PASSWORD "$(ConvertTo-CString $secret)"
#define CAMERA_DEBUG_WIFI_SERVER_IP "$address"
#define CAMERA_DEBUG_WIFI_SERVER_PORT "$ServerPort"
#define CAMERA_DEBUG_WIFI_LOCAL_PORT "0"
#endif
"@
    $target=[IO.Path]::GetFullPath($OutputPath)
    if (-not (Test-Path -LiteralPath (Split-Path $target -Parent))) {throw 'Output directory does not exist.'}
    if ((Test-Path -LiteralPath $target) -and [IO.File]::ReadAllText($target) -eq $content) {Write-Output 'Configuration unchanged.';return}
    $staged=$target+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    try {
        [IO.File]::WriteAllText($staged,$content,(New-Object Text.UTF8Encoding($false)))
        if (Test-Path -LiteralPath $target) {[IO.File]::Replace($staged,$target,($staged+'.old'))} else {[IO.File]::Move($staged,$target)}
    } finally {foreach($temporary in @($staged,($staged+'.old'))){if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary}}}
    Write-Output "Local configuration generated. Server=$address`:$ServerPort; password length=$($secret.Length)."
}
if ($MyInvocation.InvocationName -ne '.') {Invoke-WifiConfiguration}
