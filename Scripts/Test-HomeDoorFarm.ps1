param([string]$EngineRoot='D:\Epic\UE_5.8',[string]$BuildRoot='',[switch]$NoRoof,[ValidateSet('Test','Write','Read','Cancel','Dismantle')][string]$Mode='Test',[switch]$OriginalAim)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$projectRoot=Split-Path $PSScriptRoot -Parent
$module=Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'
if($BuildRoot){$module=Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved\DoorFarmQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$log=Join-Path $run 'CFDoorFarmTest.log'
Write-Host "门与农圃工作专项：$run"
if($NoRoof -and $Mode -ne 'Test'){throw '跨进程链固定使用有顶房间'}
if($OriginalAim -and $Mode -ne 'Cancel'){throw '原取景复放只用于取消后跟随样本'}
$flag='CFDoorFarm'+$Mode
if($Mode -in @('Cancel','Dismantle')){$flag+='Test'}
$modeNumber=if($Mode -eq 'Write'){1}elseif($Mode -eq 'Read'){2}elseif($Mode -eq 'Cancel'){3}elseif($Mode -eq 'Dismantle'){4}else{0}
$arguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'),'-game',('-'+$flag),'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
$exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if($BuildRoot){$exe=$module;$arguments=$arguments[1..($arguments.Count-1)]}
if($NoRoof){$arguments+='-CFDoorFarmNoRoof'}
if($OriginalAim){$arguments+='-CFDoorFarmCancelOriginalAim'}
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw '门与农圃专项超时'}
if(!(Test-Path -LiteralPath $log)){throw '专项未生成日志'}
if($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch 'CF_DOOR_FARM PASS' -Quiet) -or
   !(Select-String -LiteralPath $log -SimpleMatch 'CF_DOOR_FARM_CLOSED PASS' -Quiet) -or
   (Select-String -LiteralPath $log -Pattern '\bFAIL\b|Fatal error|Assertion failed' -Quiet)){throw "门与农圃专项未通过：$log"}
if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '专项期间模块发生变化'}
if($Mode -eq 'Cancel' -and !$OriginalAim -and !(Select-String -LiteralPath $log -SimpleMatch 'CF_DOOR_FARM_HERO_FIXTURE overlapFarm=0' -Quiet)){throw '取消后归队缺少玩家与农圃无重叠证据'}
if(!(Select-String -LiteralPath $log -SimpleMatch ('CF_DOOR_FARM_MODE PASS mode='+$modeNumber) -Quiet)){throw '缺少对应存读模式的成功标记'}
$fixtureMarker=if($NoRoof){'CF_DOOR_FARM_FIXTURE roof=0 removed=1'}else{'CF_DOOR_FARM_FIXTURE roof=1 removed=0'}
if(!(Select-String -LiteralPath $log -SimpleMatch $fixtureMarker -Quiet)){throw '缺少预期屋顶对照标记'}
[pscustomobject]@{Case=$flag;NoRoof=[bool]$NoRoof;Pass=$true;BuildSHA256=$hash;Log=$log} | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
Write-Host '门与农圃工作专项通过；不代表运输、多伙伴或跨进程恢复已验收'
