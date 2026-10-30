param([ValidateSet('Crop','Farming','WorkCycle')][string]$Suite='Crop',[string]$BuildRoot='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $projectRoot 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved/FarmRegressionQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$log=Join-Path $run ($Suite+'.log');$protected=@()
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierFarmRegressionQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '农业关联回归正在运行'}
try{
    $slotRoot=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slotRoot -Force | Out-Null
    $names=if($Suite -eq 'Crop'){@('CropCross','CropBad','CropRipe','CropLegacy23')}elseif($Suite -eq 'Farming'){@('FrontierFarmingAutomation','FrontierLegacyFoodAutomation')}else{@('FrontierWorkCycleAutomation','FrontierLegacyWorkCycleAutomation')}
    foreach($name in $names){$path=Join-Path $slotRoot ($name+'.sav');$backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path -LiteralPath $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''};if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}}
    $flag=if($Suite -eq 'Crop'){'CFCropTest'}elseif($Suite -eq 'Farming'){'CFFarmingTest'}else{'CFWorkCycleTest'};$marker=if($Suite -eq 'Crop'){'CF_CROP PASS'}elseif($Suite -eq 'Farming'){'CF_FARM PASS'}else{'CF_WORK_CYCLE PASS'}
    $arguments=@(('-'+$flag),'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    $exe=if($BuildRoot){$module}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    Write-Host ('原农业关联回归：'+$run)
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw '农业关联回归超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains($marker) -or !$raw.Contains('LogExit: Exiting.')){throw ('农业关联回归失败：'+$log)}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '农业回归期间程序改变'}
    [pscustomobject]@{Suite=$Suite;Pass=$true;BuildSHA256=$hash;Log=$log}|Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '原农业／种子逻辑及迁移关联通过；旧逻辑单元不当作真实移动或图形验收'
}finally{
    try{foreach($entry in $protected){if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原农业测试槽恢复失败'}}elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}}}
    finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
