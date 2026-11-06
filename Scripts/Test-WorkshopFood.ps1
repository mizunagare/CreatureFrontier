param([string]$BuildRoot='',[ValidateSet('Route','Blocked','Reserve','Cancel','Removed')][string]$Mode='Route')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $root 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $root ('Saved/WorkshopFoodQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$log=Join-Path $run 'Route.log'
$arguments=@('-CFWorkshopFoodRoute','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
if($Mode -ne 'Route'){$arguments+=('-CFWorkshopFood'+$Mode)}
$exe=if($BuildRoot){$module}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
Write-Host ('加工补粮隔离行程：'+$run)
$p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
if(!$p.WaitForExit(70000)){$p.Kill();$p.WaitForExit();throw '加工补粮验证超时'}
$raw=Get-Content -LiteralPath $log -Raw
if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('CF_WORKSHOP_FOOD_ROUTE PASS invariant=1 picked=1') -or !$raw.Contains('LogExit: Exiting.')){throw ('加工补粮失败：'+$log)}
if($Mode -in @('Cancel','Removed') -and !$raw.Contains('CF_WORKSHOP_FOOD_CHANGED PASS')){throw '缺少途中取消／失效事件及保货证据'}
if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '测试期间程序改变'}
[pscustomobject]@{Pass=$true;Mode=$Mode;BuildSHA256=$hash;Log=$log;Scope='隔离投入粮食与委托，普通AI实际搬运及逐帧守恒；不代表正常来源、存读或图形'}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
Write-Host '加工补粮隔离行程与守恒通过；正常来源和存读另验'
