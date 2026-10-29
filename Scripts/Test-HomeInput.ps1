param([string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$projectRoot=Split-Path $PSScriptRoot -Parent
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved\HomeInputQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$log=Join-Path $run 'HomeInput.log'
Write-Host "房屋预览输入绑定验证：$run"
# 旧程序可能忽略短模式参数而运行完整房屋测试；保护其全部模式零测试槽。
$saveRoot=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved\SaveGames'}else{Join-Path $projectRoot 'Saved\SaveGames'}
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierHomeInputQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '房屋输入测试正在运行'}
$protected=@()
try {
foreach($name in @('FrontierHomeAutomation','FrontierHomeInvalid','FrontierHomeLegacy')){
    $path=Join-Path $saveRoot ($name+'.sav');$existed=Test-Path -LiteralPath $path
    $backup=Join-Path $run ($name+'-before.sav');$beforeHash=''
    if($existed){$beforeHash=(Get-FileHash -LiteralPath $path).Hash;Copy-Item -LiteralPath $path -Destination $backup}
    $protected+= [pscustomobject]@{Path=$path;Existed=$existed;Backup=$backup;Hash=$beforeHash}
}
$arguments=@('-CFHomeTest','-CFHomeInputOnly','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
$exe=$module
if(!$BuildRoot){$exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe';$arguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'),'-game')+$arguments}
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw '房屋输入绑定专项超时'}
if($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $log)){throw '房屋输入绑定专项未正常退出'}
$body=Get-Content -LiteralPath $log -Raw
if($body -match '\bFAIL\b|Fatal error|Assertion failed' -or !$body.Contains('CF_HOME_INPUT_BINDINGS PASS floor=1 rotated=1 cancelled=1 material=192') -or !$body.Contains('CF_HOME_INPUT_ONLY PASS noSave=1')){throw "房屋输入绑定专项未通过或构建不支持：$log"}
if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '房屋输入测试期间模块发生变化'}
[pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log} | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
Write-Host '地板预览放置、墙预览旋转与取消绑定通过；不是系统鼠标或整屋人工建造验收'
} finally {
    try {foreach($entry in $protected){
        if($entry.Existed){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.Hash){throw '房屋输入测试原槽恢复失败'}}
        elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}
    }} finally {$mutex.ReleaseMutex();$mutex.Dispose()}
}
