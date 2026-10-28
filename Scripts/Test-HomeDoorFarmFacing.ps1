param([double[]]$YawAngles=@(-160,-90,0,90),[string]$EngineRoot='D:\Epic\UE_5.8',[string]$BuildRoot='')
$ErrorActionPreference='Stop'
if(!$YawAngles.Count){throw '朝向列表不能为空'}
foreach($yaw in $YawAngles){if([double]::IsNaN($yaw) -or [double]::IsInfinity($yaw) -or $yaw -lt -180 -or $yaw -gt 180){throw '朝向必须为-180到180度的有限数'}}
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved\DoorFarmFacingQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "合法站位朝向对照：$run"
$results=@()
foreach($yaw in $YawAngles){
    $pass=$false;$detail=''
    try {& (Join-Path $PSScriptRoot 'Test-HomeDoorFarm.ps1') -EngineRoot $EngineRoot -BuildRoot $BuildRoot -Mode Cancel -FollowYaw $yaw -FacingProbe;$pass=$true}
    catch {$detail=$_.Exception.Message;Write-Host "朝向 $yaw 未通过：$detail"}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '朝向矩阵期间程序发生变化，不能合并结果'}
    $results += [pscustomobject]@{Yaw=$yaw;Pass=$pass;BuildSHA256=$hash;Detail=$detail}
    $results | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
}
if(@($results | Where-Object {!$_.Pass}).Count){throw "朝向矩阵存在失败，全部结果已保留：$run"}
Write-Host '本次全部朝向样本通过；不代表任意房间与队列均通过'
