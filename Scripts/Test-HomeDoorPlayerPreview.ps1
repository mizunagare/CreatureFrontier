param([switch]$Editor,[string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8',
      [string[]]$Resolutions=@('960x540','1280x720','1920x1080'),[switch]$Rotated)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$projectRoot=Split-Path $PSScriptRoot -Parent
if($Editor){
    $exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    $module=Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'
    $savedRoot=Join-Path $projectRoot 'Saved';$working=$projectRoot
}else{
    if(!$BuildRoot){throw '请指定 Editor 或 BuildRoot'}
    $exe=Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
    $module=$exe;$savedRoot=Join-Path $BuildRoot 'CreatureFrontier\Saved';$working=$BuildRoot
}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved\DoorPlayerRenderQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "玩家门通行图形证据：$run"
$results=@()
foreach($resolution in $Resolutions){
    if($resolution -notmatch '^(\d+)x(\d+)$'){throw '分辨率必须为宽x高'}
    $width=[int]$Matches[1];$height=[int]$Matches[2]
    if($width -lt 960 -or $height -lt 540 -or $width -gt 3840 -or $height -gt 2160){throw '本专项尺寸超出验收范围'}
    $folder=Join-Path $run $resolution;New-Item -ItemType Directory -Path $folder | Out-Null
    $log=Join-Path $folder 'Traversal.log';$started=[DateTime]::UtcNow
    $flag=if($Rotated){'-CFDoorPlayerRotatedTest'}else{'-CFDoorPlayerTest'}
    $yaw=if($Rotated){90}else{0}
    $arguments=@('-game',$flag,'-CFDoorPlayerPreview','-RenderOffscreen','-ForceRes',('-ResX='+$width),('-ResY='+$height),'-nosound','-unattended','-nop4','-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.DefaultFeature.MotionBlur 0,r.AntiAliasingMethod 1"',('-abslog="'+$log+'"'))
    if($Editor){$arguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'))+$arguments}
    $process=Start-Process -FilePath $exe -WorkingDirectory $working -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw "玩家通行图形专项超时：$log"}
    if($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch ('CF_DOOR_PLAYER PASS yaw='+$yaw) -Quiet) -or (Select-String -LiteralPath $log -Pattern 'CF_\w+ FAIL|Fatal error|Assertion failed' -Quiet)){throw "玩家通行图形专项失败：$log"}
    $contents=Get-Content -LiteralPath $log -Raw
    $rootMatch=[regex]::Match($contents,'CF_DOOR_PLAYER_ROOT [^\r\n]*[/\\]([A-F0-9]{32})')
    if(!$rootMatch.Success){throw '缺少本轮截图根目录'}
    $frames=@()
    foreach($scene in @('Closed','Open','Occupied','InsideClosed','Returned')){
        $frame=[regex]::Match($contents,('CF_DOOR_PLAYER_SHOT '+$scene+' viewport='+$width+'x'+$height+' frame=(\d+)'))
        if(!$frame.Success){throw "缺少实际门对象卡/视口帧：$scene"}
        $expectedState=if($scene -in @('Closed','InsideClosed','Returned')){1}else{2}
        if(!(Select-String -LiteralPath $log -SimpleMatch ('CF_DOOR_PLAYER_SHOT_STATE '+$scene+' PASS state='+$expectedState+' expected='+$expectedState) -Quiet)){throw "截图实际处理时的门状态与场景标签不一致：$scene"}
        $frames+=$frame.Groups[1].Value
        $source=Join-Path $savedRoot ('DoorPlayerPreview\'+$rootMatch.Groups[1].Value+'\'+$scene+'.png')
        $info=Get-Item -LiteralPath $source
        if($info.Length -lt 1000 -or $info.LastWriteTimeUtc -lt $started){throw '门通行截图不是本轮有效新图'}
        $header=New-Object byte[] 24;$stream=[IO.File]::OpenRead($source)
        try{$read=$stream.Read($header,0,24)}finally{$stream.Dispose()}
        $pngWidth=[int]$header[16]*16777216+[int]$header[17]*65536+[int]$header[18]*256+[int]$header[19]
        $pngHeight=[int]$header[20]*16777216+[int]$header[21]*65536+[int]$header[22]*256+[int]$header[23]
        if($read -ne 24 -or $header[0] -ne 137 -or $header[1] -ne 80 -or $pngWidth -ne $width -or $pngHeight -ne $height){throw "门通行 PNG 格式/尺寸不符：$scene"}
        Copy-Item -LiteralPath $source -Destination (Join-Path $folder ($scene+'.png'))
    }
    if(@($frames | Sort-Object -Unique).Count -ne 5){throw '五张门通行截图没有对应不同HUD帧'}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '图形专项期间程序模块发生变化'}
    $results+=[pscustomobject]@{Resolution=$resolution;Yaw=$yaw;Pass=$true;BuildSHA256=$hash;Log=$log}
    $results | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host "$resolution 行为及五张新图检查通过，仍需逐图查看"
}
