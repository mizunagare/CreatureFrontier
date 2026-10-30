param([Parameter(Mandatory=$true)][string]$ReadInputPath,[string]$BuildRoot='',[switch]$Read,[switch]$Preview,
      [ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $projectRoot 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved/UpperDeckLifeQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$log=Join-Path $run 'Life.log';$protected=@();$started=$null
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierUpperDeckLifeQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '二层生产与照料专项正在运行'}
try{
    $slotRoot=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slotRoot -Force | Out-Null
    foreach($name in @('UpperDeckLifeInput','UpperDeckLifeWork','UpperDeckLifeDone','UpperDeckLifeBlocked')){
        $path=Join-Path $slotRoot ($name+'.sav');if($path -eq $ReadInputPath){throw '输入须为独立副本，不能使用受保护测试槽'}
        $backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path -LiteralPath $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slotRoot 'UpperDeckLifeInput.sav')
    $arguments=@($(if($Read){'-CFUpperDeckLifeRead'}else{'-CFUpperDeckLifeTest'}),'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFUpperDeckLifePreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    $started=[DateTime]::UtcNow
    [pscustomobject]@{Read=[bool]$Read;Preview=[bool]$Preview;Resolution=$Resolution;BuildSHA256=$hash;ReadInputSHA256=$inputHash;StartedUTC=$started.ToString('o')}|Export-Csv -LiteralPath (Join-Path $run 'Run.csv') -NoTypeInformation
    Write-Host ('原二层平台生产与照料：'+$run)
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(330000)){$process.Kill();$process.WaitForExit();throw '二层生产与照料专项超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('LogExit: Exiting.')){throw ('二层生产与照料专项失败：'+$log)}
    foreach($marker in @('CF_DECK_LIFE_PRODUCED PASS upstairs=1 actualSteps=1 seed=0 food=1 xp=9','CF_DECK_LIFE_FEED PASS upstairs=1 food=0 bond=12','CF_DECK_LIFE_SAVED PASS version=37 modules=25 retainedFacilities=2 facilities=3 material=0 seeds=0 food=0 xp=9 bond=12','CF_DECK_LIFE PASS reason=complete')){
        if(!$raw.Contains($marker)){throw ('缺少二层生产／照料标记：'+$marker)}
    }
    $required=if($Read){@('CF_DECK_LIFE_RESTORE PASS version=37 downstairs=1 task=1 reserved=1 retained=25')}else{@('CF_DECK_LIFE_HARVEST count=18 material=18','CF_DECK_LIFE_ORDER PASS version=37 downstairs=1 aimed=1 material=0 seeds=0 reserved=1 facilities=3')}
    foreach($marker in $required){if(!$raw.Contains($marker)){throw ('缺少生产投入／恢复标记：'+$marker)}}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash){throw '程序或源输入副本改变'}
    if($Preview){
        if(!$raw.Contains('CF_DECK_LIFE_AIM PASS downstairs=1 target=1')){throw '楼下图形预览未实际瞄准新农圃'}
        $root=[regex]::Match($raw,'CF_DECK_LIFE_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$root.Success){throw '缺少生产照料截图独立目录'}
        foreach($scene in @('Work','Production','Harvest','Fed')){
            $source=Join-Path $saved ('UpperDeckLifePreview/'+$root.Groups[1].Value+'/'+$scene+'.png')
            if(!(Test-Path -LiteralPath $source) -or (Get-Item -LiteralPath $source).LastWriteTimeUtc -lt $started){throw '生产照料截图缺失或不是本轮生成'}
            $bytes=[IO.File]::ReadAllBytes($source);if($bytes.Length -lt 1000 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80){throw '生产照料截图格式错误'}
            $width=[int]$bytes[16]*16777216+[int]$bytes[17]*65536+[int]$bytes[18]*256+[int]$bytes[19];$height=[int]$bytes[20]*16777216+[int]$bytes[21]*65536+[int]$bytes[22]*256+[int]$bytes[23]
            if(($width.ToString()+'x'+$height) -ne $Resolution){throw '生产照料截图尺寸不符'}
            Copy-Item -LiteralPath $source -Destination (Join-Path $run ($scene+'.png'))
        }
    }
    $workHash=if(!$Read){Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'UpperDeckLifeWork.sav')}else{''}
    [pscustomobject]@{Read=[bool]$Read;Preview=[bool]$Preview;Resolution=$Resolution;Pass=$true;BuildSHA256=$hash;ReadInputSHA256=$inputHash;WorkSaveSHA256=$workHash;DoneSaveSHA256=(Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'UpperDeckLifeDone.sav'));Log=$log}|Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '同一新平台实际种植生产、取粮喂养、共同下楼与存读通过；图形仍须逐图目视，固定夹具不作为自然游玩或最终品质验收'
}finally{
    try{
        if($started){foreach($name in @('UpperDeckLifeWork','UpperDeckLifeDone','UpperDeckLifeBlocked')){$path=Join-Path $slotRoot ($name+'.sav');if((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'-generated.sav'))}}}
        foreach($entry in $protected){if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '二层生产照料原测试槽恢复失败'}}elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}}
    }finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
