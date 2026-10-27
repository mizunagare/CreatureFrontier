param([string]$BuildRoot = '')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $BuildRoot) { $BuildRoot = Join-Path $projectRoot 'Builds\Windows' }
$exePath = Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
if (-not (Test-Path -LiteralPath $exePath)) { throw "Development game executable not found: $exePath" }
$testLogRoot = Join-Path $projectRoot 'Saved\PackagedTests'
New-Item -ItemType Directory -Path $testLogRoot -Force | Out-Null
$cases = @(
    @{Flag='CFSmokeTest'; Marker='CF_SMOKE PASS'},
    @{Flag='CFInventoryTest'; Marker='CF_INVENTORY PASS'},
    @{Flag='CFCareTest'; Marker='CF_CARE PASS'},
    @{Flag='CFSaveWrite'; Marker='CF_SAVE_WRITE PASS'},
    @{Flag='CFSaveRead'; Marker='CF_SAVE_READ PASS'},
    @{Flag='CFNavTest'; Marker='CF_NAV_WORK PASS'}
)
foreach ($case in $cases) {
    $logPath = Join-Path $testLogRoot ($case.Flag + '.log')
    $testProcess = Start-Process -FilePath $exePath -WorkingDirectory $BuildRoot `
        -ArgumentList @('-nullrhi', '-nosound', '-unattended', '-nop4', "-$($case.Flag)", "-abslog=`"$logPath`"") `
        -WindowStyle Hidden -Wait -PassThru
    if ($testProcess.ExitCode -ne 0) { throw "$($case.Flag) failed with exit code $($testProcess.ExitCode)" }
    if (-not (Select-String -LiteralPath $logPath -SimpleMatch $case.Marker -Quiet)) {
        throw "$($case.Flag) did not emit its success marker; inspect $logPath"
    }
    Write-Host "$($case.Flag): PASS"
}
