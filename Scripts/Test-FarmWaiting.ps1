param([Parameter(Mandatory=$true)][string]$ReadInputPath,[ValidateSet('Empty','Full')][string]$Mode='Empty',[string]$BuildRoot='',[switch]$Read,[switch]$Preview,
      [ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $projectRoot 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved/FarmWaitingQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$log=Join-Path $run 'Waiting.log';$protected=@();$started=$null
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierFarmWaitingQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '农业等待专项正在运行'}
try{
    $slotRoot=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slotRoot -Force | Out-Null
    foreach($name in @('FarmWaitingInput','FarmWaitingHold','FarmWaitingDone','FarmWaitingBlocked')){
        $path=Join-Path $slotRoot ($name+'.sav');if($path -eq $ReadInputPath){throw '输入须为独立副本，不能使用受保护测试槽'}
        $backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path -LiteralPath $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slotRoot 'FarmWaitingInput.sav')
    $arguments=@(('-CFFarmWait'+$Mode),'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Read){$arguments+='-CFFarmWaitRead'}
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFFarmWaitPreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    $started=[DateTime]::UtcNow
    [pscustomobject]@{Mode=$Mode;Read=[bool]$Read;Preview=[bool]$Preview;Resolution=$Resolution;BuildSHA256=$hash;ReadInputSHA256=$inputHash;StartedUTC=$started.ToString('o')}|Export-Csv -LiteralPath (Join-Path $run 'Run.csv') -NoTypeInformation
    Write-Host ('农业等待与复工：'+$run)
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(130000)){$process.Kill();$process.WaitForExit();throw '农业等待专项超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('LogExit: Exiting.')){throw ('农业等待专项失败：'+$log)}
    $label=$Mode.ToLowerInvariant()
    foreach($marker in @(('CF_FARM_WAIT_RESUME PASS mode='+$label+' cycle=1 originalTask=1'),('CF_FARM_WAIT_SAVED PASS version=38 mode='+$label+' modules=25 sameTask=1'),('CF_FARM_WAIT PASS mode='+$label+' reason=complete'))){if(!$raw.Contains($marker)){throw ('缺少等待复工标记：'+$marker)}}
    if($Mode -eq 'Full' -and !$raw.Contains('CF_FARM_WAIT_RESUME PASS mode=full cycle=2 originalTask=1')){throw '第二次满粮成熟等待及取粮后结算缺失'}
    if($Read -and $raw -notmatch ('CF_FARM_WAIT_RESTORE PASS version=(?:37|38) mode='+$label+' sameTask=1')){throw '缺少等待态冷恢复标记'}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash){throw '程序或源副本改变'}
    if($Preview){
        $root=[regex]::Match($raw,'CF_FARM_WAIT_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$root.Success){throw '缺少等待提示截图目录'}
        $scenes=if($Mode -eq 'Full'){@('Waiting','WaitingAgain')}else{@('Waiting')}
        foreach($scene in $scenes){$source=Join-Path $saved ('FarmWaitingPreview/'+$root.Groups[1].Value+'/'+$scene+'.png');if(!(Test-Path -LiteralPath $source) -or (Get-Item -LiteralPath $source).LastWriteTimeUtc -lt $started){throw '等待截图缺失或不是本轮生成'};$bytes=[IO.File]::ReadAllBytes($source);if($bytes.Length -lt 1000 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80){throw '等待截图格式错误'};$width=[int]$bytes[16]*16777216+[int]$bytes[17]*65536+[int]$bytes[18]*256+[int]$bytes[19];$height=[int]$bytes[20]*16777216+[int]$bytes[21]*65536+[int]$bytes[22]*256+[int]$bytes[23];if(($width.ToString()+'x'+$height) -ne $Resolution){throw '等待截图尺寸不符'};Copy-Item -LiteralPath $source -Destination (Join-Path $run ($scene+'.png'))}
    }
    $holdHash=if(!$Read){Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'FarmWaitingHold.sav')}else{''}
    [pscustomobject]@{Mode=$Mode;Read=[bool]$Read;Preview=[bool]$Preview;Resolution=$Resolution;Pass=$true;BuildSHA256=$hash;ReadInputSHA256=$inputHash;HoldSaveSHA256=$holdHash;DoneSaveSHA256=(Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'FarmWaitingDone.sav'));Log=$log}|Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '农业等待不扣工作体力、保留原任务及补种／取粮后复工存读通过；容量／种子为明确单元夹具，不作为自然经济或长期经营验收'
}finally{
    try{
        if($started){foreach($name in @('FarmWaitingHold','FarmWaitingDone','FarmWaitingBlocked')){$path=Join-Path $slotRoot ($name+'.sav');if((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'-generated.sav'))}}}
        foreach($entry in $protected){if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '农业等待原测试槽恢复失败'}}elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}}
    }finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
