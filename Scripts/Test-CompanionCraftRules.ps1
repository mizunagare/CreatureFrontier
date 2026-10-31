param([string]$BuildRoot='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $root 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $root ('Saved/CompanionCraftRulesQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$log=Join-Path $run 'Rules.log'
$argsList=@('-CFCompanionCraftRules','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
$exe=if($BuildRoot){$module}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
if(!$BuildRoot){$argsList=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$argsList}
Write-Host ('伙伴加工规则夹具：'+$run)
$p=Start-Process -FilePath $exe -ArgumentList $argsList -WindowStyle Hidden -PassThru
if(!$p.WaitForExit(60000)){$p.Kill();$p.WaitForExit();throw '伙伴加工规则验证超时'}
$raw=Get-Content -LiteralPath $log -Raw
if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('CF_COMPANION_CRAFT_RULES PASS missing=1 full=1 partial=1 paused=1 rest=1 exclusive=1 finished=1 once=1') -or !$raw.Contains('LogExit: Exiting.')){throw ('伙伴加工规则失败：'+$log)}
if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '规则验证期间程序变化'}
[pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log;Scope='独立库存规则夹具，非真实采集或移动'}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
Write-Host '缺料／满格等待、暂停／休息、单工人及一次结算规则通过；不作为实际AI移动或工作耗时证据'
