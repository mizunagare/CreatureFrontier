param(
    [string]$EngineRoot = 'D:\Epic\UE_5.8',
    [string]$Configuration = 'Development',
    [string]$ArchiveDirectory = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $ArchiveDirectory) { $ArchiveDirectory = Join-Path $projectRoot 'Builds\Windows' }
$uatPath = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
if (-not (Test-Path -LiteralPath $uatPath)) { throw "找不到虚幻自动化工具：$uatPath" }
if ($Configuration -notin @('Development', 'Shipping')) { throw '构建配置必须为 Development 或 Shipping' }

& $uatPath BuildCookRun "-project=$projectRoot\CreatureFrontier.uproject" -noP4 -platform=Win64 `
    "-clientconfig=$Configuration" -build -cook -map=/Game/Maps/L_Prototype -stage -pak -archive `
    "-archivedirectory=$ArchiveDirectory" -utf8output -unattended '-UbtArgs=-MaxParallelActions=2'
if ($LASTEXITCODE -ne 0) { throw "打包失败，退出码：$LASTEXITCODE" }
Write-Host "Windows 构建目录：$ArchiveDirectory"
