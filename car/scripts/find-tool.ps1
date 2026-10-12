function Find-AutocarTool([string]$Name,[string]$Explicit='') {
    if ($Explicit) {
        if (-not(Test-Path -LiteralPath $Explicit -PathType Leaf)){throw "Tool not found: $Explicit"}
        return [IO.Path]::GetFullPath($Explicit)
    }
    $variable=if($Name -eq 'Keil'){'AUTOCAR_KEIL'}else{'AUTOCAR_HOST_CC'}
    $value=[Environment]::GetEnvironmentVariable($variable)
    if($value){return Find-AutocarTool $Name $value}
    $local=Join-Path $PSScriptRoot 'tool_paths.local.json'
    if(Test-Path -LiteralPath $local){$settings=Get-Content -LiteralPath $local -Raw|ConvertFrom-Json;if($settings.$Name){return Find-AutocarTool $Name $settings.$Name}}
    if($Name -eq 'Gcc') {
        $bundled=Join-Path $PSScriptRoot '../../simulation/toolchain/mingw64/bin/gcc.exe'
        if(Test-Path -LiteralPath $bundled){return [IO.Path]::GetFullPath($bundled)}
    }
    $command=Get-Command $(if($Name -eq 'Keil'){'UV4.exe'}else{'gcc.exe'}) -ErrorAction SilentlyContinue
    if($command){return $command.Source}
    foreach($drive in Get-PSDrive -PSProvider FileSystem){
        $patterns=if($Name -eq 'Keil'){@('Keil_v5/UV4/UV4.exe')}else{@('msys64/ucrt64/bin/gcc.exe','Xilinx/Vivado/*/tps/mingw/*/win64.o/nt/bin/gcc.exe')}
        foreach($pattern in $patterns){foreach($path in Get-ChildItem -Path (Join-Path $drive.Root $pattern) -File -ErrorAction SilentlyContinue){return $path.FullName}}
    }
    throw "No usable $Name found; supply an explicit path or $variable."
}
