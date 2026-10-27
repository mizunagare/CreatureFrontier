param([string]$BuildRoot = '', [int]$TimeoutSeconds = 60)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $BuildRoot) {
    $candidates = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Builds') -Directory | ForEach-Object {
        $binary = Join-Path $_.FullName 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
        if (Test-Path -LiteralPath $binary) {
            [pscustomobject]@{ Root=$_.FullName; Built=(Get-Item -LiteralPath $binary).LastWriteTimeUtc }
        }
    } | Sort-Object Built -Descending)
    if ($candidates.Count -eq 0) { throw 'No packaged Development build found' }
    $BuildRoot = $candidates[0].Root
}
if ($TimeoutSeconds -lt 10 -or $TimeoutSeconds -gt 60) { throw 'TimeoutSeconds must be between 10 and 60' }
Write-Host "Testing build: $BuildRoot"
$exePath = Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
if (-not (Test-Path -LiteralPath $exePath)) { throw "Development game executable not found: $exePath" }
$runName = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$testLogRoot = Join-Path $projectRoot (Join-Path 'Saved\PackagedTests' $runName)
New-Item -ItemType Directory -Path $testLogRoot -Force | Out-Null
Write-Host "Fresh test logs: $testLogRoot"
$cases = @(
    @{Flag='CFSmokeTest'; Marker='CF_SMOKE PASS'},
    @{Flag='CFInventoryTest'; Marker='CF_INVENTORY PASS'},
    @{Flag='CFCareTest'; Marker='CF_CARE PASS'},
    @{Flag='CFSpeciesTest'; Marker='CF_SPECIES PASS'},
    @{Flag='CFCombatTest'; Marker='CF_COMBAT PASS'},
    @{Flag='CFEncounterTest'; Marker='CF_ENCOUNTER PASS'},
    @{Flag='CFBossTest'; Marker='CF_BOSS PASS'},
    @{Flag='CFBuildingTest'; Marker='CF_BUILD PASS'},
    @{Flag='CFWorldTest'; Marker='CF_WORLD PASS'},
    @{Flag='CFFarmingTest'; Marker='CF_FARM PASS'},
    @{Flag='CFFeedingTest'; Marker='CF_FEED PASS'},
    @{Flag='CFWorkCycleTest'; Marker='CF_WORK_CYCLE PASS'},
    @{Flag='CFSquadTest'; Marker='CF_SQUAD PASS'},
    @{Flag='CFJournalTest'; Marker='CF_JOURNAL PASS'},
    @{Flag='CFObjectiveTest'; Marker='CF_OBJECTIVE PASS'},
    @{Flag='CFRespawnTest'; Marker='CF_RESPAWN PASS'},
    @{Flag='CFEquipmentTest'; Marker='CF_EQUIPMENT PASS'},
    @{Flag='CFMeleeTest'; Marker='CF_MELEE PASS'},
    @{Flag='CFLogisticsTest'; Marker='CF_LOGISTICS PASS'},
    @{Flag='CFSaveWrite'; Marker='CF_SAVE_WRITE PASS'},
    @{Flag='CFSaveRead'; Marker='CF_SAVE_READ PASS'},
    @{Flag='CFNavTest'; Marker='CF_NAV_WORK PASS'}
)
foreach ($case in $cases) {
    $logPath = Join-Path $testLogRoot ($case.Flag + '.log')
    $testProcess = Start-Process -FilePath $exePath -WorkingDirectory $BuildRoot `
        -ArgumentList @('-nullrhi', '-nosound', '-unattended', '-nop4', "-$($case.Flag)", "-abslog=`"$logPath`"") `
        -WindowStyle Hidden -PassThru
    if (-not $testProcess.WaitForExit($TimeoutSeconds * 1000)) {
        $testProcess.Kill()
        throw "$($case.Flag) exceeded $TimeoutSeconds seconds; test process stopped. Inspect $logPath"
    }
    if ($testProcess.ExitCode -ne 0) { throw "$($case.Flag) failed with exit code $($testProcess.ExitCode)" }
    if (-not (Select-String -LiteralPath $logPath -SimpleMatch $case.Marker -Quiet)) {
        throw "$($case.Flag) did not emit its success marker; inspect $logPath"
    }
    Write-Host "$($case.Flag): PASS"
}
