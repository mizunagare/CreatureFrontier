param([string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved\GatherHoldQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "持续采集与用料验证：$run"
$saveRoot=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved\SaveGames'}else{Join-Path $projectRoot 'Saved\SaveGames'}
$slot=Join-Path $saveRoot 'GatherHoldAutomation.sav';$backup=Join-Path $run 'GatherHoldAutomation-before.sav'
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierGatherHoldQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '持续采集测试槽正在使用'}
$existed=Test-Path -LiteralPath $slot
try{
    if($existed){$originalHash=(Get-FileHash $slot).Hash;Copy-Item -LiteralPath $slot -Destination $backup}
    $log=Join-Path $run 'GatherHold.log'
    $arguments=@('-CFGatherHoldTest','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    $exe=$module
    if(!$BuildRoot){$exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe';$arguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'),'-game')+$arguments}
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw '持续采集验证超时'}
    $body=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $body -match 'CF_.*FAIL|Fatal error|Assertion failed' -or
       !$body.Contains('CF_GATHER_BUILD PASS initial=6 harvested=2 cost=8 material=0') -or
       !$body.Contains('CF_GATHER_HOLD PASS phase=9 material=6 stock=16')){throw "持续采集或资源投入验证失败：$log"}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '持续采集验证期间程序变化'}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log} | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '持续采集、停止边界与真实材料建地板通过；仍非系统持续键盘或完整采集建房路线验收'
}finally{
    try{if($existed){Copy-Item -LiteralPath $backup -Destination $slot;if((Get-FileHash $slot).Hash -ne $originalHash){throw '持续采集测试槽恢复失败'}}elseif(Test-Path $slot){Remove-Item -LiteralPath $slot}}
    finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
