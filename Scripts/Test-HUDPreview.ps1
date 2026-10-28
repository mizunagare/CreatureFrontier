param([Parameter(Mandatory=$true)][string]$BuildRoot,
      [string[]]$Resolutions=@('960x540','1280x720','1920x1080'),[switch]$Rescue)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
if(!(Test-Path -LiteralPath $exe)){throw 'Packaged executable missing'}
$buildHash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$evidence=Join-Path $projectRoot ('Saved\HUDRenderQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $evidence | Out-Null
Write-Host "HUD evidence: $evidence"
$scenes=@('Companion','Roster','RosterDanger','HelpDanger','Resource','Garden','Building','Downed')
if($Rescue){$scenes+=@('CompanionDownedNoMedicine','CompanionDownedMedicine')}
$results=@()
foreach($resolution in $Resolutions){
    if($resolution -notmatch '^(\d{3,4})x(\d{3,4})$'){throw 'Use WIDTHxHEIGHT resolution values'}
    $width=[int]$Matches[1];$height=[int]$Matches[2]
    if($width -lt 640 -or $width -gt 4096 -or $height -lt 360 -or $height -gt 2160){throw 'Resolution outside test safety bounds'}
    $log=Join-Path $evidence ($resolution+'.log')
    $previewArguments=@('-CFHUDPreview','-RenderOffscreen','-ForceRes',('-ResX='+$width),('-ResY='+$height),'-nosound','-unattended','-nop4','-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.DefaultFeature.MotionBlur 0,r.AntiAliasingMethod 1"',('-abslog="'+$log+'"'))
    if($Rescue){$previewArguments+='-CFHUDRescuePreview'}
    $process=Start-Process -FilePath $exe -WorkingDirectory $BuildRoot -ArgumentList $previewArguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw "HUD preview timeout: $log"}
    $contents=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $contents -notmatch ('CF_HUD_PREVIEW PASS scenes='+$scenes.Count+'\b') -or $contents -match 'CF_HUD.*FAIL|Fatal error|Assertion failed'){throw "HUD behavior failed: $log"}
    foreach($scene in $scenes){if($contents -notmatch ('CF_HUD_FRAME PASS scene='+$scene+' viewport='+$resolution+' ')){throw "Missing actual $resolution frame: $scene"}}
    $rootMatch=[regex]::Match($contents,'CF_HUD_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})')
    if(!$rootMatch.Success){throw 'Fresh screenshot directory not recorded'}
    $shots=Join-Path $BuildRoot ('CreatureFrontier\Saved\HUDPreview\'+$rootMatch.Groups[1].Value)
    foreach($scene in $scenes){
        $source=Join-Path $shots ($scene+'.png')
        if(!(Test-Path -LiteralPath $source) -or (Get-Item -LiteralPath $source).Length -lt 1000){throw "Screenshot missing: $source"}
        $header=New-Object byte[] 24;$stream=[IO.File]::OpenRead($source)
        try {$read=$stream.Read($header,0,24)} finally {$stream.Dispose()}
        if($read -ne 24 -or $header[0] -ne 137 -or $header[1] -ne 80){throw 'Invalid PNG header'}
        $pngWidth=[int]$header[16]*16777216+[int]$header[17]*65536+[int]$header[18]*256+[int]$header[19]
        $pngHeight=[int]$header[20]*16777216+[int]$header[21]*65536+[int]$header[22]*256+[int]$header[23]
        if($pngWidth -ne $width -or $pngHeight -ne $height){throw "PNG resolution mismatch: $source"}
        Copy-Item -LiteralPath $source -Destination (Join-Path $evidence ($resolution+'-'+$scene+'.png'))
    }
    if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $buildHash){throw 'Executable changed during HUD checks'}
    $results += [pscustomobject]@{Resolution=$resolution;Scenes=$scenes.Count;Pass=$true;BuildSHA256=$buildHash;Log=$log}
    $results | Export-Csv -LiteralPath (Join-Path $evidence 'Results.csv') -NoTypeInformation
    Write-Host "$resolution : $($scenes.Count)个渲染场景通过，PNG尺寸已验证"
}
Write-Host "HUD run completed. Inspect every screenshot: $evidence"
