param([Parameter(Mandatory=$true)][string]$LegacyBuildRoot,
      [Parameter(Mandatory=$true)][string]$BuildRoot,
      [switch]$IncludeCrops,
      [switch]$IncludeNames,
      [switch]$IncludeMemories,
      [switch]$IncludeJourneys)

$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
if(!(Test-Path -LiteralPath $exe)){throw '当前构建的可执行程序不存在'}
$buildHash=(Get-FileHash -LiteralPath $exe).Hash
$run=Join-Path $projectRoot ('Saved\CargoLegacyAudit\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-Verified')
New-Item -ItemType Directory -Path $run | Out-Null
$cases=@(
    @{Slot='StorageCross'; Flag='CFStorageRead'; Marker='CF_STORAGE_READ PASS'},
    @{Slot='FrontierHomeCrossProcess'; Flag='CFHomeRead'; Marker='CF_HOME_READ PASS'},
    @{Slot='FieldCross'; Flag='CFFieldRead'; Marker='CF_FIELD_READ PASS'},
    @{Slot='FrontierExpeditionCrossProcess'; Flag='CFExpeditionRead'; Marker='CF_EXPEDITION_READ PASS'}
)
if(Test-Path -LiteralPath (Join-Path $LegacyBuildRoot 'CreatureFrontier\Saved\SaveGames\CargoCross.sav')){
    $cases += @{Slot='CargoCross'; Flag='CFCargoRead'; Marker='CF_CARGO_READ PASS'}
}
if(Test-Path -LiteralPath (Join-Path $LegacyBuildRoot 'CreatureFrontier\Saved\SaveGames\MaterialsCross.sav')){
    $cases += @{Slot='MaterialsCross'; Flag='CFMaterialsRead'; Marker='CF_MATERIALS_READ PASS'}
}
if($IncludeCrops -and (Test-Path -LiteralPath (Join-Path $LegacyBuildRoot 'CreatureFrontier\Saved\SaveGames\FrontierFarmingAutomation.sav'))){
    $cases += @{Slot='FrontierFarmingAutomation'; Flag='CFCropLegacyRead'; Marker='CF_CROP_LEGACY_READ PASS'}
}
$results=@()
if(Test-Path -LiteralPath (Join-Path $LegacyBuildRoot 'CreatureFrontier\Saved\SaveGames\BefriendCross.sav')){
    $cases += @{Slot='BefriendCross';Flag='CFBefriendRead';Marker='CF_BEFRIEND_READ PASS'}
}
if(Test-Path -LiteralPath (Join-Path $LegacyBuildRoot 'CreatureFrontier\Saved\SaveGames\HollowCross.sav')){
    $cases += @{Slot='HollowCross';Flag='CFHollowRead';Marker='CF_HOLLOW_READ PASS'}
}
if(Test-Path -LiteralPath (Join-Path $LegacyBuildRoot 'CreatureFrontier\Saved\SaveGames\DefenseCross.sav')){
    $cases += @{Slot='DefenseCross';Flag='CFDefenseRead';Marker='CF_DEFENSE_READ PASS'}
}
if(Test-Path -LiteralPath (Join-Path $LegacyBuildRoot 'CreatureFrontier\Saved\SaveGames\HollowAdventureCross.sav')){
    $cases += @{Slot='HollowAdventureCross';Flag='CFHollowAdventureRead';Marker='CF_HOLLOW_ADVENTURE_READ PASS'}
}
if($IncludeJourneys){$cases+=@{Slot='MemoryDoneCross';Flag='CFMemoryDoneRead';Marker='CF_MEMORY_DONE_READ PASS'}}
if(Test-Path -LiteralPath (Join-Path $LegacyBuildRoot 'CreatureFrontier\Saved\SaveGames\HollowRecoveryCross.sav')){
    $cases+=@{Slot='HollowRecoveryCross';Flag='CFHollowRecoveryRead';Marker='CF_HOLLOW_RECOVERY_READ PASS'}
}
if(Test-Path -LiteralPath (Join-Path $LegacyBuildRoot 'CreatureFrontier\Saved\SaveGames\HollowCheckpointFinal.sav')){
    $cases+=@{Slot='HollowCheckpointFinal';Flag='CFHollowCheckpointRead';Marker='CF_HOLLOW_CHECKPOINT_READ PASS'}
}
if($IncludeMemories){$cases+=@{Slot='PersonalityCross';Flag='CFMemoryLegacyRead';Marker='CF_MEMORY_LEGACY_READ PASS'}}
if($IncludeNames){$cases+=@{Slot='NameCross';Flag='CFNameRead';Marker='CF_NAME_READ PASS'};$cases+=@{Slot='NameCross';Flag='CFPersonalityLegacyRead';Marker='CF_PERSONALITY_LEGACY_READ PASS'}}
foreach($case in $cases){
    $source=Join-Path $LegacyBuildRoot ('CreatureFrontier\Saved\SaveGames\'+$case.Slot+'.sav')
    $target=Join-Path $BuildRoot ('CreatureFrontier\Saved\SaveGames\'+$case.Slot+'.sav')
    if(!(Test-Path -LiteralPath $source)){throw "缺少旧版输入存档：$source"}
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    $backup=Join-Path $run ($case.Slot+'.current-backup')
    $existed=Test-Path -LiteralPath $target
    $originalHash=if($existed){(Get-FileHash -LiteralPath $target).Hash}else{''}
    if($existed){Copy-Item -LiteralPath $target -Destination $backup}
    $legacyInput=Join-Path $run ($case.Slot+'.legacy-input')
    Copy-Item -LiteralPath $source -Destination $legacyInput
    try {
        Copy-Item -LiteralPath $legacyInput -Destination $target
        foreach($attempt in 1..2){
            $log=Join-Path $run ($case.Flag+'-'+$attempt+'.log')
            $process=Start-Process -FilePath $exe -WorkingDirectory $BuildRoot -WindowStyle Hidden -PassThru -ArgumentList @('-nullrhi','-nosound','-unattended','-nop4',('-'+$case.Flag),('-abslog="'+$log+'"'))
            $done=$process.WaitForExit(60000)
            if(!$done){$process.Kill();$process.WaitForExit()}
            $pass=$done -and $process.ExitCode -eq 0 -and (Test-Path -LiteralPath $log) -and [bool](Select-String -LiteralPath $log -SimpleMatch $case.Marker)
            if($pass){$pass=!(Select-String -LiteralPath $log -Pattern 'CF_\w+ FAIL|Fatal error|Assertion failed' -Quiet)}
            if((Get-FileHash -LiteralPath $exe).Hash -ne $buildHash){throw '旧档审计期间测试程序发生变化'}
            $results += [pscustomobject]@{Slot=$case.Slot; Attempt=$attempt; Pass=$pass; InputSHA256=(Get-FileHash -LiteralPath $legacyInput).Hash; BuildSHA256=(Get-FileHash -LiteralPath $exe).Hash}
            $results | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
            Write-Host "$($case.Flag) 第 $attempt 次读取，通过：$pass"
            if(!$pass){throw "旧档读取失败，详见：$log"}
        }
    } finally {
        if($existed){
            Copy-Item -LiteralPath $backup -Destination $target
            if((Get-FileHash -LiteralPath $target).Hash -ne $originalHash){throw "测试存档槽恢复校验不一致：$target"}
        } elseif(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target}
    }
}
Write-Host "旧档审计通过，当前测试存档槽已恢复。证据：$run"
