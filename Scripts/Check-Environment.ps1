$ErrorActionPreference = "Continue"

Write-Host "Creature Frontier environment check"

$engineCandidates = @(
    "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe",
    "D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe",
    "D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
)

$manifestDirectory = "C:\ProgramData\Epic\EpicGamesLauncher\Data\Manifests"
if (Test-Path -LiteralPath $manifestDirectory) {
    Get-ChildItem -LiteralPath $manifestDirectory -Filter "*.item" -ErrorAction SilentlyContinue |
        ForEach-Object {
            try {
                $manifest = Get-Content -Raw -LiteralPath $_.FullName | ConvertFrom-Json
                if ($manifest.AppName -like "UE_*") {
                    $engineCandidates += Join-Path $manifest.InstallLocation "Engine\Binaries\Win64\UnrealEditor.exe"
                }
            } catch {
                Write-Verbose "Could not read Epic manifest $($_.FullName)"
            }
        }
}

$engine = $engineCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

[pscustomobject]@{
    UnrealEditor = if ($engine) { $engine } else { "MISSING" }
    VisualStudio = if (Test-Path -LiteralPath $vswhere) { & $vswhere -latest -property installationPath } else { "MISSING" }
    Git = if (Get-Command git -ErrorAction SilentlyContinue) { git --version } else { "MISSING" }
    GitLFS = if (Get-Command git -ErrorAction SilentlyContinue) {
        $lfsVersion = git lfs version 2>$null
        if ($LASTEXITCODE -eq 0) { $lfsVersion } else { "MISSING" }
    } else { "MISSING" }
    DotNetSDK = if (Get-Command dotnet -ErrorAction SilentlyContinue) {
        $sdks = @(dotnet --list-sdks)
        if ($sdks.Count -gt 0) { $sdks -join ", " } else { "MISSING (runtime only)" }
    } else { "MISSING" }
} | Format-List

if (-not $engine) {
    Write-Warning "Install UE 5.8 with Templates and Feature Packs through Epic Games Launcher."
    exit 1
}
