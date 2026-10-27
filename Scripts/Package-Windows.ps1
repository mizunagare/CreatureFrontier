param(
    [string]$EngineRoot = 'D:\Epic\UE_5.8',
    [string]$Configuration = 'Development'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$uatPath = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
if (-not (Test-Path -LiteralPath $uatPath)) { throw "Unreal Automation Tool not found: $uatPath" }
if ($Configuration -notin @('Development', 'Shipping')) { throw 'Configuration must be Development or Shipping' }

& $uatPath BuildCookRun "-project=$projectRoot\CreatureFrontier.uproject" -noP4 -platform=Win64 `
    "-clientconfig=$Configuration" -build -cook -map=/Game/Maps/L_Prototype -stage -pak -archive `
    "-archivedirectory=$projectRoot\Builds\Windows" -utf8output -unattended '-UbtArgs=-MaxParallelActions=2'
if ($LASTEXITCODE -ne 0) { throw "Packaging failed with exit code $LASTEXITCODE" }
Write-Host "Windows build: $projectRoot\Builds\Windows\CreatureFrontier.exe"
