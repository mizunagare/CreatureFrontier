param([Parameter(Mandatory=$true)][string]$LegacyBuildRoot,
      [Parameter(Mandatory=$true)][string]$BuildRoot)

$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
if(!(Test-Path -LiteralPath $exe)){throw 'Current executable missing'}
$run=Join-Path $projectRoot ('Saved\CargoLegacyAudit\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-Verified')
New-Item -ItemType Directory -Path $run | Out-Null
$cases=@(
    @{Slot='StorageCross'; Flag='CFStorageRead'; Marker='CF_STORAGE_READ PASS'},
    @{Slot='FrontierHomeCrossProcess'; Flag='CFHomeRead'; Marker='CF_HOME_READ PASS'},
    @{Slot='FieldCross'; Flag='CFFieldRead'; Marker='CF_FIELD_READ PASS'},
    @{Slot='FrontierExpeditionCrossProcess'; Flag='CFExpeditionRead'; Marker='CF_EXPEDITION_READ PASS'}
)
$results=@()
foreach($case in $cases){
    $source=Join-Path $LegacyBuildRoot ('CreatureFrontier\Saved\SaveGames\'+$case.Slot+'.sav')
    $target=Join-Path $BuildRoot ('CreatureFrontier\Saved\SaveGames\'+$case.Slot+'.sav')
    if(!(Test-Path -LiteralPath $source)){throw "Legacy input missing: $source"}
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
            $results += [pscustomobject]@{Slot=$case.Slot; Attempt=$attempt; Pass=$pass; InputSHA256=(Get-FileHash -LiteralPath $legacyInput).Hash; BuildSHA256=(Get-FileHash -LiteralPath $exe).Hash}
            $results | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
            Write-Host "$($case.Flag) attempt=$attempt pass=$pass"
            if(!$pass){throw "Legacy read failed; see $log"}
        }
    } finally {
        if($existed){
            Copy-Item -LiteralPath $backup -Destination $target
            if((Get-FileHash -LiteralPath $target).Hash -ne $originalHash){throw "Test slot restore mismatch: $target"}
        } elseif(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target}
    }
}
Write-Host "Legacy audit passed; current test slots restored. Evidence: $run"
