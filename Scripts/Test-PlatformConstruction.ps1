param([Parameter(Mandatory=$true)][string]$ReadInputPath,[string]$BuildRoot='',
      [switch]$Read,[switch]$Preview,[ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $projectRoot 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved/PlatformConstructionQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$log=Join-Path $run 'Platform.log';$protected=@();$started=$null
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierPlatformQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '地板屋顶施工专项正在运行'}
try{
    $slotRoot=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slotRoot -Force | Out-Null
    foreach($name in @('PlatformInput','PlatformFloorMid','PlatformRoofMid','PlatformBad','PlatformDone','PlatformBlocked')){
        $path=Join-Path $slotRoot ($name+'.sav');if($path -eq $ReadInputPath){throw '输入必须为独立副本，不能使用受保护槽'}
        $backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path -LiteralPath $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slotRoot 'PlatformInput.sav')
    $arguments=@($(if($Read){'-CFPlatformRead'}else{'-CFPlatformTest'}),'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFPlatformPreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    $started=[DateTime]::UtcNow
    [pscustomobject]@{Read=[bool]$Read;Preview=[bool]$Preview;Resolution=$Resolution;BuildSHA256=$hash;ReadInputSHA256=$inputHash;StartedUTC=$started.ToString('o')}|Export-Csv -LiteralPath (Join-Path $run 'Run.csv') -NoTypeInformation
    Write-Host ('原家园地板屋顶施工：'+$run)
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(330000)){$process.Kill();$process.WaitForExit();throw '地板屋顶施工专项超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('LogExit: Exiting.')){throw ('地板屋顶施工专项失败：'+$log)}
    foreach($marker in @('CF_PLATFORM_MOTION_GUARDS PASS longFrame=1 horizontalJump=0 verticalJump=0 zeroTimeJump=0','CF_PLATFORM_JOIN PASS outside=1 continuous=1','CF_PLATFORM_LIFE PASS version=37 modules=24 retained=19 floor=1 roof=1 hero=1 pet=1 home=1 petHome=1 material=0 seeds=1 food=0 bond=9','CF_PLATFORM_BUILD PASS reason=complete')){if(!$raw.Contains($marker)){throw ('缺少施工行为标记：'+$marker)}}
    $part=0
    if($Read){$match=[regex]::Match($raw,'CF_PLATFORM_RESTORE PASS version=37 part=([04]) carried=3 retained=19');if(!$match.Success){throw '缺少地板或屋顶在途恢复标记'};$part=[int]$match.Groups[1].Value}
    else{foreach($marker in @('CF_PLATFORM_HARVEST count=33 material=38','CF_PLATFORM_GUARDS PASS pendingSupport=0 facilitySupport=0 bad36Rejected=1 retained=1','CF_PLATFORM_MID PASS version=37 part=0 carried=3 paid=0','CF_PLATFORM_MID PASS version=37 part=4 carried=3 paid=0')){if(!$raw.Contains($marker)){throw ('缺少原屋施工投入或保护标记：'+$marker)}}}
    if(!$Read -or $part -eq 0){if(!$raw.Contains('CF_PLATFORM_ROOF_SUPPORT PASS retained=1 refund=0')){throw '待建屋顶的承重墙拒拆标记缺失'}}
    for($index=$part;$index -le 4;$index++){if(!$raw.Contains('CF_PLATFORM_PART PASS part='+$index+' ')){throw '有模块没有实际施工完工'}}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash){throw '程序或源输入副本发生变化'}
    if($Preview){
        $root=[regex]::Match($raw,'CF_PLATFORM_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$root.Success){throw '缺少本轮截图根目录'}
        $scenes=if(!$Read){@('FloorPending','FloorDone','RoofPending','RoofDone','CanopyOccupied')}elseif($part -eq 0){@('FloorDone','RoofPending','RoofDone','CanopyOccupied')}else{@('RoofDone','CanopyOccupied')}
        foreach($scene in $scenes){$source=Join-Path $saved ('PlatformPreview/'+$root.Groups[1].Value+'/'+$scene+'.png');if(!(Test-Path -LiteralPath $source) -or (Get-Item -LiteralPath $source).LastWriteTimeUtc -lt $started){throw '截图缺失或不是本轮生成'};$bytes=[IO.File]::ReadAllBytes($source);if($bytes.Length -lt 1000 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80){throw '截图不是有效PNG'};$width=[int]$bytes[16]*16777216+[int]$bytes[17]*65536+[int]$bytes[18]*256+[int]$bytes[19];$height=[int]$bytes[20]*16777216+[int]$bytes[21]*65536+[int]$bytes[22]*256+[int]$bytes[23];if(($width.ToString()+'x'+$height) -ne $Resolution){throw 'PNG实际尺寸不符'};Copy-Item -LiteralPath $source -Destination (Join-Path $run ($scene+'.png'))}
    }
    $floorHash=if(!$Read){Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'PlatformFloorMid.sav')}else{''}
    $roofHash=if(!$Read -or $part -eq 0){Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'PlatformRoofMid.sav')}else{''}
    [pscustomobject]@{Read=[bool]$Read;StartPart=$part;Preview=[bool]$Preview;Resolution=$Resolution;Pass=$true;BuildSHA256=$hash;ReadInputSHA256=$inputHash;FloorSaveSHA256=$floorHash;RoofSaveSHA256=$roofHash;DoneSaveSHA256=(Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'PlatformDone.sav'));Log=$log}|Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '原屋伙伴真实供料搭建地板／屋顶、支撑保护与新增遮棚实际同行存读通过；图形仍须逐张目视，不当作任意房型或自然采集路线验收'
}finally{
    try{
        if($started){foreach($name in @('PlatformFloorMid','PlatformRoofMid','PlatformDone','PlatformBlocked')){$path=Join-Path $slotRoot ($name+'.sav');if((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'-generated.sav'))}}}
        foreach($entry in $protected){if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原施工测试槽恢复失败'}}elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}}
    }finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
