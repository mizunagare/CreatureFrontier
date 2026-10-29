param([string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$projectRoot=Split-Path $PSScriptRoot -Parent
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved\WorkAimQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "设施瞄准派工验证：$run"
$saveRoot=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved\SaveGames'}else{Join-Path $projectRoot 'Saved\SaveGames'}
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierWorkAimQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '设施瞄准派工验证正在运行'}
$protected=@()
try {
    # 兼容旧程序忽略新开关的情况，保护完整工作周期专项可能写入的QA槽。
    foreach($name in @('FrontierWorkCycleAutomation','FrontierLegacyWorkCycleAutomation')){
        $path=Join-Path $saveRoot ($name+'.sav');$backup=Join-Path $run ($name+'-before.sav');$exists=Test-Path -LiteralPath $path;$beforeHash=''
        if($exists){$beforeHash=(Get-FileHash -LiteralPath $path).Hash;Copy-Item -LiteralPath $path -Destination $backup}
        $protected+=[pscustomobject]@{Path=$path;Backup=$backup;Exists=$exists;Hash=$beforeHash}
    }
    $log=Join-Path $run 'WorkAim.log'
    $arguments=@('-CFWorkCycleTest','-CFWorkAimTest','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    $exe=$module
    if(!$BuildRoot){$exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe';$arguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'),'-game')+$arguments}
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw '设施瞄准派工验证超时'}
    if($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $log)){throw "设施瞄准派工未正常通过：$log"}
    $body=Get-Content -LiteralPath $log -Raw
    if($body -match '\bFAIL\b|Fatal error|Assertion failed' -or !$body.Contains('CF_WORK_AIM PASS aimedFar=1 nearestFallback=1 feedingPreserved=1 noSave=1')){throw "设施瞄准派工标记不完整或构建不支持：$log"}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '设施瞄准派工测试期间模块变化'}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log} | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '瞄准较远设施、就近回退及喂食槽不改原指令通过；不代表自然工作或硬件输入验收'
} finally {
    try {foreach($entry in $protected){
        if($entry.Exists){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.Hash){throw '工作周期QA原槽恢复失败'}}
        elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}
    }} finally {$mutex.ReleaseMutex();$mutex.Dispose()}
}
