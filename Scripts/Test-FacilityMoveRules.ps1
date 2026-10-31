param([string]$BuildRoot='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $root 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $root ('Saved/FacilityMoveRulesQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$log=Join-Path $run 'Rules.log'
$argsList=@('-CFFacilityMoveRules','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
$exe=if($BuildRoot){$module}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
if(!$BuildRoot){$argsList=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$argsList}
Write-Host ('设施搬迁逻辑夹具：'+$run)
$p=Start-Process -FilePath $exe -ArgumentList $argsList -WindowStyle Hidden -PassThru
if(!$p.WaitForExit(60000)){$p.Kill();$p.WaitForExit();throw '设施搬迁逻辑验证超时'}
$raw=Get-Content -LiteralPath $log -Raw
if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('CF_FACILITY_MOVE_RULES PASS started=1 cancelled=1 occupied=1 noDuplicate=1 valid=1 moved=1') -or !$raw.Contains('LogExit: Exiting.')){throw ('设施搬迁逻辑验证失败：'+$log)}
if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '验证期间程序发生变化'}
[pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log;Scope='隔离逻辑夹具，不证明实际步行、真实资源来源或存档恢复'}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
Write-Host '预览取消、占用拒绝、防重复建造和同实体状态保留通过；真实伙伴重新到达与存读仍待验证'
