param([switch]$Editor,[string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$projectRoot=Split-Path $PSScriptRoot -Parent
if($Editor){
    $exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    $module=Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'
    $saveRoot=Join-Path $projectRoot 'Saved\SaveGames'
}else{
    if(!$BuildRoot){throw '请指定 Editor 或 BuildRoot'}
    $exe=Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
    $module=$exe
    $saveRoot=Join-Path $BuildRoot 'CreatureFrontier\Saved\SaveGames'
}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved\DoorQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "实体门专项证据：$run"
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierDoorQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '实体门专项正在使用相同测试槽'}
$backups=@()
$results=@()
try{
    foreach($slot in @('DoorLegacy29','DoorGood','DoorBad','DoorOpen')){
        $path=Join-Path $saveRoot ($slot+'.sav')
        if(Test-Path -LiteralPath $path){
            $backup=Join-Path $run ($slot+'-before.sav')
            Copy-Item -LiteralPath $path -Destination $backup
            $backups+=@{Path=$path;Backup=$backup;Hash=(Get-FileHash -LiteralPath $path).Hash}
        }
    }
    foreach($case in @(@('CFDoorTest','CF_DOOR PASS'),@('CFDoorOpenRead','CF_DOOR_OPEN_READ PASS'),@('CFDoorClosedRead','CF_DOOR_CLOSED_READ PASS'),@('CFDoorNavigationTest','CF_DOOR_NAV PASS yaw=0'),@('CFDoorNavigationRotatedTest','CF_DOOR_NAV PASS yaw=90'))){
    $log=Join-Path $run ($case[0]+'.log')
    $arguments=@('-game',('-'+$case[0]),'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    $openInput=Join-Path $saveRoot 'DoorOpen.sav';$closedInput=Join-Path $saveRoot 'DoorGood.sav'
    $inputHashes=if($case[0] -ne 'CFDoorTest'){@((Get-FileHash -LiteralPath $openInput).Hash,(Get-FileHash -LiteralPath $closedInput).Hash)}else{@()}
    if($Editor){$arguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'))+$arguments}
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw '实体门专项超时'}
    if($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch $case[1] -Quiet) -or (Select-String -LiteralPath $log -Pattern 'CF_\w+ FAIL|Fatal error|Assertion failed' -Quiet)){throw "实体门专项未通过：$log"}
    if($case[0] -like 'CFDoorNavigation*' -and !(Select-String -LiteralPath $log -SimpleMatch 'CF_DOOR_NAV_CLOSED PASS' -Quiet)){throw '缺少真实闭门等待成功标记'}
    if($inputHashes.Count -and ((Get-FileHash -LiteralPath $openInput).Hash -ne $inputHashes[0] -or (Get-FileHash -LiteralPath $closedInput).Hash -ne $inputHashes[1])){throw '跨进程读取改写了实体门输入存档'}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '专项期间程序模块发生变化'}
    $results+=[pscustomobject]@{Case=$case[0];Pass=$true;BuildSHA256=$hash;Log=$log}
    $results | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host ($case[0]+' 已通过')
    }
    foreach($slot in @('DoorLegacy29','DoorGood','DoorBad','DoorOpen')){
        $path=Join-Path $saveRoot ($slot+'.sav')
        if(Test-Path -LiteralPath $path){Copy-Item -LiteralPath $path -Destination (Join-Path $run ($slot+'.sav'))}
    }
    Write-Host '实体门五进程专项通过；图形与扩展导航样本另行验收'
}finally{
    try{foreach($entry in $backups){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.Hash){throw '原实体门测试槽恢复校验失败'}}}
    finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
