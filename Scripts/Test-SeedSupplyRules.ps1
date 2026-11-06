param([string]$BuildRoot='',[switch]$Route,[switch]$Blocked,[switch]$Waiting)
$ErrorActionPreference='Stop'
if($Route -and $Blocked){throw '路线与不可达混合调度使用独立模式'}
if($Waiting -and ($Route -or $Blocked)){throw '运输等待验证必须独立运行'}
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $root 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $root ('Saved/SeedSupplyQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$log=Join-Path $run 'Rules.log'
$arguments=@('-CFSeedSupplyRules','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
if($Route){$arguments+='-CFSeedSupplyRoute'}
if($Blocked){$arguments+='-CFSeedSupplyBlocked'}
if($Waiting){$arguments+='-CFTransportWaiting'}
$exe=if($BuildRoot){$module}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
Write-Host ('携种事务隔离证据：'+$run)
$p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
if(!$p.WaitForExit(90000)){$p.Kill();$p.WaitForExit();throw '携种事务规则验证超时'}
$raw=Get-Content -LiteralPath $log -Raw
$marker=if($Route){'CF_SEED_ROUTE PASS invariant=1 picked=1 farm=3 carried=0'}else{'CF_SEED_SUPPLY_RULES PASS range=1 paused=1 mixed=1 pickup=1 full=1 partial=1 cancel=1 legacy=1 retained=1 reject=1'}
if($Blocked){$marker='CF_SEED_BLOCKED PASS invariant=1 picked=1 food=3 seedBox=3'}
if($Waiting){$marker='CF_TRANSPORT_WAIT PASS intact=1 stationary=1'}
if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains($marker) -or !$raw.Contains('LogExit: Exiting.')){throw ('携种规则失败：'+$log)}
if($Waiting -and !$raw.Contains('CF_TRANSPORT_RESUPPLY PASS invariant=1 picked=1 farmSeeds=3')){throw '缺少等待后补货自动恢复证据'}
if(!$Route -and !$Blocked -and !$Waiting -and !$raw.Contains('CF_SEED_RECOVERY PASS full=1 partial=1 paused=1 downedAfterDestroy=1')){throw '缺少携种取回边界通过证据'}
if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '验证期间程序发生变化'}
$scope=if($Route){'隔离初始化3种，普通AI实际取种送圃及逐帧守恒；不代表真实生产来源或冷启动恢复'}else{'隔离事务与快照字段校验，不写存档，不代表实际搬运或冷启动恢复'}
if($Blocked){$scope='不可达高处种箱与正常粮运并存；不取种、不阻断送粮，两类库存守恒'}
if($Waiting){$scope='空手到岗等待3秒不空耗、饥饿继续；一次投入3测试种后自动实际取送并消耗体力；隔离夹具，非正常制种或存读'}
[pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log;Scope=$scope}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
Write-Host ($scope+'：通过')
