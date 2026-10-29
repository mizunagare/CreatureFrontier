param([string]$EngineRoot='D:\Epic\UE_5.8',[string]$BuildRoot='',[switch]$Furnished,[switch]$InputBuilt,[string]$ReadInputPath='')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
if($ReadInputPath -and !$InputBuilt){throw '真实旧档读取入口仅用于输入建房工作链'}
$projectRoot=Split-Path $PSScriptRoot -Parent
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$buildHash=Get-FrontierBuildFingerprint -LiteralPath $module
$saveRoot=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved\SaveGames'}else{Join-Path $projectRoot 'Saved\SaveGames'}
$slotName=if($InputBuilt){'HomeInputWorkBlocked'}elseif($Furnished){'FurnishedFarmBlocked'}else{'DoorFarmBlocked'}
$slot=Join-Path $saveRoot ($slotName+'.sav')
if($ReadInputPath){
    $ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
    if([IO.Path]::GetFullPath($slot) -eq $ReadInputPath){throw '真实输入不能与临时读取槽相同'}
    $sourceHash=(Get-FileHash -LiteralPath $ReadInputPath).Hash
}
$run=Join-Path $projectRoot ('Saved\DoorFarmPersistenceQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "农圃跨进程恢复链：$run"
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierDoorFarmPersistenceQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '农圃跨进程测试槽正在使用'}
$existed=Test-Path -LiteralPath $slot
$backup=Join-Path $run ($slotName+'-before.sav')
try {
    if($existed){$originalHash=(Get-FileHash -LiteralPath $slot).Hash;Copy-Item -LiteralPath $slot -Destination $backup}
    if($ReadInputPath){
        New-Item -ItemType Directory -Path $saveRoot -Force | Out-Null
        Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $run 'Legacy-source.sav')
        Copy-Item -LiteralPath $ReadInputPath -Destination $slot
    }
    elseif($InputBuilt){& (Join-Path $PSScriptRoot 'Test-HomeInput.ps1') -EngineRoot $EngineRoot -BuildRoot $BuildRoot -Work -Mode Write}
    else{& (Join-Path $PSScriptRoot 'Test-HomeDoorFarm.ps1') -EngineRoot $EngineRoot -BuildRoot $BuildRoot -Mode Write -Furnished:$Furnished}
    if(!(Test-Path -LiteralPath $slot)){throw '闭门受阻写入没有生成存档'}
    $inputHash=(Get-FileHash -LiteralPath $slot).Hash
    Copy-Item -LiteralPath $slot -Destination (Join-Path $run ($slotName+'-input.sav'))
    foreach($attempt in 1..2){
        if($InputBuilt){& (Join-Path $PSScriptRoot 'Test-HomeInput.ps1') -EngineRoot $EngineRoot -BuildRoot $BuildRoot -Work -Mode Read}
        else{& (Join-Path $PSScriptRoot 'Test-HomeDoorFarm.ps1') -EngineRoot $EngineRoot -BuildRoot $BuildRoot -Mode Read -Furnished:$Furnished}
        if((Get-FileHash -LiteralPath $slot).Hash -ne $inputHash){throw '恢复读取改写了闭门输入'}
        if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $buildHash){throw '跨进程链期间测试模块变化'}
        if($ReadInputPath -and (Get-FileHash -LiteralPath $ReadInputPath).Hash -ne $sourceHash){throw '真实旧档源文件发生变化'}
        [pscustomobject]@{Attempt=$attempt;Pass=$true;InputSHA256=$inputHash;SourcePath=$ReadInputPath;BuildSHA256=$buildHash} | Export-Csv -LiteralPath (Join-Path $run ('Read-'+$attempt+'.csv')) -NoTypeInformation
    }
    if($ReadInputPath){Write-Host '真实输入两个独立进程读取并继续工作通过；源输入未改写'}else{Write-Host '闭门写入和两个独立进程开门恢复均通过；输入未改写'}
} finally {
    try {
        if($existed){Copy-Item -LiteralPath $backup -Destination $slot;if((Get-FileHash -LiteralPath $slot).Hash -ne $originalHash){throw '农圃测试槽原档恢复失败'}}
        elseif(Test-Path -LiteralPath $slot){Remove-Item -LiteralPath $slot}
    } finally {$mutex.ReleaseMutex();$mutex.Dispose()}
}
