param([string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$exe=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}
$module=if($BuildRoot){$exe}else{Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved\HomePaletteQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$log=Join-Path $run 'CFHomePaletteTest.log'
Write-Host "游戏世界门材质核对：$run"
$arguments=@('-game','-CFHomePaletteTest','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
if(!$BuildRoot){$arguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'))+$arguments}
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw '门材质专项超时'}
if($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $log)){throw '门材质专项未正常结束'}
if(!(Select-String -LiteralPath $log -SimpleMatch 'CF_HOME_PALETTE PASS built=1 opened=1 closed=1' -Quiet) -or
    (Select-String -LiteralPath $log -Pattern '\bFAIL\b|Fatal error|Assertion failed' -Quiet)){throw "门材质专项未通过：$log"}
foreach($stage in @('built','opened','closed','settled')){
    $parts=@(Select-String -LiteralPath $log -Pattern ('CF_HOME_PALETTE_PART stage='+$stage+' part=Part[0-3] dynamic=1 matches=1'))
    if($parts.Count -ne 4){throw "门材质阶段 $stage 没有四个零件匹配证据"}
}
if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '门材质专项期间程序模块变化'}
[pscustomobject]@{Case='CFHomePaletteTest';Pass=$true;BuildSHA256=$hash;Log=$log} | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
Write-Host '四阶段材质实例参数核对通过；无渲染模式不证明像素颜色或光照品质'
