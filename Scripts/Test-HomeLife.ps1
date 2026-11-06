param([Parameter(Mandatory=$true)][string]$ReadInputPath,[string]$BuildRoot='',[switch]$Trace,[switch]$Resume,[switch]$Recovery,[switch]$Preview,[ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
if($Resume -and $Recovery){throw '中途恢复与旧失败现场恢复不能混用'}
if($Preview -and ($Resume -or $Recovery)){throw '图形验证使用完整自然生活流程'}
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $root 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $root 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$sourceHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$run=Join-Path $root ('Saved/HomeLifeQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$slots=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slots -Force|Out-Null
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierHomeLifeQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '真实伙伴生活验证正在运行'}
$protected=@();$started=$null
try{
    foreach($name in @('HomeLifeInput','HomeLifeMid','HomeLifeDone','HomeLifeBlocked')){
        $path=Join-Path $slots ($name+'.sav');if([IO.Path]::GetFullPath($path) -eq $ReadInputPath){throw '来源必须是独立副本'}
        $backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slots 'HomeLifeInput.sav')
    $log=Join-Path $run 'Life.log'
    $scope=if($Recovery){'原2粮低饱食失败档保留消耗，运输伙伴自然吃粮复工与双方返岗；非完整4粮链'}elseif($Resume){'独立恢复首次农夫进食后的3粮中途档，运输伙伴自然吃粮休息复工与双方返岗；不重复证明此前一餐'}else{'真实送粮档自然消耗、两伙伴取食返岗及运输伙伴休息复工；不改需求或时间'}
    [pscustomobject]@{BuildSHA256=$hash;SourceSHA256=$sourceHash;Source=$ReadInputPath;BuildRoot=$BuildRoot;Resume=[bool]$Resume;Recovery=[bool]$Recovery;Preview=[bool]$Preview;Resolution=$Resolution;Scope=$scope}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    $arguments=@('-CFIndoorFeedingTest','-CFHomeLifeTest','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Trace){$arguments+=@('-CFFeedingApproachTrace','-CFFarmApproachTrace')}
    if($Resume){$arguments+='-CFHomeLifeRead'}
    if($Recovery){$arguments+='-CFHomeLifeRecovery'}
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFHomeLifePreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if($Preview -and !$BuildRoot){$exe='D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    $started=[DateTime]::UtcNow;Write-Host ('自然伙伴生活证据：'+$run)
    $p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(220000)){$p.Kill();$p.WaitForExit();throw '自然伙伴生活验证超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    $reason=if($Recovery){'failed-state-fed-rest-return'}elseif($Resume){'mid-state-fed-rest-return'}else{'natural-meals-rest-return'}
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('CF_HOME_LIFE PASS reason='+$reason) -or !$raw.Contains('LogExit: Exiting.')){throw ('自然伙伴生活未通过：'+$log)}
    $done=Join-Path $slots 'HomeLifeDone.sav'
    if(!(Test-Path $done) -or (Get-Item $done).LastWriteTimeUtc -lt $started){throw '缺少本次新完成档'}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $sourceHash){throw '程序或来源指纹变化'}
    if($Preview){$id=[regex]::Match($raw,'CF_HOME_LIFE_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$id.Success){throw '缺少截图目录'};foreach($name in @('FarmerMeal','CourierMeal','Rest','Returned')){$path=Join-Path $saved ('HomeLifePreview/'+$id.Groups[1].Value+'/'+$name+'.png');if(!(Test-Path $path) -or (Get-Item $path).LastWriteTimeUtc -lt $started){throw '缺少本轮新图'};Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'.png'))}}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log;VisualReviewRequired=[bool]$Preview}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host ($scope+'：通过；尚非长期经营或图形验收')
}finally{try{foreach($entry in $protected){
    if($started -and (Test-Path $entry.Path) -and (Get-Item $entry.Path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ((Split-Path $entry.Path -LeafBase)+'-generated.sav'))}
    if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原测试槽恢复失败'}}elseif(Test-Path $entry.Path){Remove-Item -LiteralPath $entry.Path}
}}finally{$mutex.ReleaseMutex();$mutex.Dispose()}}
