param([string]$BuildRoot='', [string]$ReadInputPath='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$program=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}
$module=if($BuildRoot){$program}else{Join-Path $root 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved'}else{Join-Path $root 'Saved'}
$slots=Join-Path $saved 'SaveGames'
$run=Join-Path $root ('Saved\DoorConstructionQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run,$slots -Force|Out-Null
$protected=@();$started=$null;$hash=(Get-FileHash -LiteralPath $module).Hash
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierDoorConstructionQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '木门施工测试槽正在使用'}
try{
    foreach($name in @('DoorConstructionInput','DoorConstructionMid','DoorConstructionDone','DoorConstructionBad')){
        $path=Join-Path $slots ($name+'.sav');$backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path -LiteralPath $path){(Get-FileHash -LiteralPath $path).Hash}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    $inputHash='';if($ReadInputPath){$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path;if($ReadInputPath -eq (Join-Path $slots 'DoorConstructionInput.sav')){throw '恢复来源须为独立副本'};$inputHash=(Get-FileHash -LiteralPath $ReadInputPath).Hash;Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slots 'DoorConstructionInput.sav')}
    $log=Join-Path $run 'Door.log';$flag=if($ReadInputPath){'-CFDoorConstructionRead'}else{'-CFDoorConstructionTest'}
    $arguments=@($flag,'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    [pscustomobject]@{BuildSHA256=$hash;Input=$ReadInputPath;InputSHA256=$inputHash}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    Write-Host ('木门施工证据：'+$run);$started=[DateTime]::UtcNow;$p=Start-Process -FilePath $program -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(120000)){$p.Kill();$p.WaitForExit();throw '木门施工超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('CF_DOOR_CONSTRUCTION PASS paid=12 total=26 closed=1 carried=0 restored=1') -or !$raw.Contains('LogExit: Exiting.')){throw '木门施工验证失败'}
    if(!$ReadInputPath -and !$raw.Contains('CF_DOOR_CONSTRUCTION_MID PASS carried=3 unfinishedToggle=0 badOpenRejected=1')){throw '缺少携料和非法状态检查'}
    if(!$ReadInputPath -and !$raw.Contains('CF_DOOR_CONSTRUCTION_PASSAGE PASS capsule=1 collisionRetained=1')){throw '缺少门工地实体通行检查'}
    if((Get-FileHash -LiteralPath $module).Hash -ne $hash -or ($ReadInputPath -and (Get-FileHash -LiteralPath $ReadInputPath).Hash -ne $inputHash)){throw '程序或来源改变'}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '木门搬运施工与存读通过；独立夹具无图形，不代表完整建房体验'
}finally{
    try{foreach($entry in $protected){if($started -and (Test-Path -LiteralPath $entry.Path) -and (Get-Item -LiteralPath $entry.Path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ((Split-Path $entry.Path -LeafBase)+'-generated.sav'))};if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.Hash){throw '原木门测试槽恢复失败'}}elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}}}finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
