param([string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8',[switch]$Room,[switch]$Work,[ValidateSet('Test','Write','Read')][string]$Mode='Test',[switch]$Fence,[switch]$FenceNavigation)
$ErrorActionPreference='Stop'
if($FenceNavigation){if($Mode -ne 'Test'){throw '栅栏导航仅支持普通模式'};$Fence=$true}
if($Fence -and ($Room -or $Work)){throw '栅栏专项不能与房屋工作组合'}
if($Mode -ne 'Test' -and !$Work -and !$Fence){throw '存读模式仅用于房屋工作或栅栏链'}
if($Work){$Room=$true}
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
if($Room){$arguments+='-CFHomeRoomInput'}
if($Fence){$arguments+='-CFHomeFenceTest'}
if($FenceNavigation){$arguments+='-CFHomeFenceNavigation'}
if($Work){$arguments+=@('-CFHomeRoomWork','-CFFurnishedFarm')}
if($Mode -eq 'Write'){$arguments+='-CFHomeRoomWorkWrite'}
if($Mode -eq 'Read'){$arguments=@('-CFDoorFarmRead','-CFFurnishedFarm','-CFHomeRoomWorkRead','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))}
if($Fence -and $Mode -ne 'Test'){$arguments=@('-CFHomeTest',('-CFHomeFence'+$Mode),'-CFHomeFenceTest','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))}
$exe=$module
if(!$BuildRoot){$exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe';$arguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'),'-game')+$arguments}
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw '房屋输入绑定专项超时'}
if($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $log)){throw '房屋输入绑定专项未正常退出'}
$body=Get-Content -LiteralPath $log -Raw
$completion=if($FenceNavigation){'CF_HOME_FENCE_NAV PASS phase=4 entered=1 exited=1 continuous=1 noCross=1'}elseif($Fence -and $Mode -eq 'Write'){'CF_HOME_FENCE_WRITE PASS modules=4 material=180'}elseif($Fence -and $Mode -eq 'Read'){'CF_HOME_FENCE_READ PASS identity=1 badVersion=1 material=180'}elseif($Fence){'CF_HOME_FENCE PASS modules=3 material=182 collision=1 removed=1 floorGuard=1 noSave=1'}elseif($Mode -eq 'Read'){'CF_FURNISHED_RESTORE PASS identity=1 inventory=1'}elseif($Room){'CF_HOME_ROOM_INPUT PASS modules=11 material=112 noSave=1'}else{'CF_HOME_INPUT_ONLY PASS noSave=1'}
if($FenceNavigation -and !$body.Contains('CF_HOME_FENCE_NAV_OPEN PASS closedWait=1 refund=2')){throw '缺少封闭等待与正常拆除返还标记'}
if($body -match '\bFAIL\b|Fatal error|Assertion failed' -or !$body.Contains($completion) -or ($Mode -ne 'Read' -and !$body.Contains('CF_HOME_INPUT_BINDINGS PASS floor=1 rotated=1 cancelled=1 material=192'))){throw "房屋输入绑定专项未通过或构建不支持：$log"}
if($Room -and $Mode -ne 'Read' -and ([regex]::Matches($body,'CF_HOME_ROOM_INPUT_PART PASS')).Count -ne 10){throw '整屋输入缺少十次新增模块的实际成功记录'}
if($Work){$number=if($Mode -eq 'Write'){1}elseif($Mode -eq 'Read'){2}else{0};if(!$body.Contains('CF_DOOR_FARM_MODE PASS mode='+$number)){throw '未完成指定的工作存读模式'}}
if($Work){foreach($marker in @('CF_HOME_ROOM_WORK PASS noRebuild=1','CF_DOOR_FARM_CLOSED PASS','CF_FURNISHED_FARM PASS','CF_DOOR_FARM PASS')){if(!$body.Contains($marker)){throw "建房后伙伴工作链缺少必要标记：$marker"}}}
if($Work -and $Mode -ne 'Read'){foreach($marker in @('CF_HOME_FARM_INPUT PASS','CF_HOME_FARM_SEED_INPUT PASS')){if(!$body.Contains($marker)){throw "农圃预览放置或种子输入未实际通过：$marker"}}}
if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '房屋输入测试期间模块发生变化'}
[pscustomobject]@{Room=[bool]$Room;Work=[bool]$Work;Mode=$Mode;Pass=$true;BuildSHA256=$hash;Log=$log} | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
if($FenceNavigation){Write-Host '伙伴绕入绕出、封闭等待及拆除后恢复进入通过；不代表多伙伴或长期导航验收'}
elseif($Fence -and $Mode -eq 'Write'){Write-Host '栅栏预览建造与保存通过；尚需独立读取'}
elseif($Fence -and $Mode -eq 'Read'){Write-Host '栅栏身份、支撑、成本与物资恢复及伪旧版本拒绝通过'}
elseif($Fence){Write-Host '栅栏预览放置、旋转、阻挡、拆除返还和非承重规则通过；存读与伙伴导航尚待'}
elseif($Mode -eq 'Write'){Write-Host '输入建房并保存闭门等待任务通过；仍需独立进程读取后继续工作'}
elseif($Mode -eq 'Read'){Write-Host '输入建房存档的身份、物资及原任务恢复通过；完整链结果由存档脚本汇总'}
elseif($Work){Write-Host '输入建房与原房伙伴闭门等待、开门工作及物资核对通过；未验收系统鼠标或自然移动建房'}
elseif($Room){Write-Host '双格门窗房屋及储物箱共11模块预览输入通过；不是系统鼠标或自然移动建房验收'}
else{Write-Host '地板预览放置、墙预览旋转与取消绑定通过；不是系统鼠标或整屋人工建造验收'}
} finally {
    try {foreach($entry in $protected){
        if($entry.Existed){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.Hash){throw '房屋输入测试原槽恢复失败'}}
        elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}
    }} finally {$mutex.ReleaseMutex();$mutex.Dispose()}
}
