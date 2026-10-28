$ErrorActionPreference = "Continue"

Write-Host "幻兽边境开发环境检查"

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
                Write-Verbose "无法读取 Epic 安装清单：$($_.FullName)"
            }
        }
}

$engine = $engineCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

[pscustomobject]@{
    '虚幻编辑器' = if ($engine) { $engine } else { "未安装" }
    'Visual Studio' = if (Test-Path -LiteralPath $vswhere) { & $vswhere -latest -property installationPath } else { "未安装" }
    Git = if (Get-Command git -ErrorAction SilentlyContinue) { git --version } else { "未安装" }
    GitLFS = if (Get-Command git -ErrorAction SilentlyContinue) {
        $lfsVersion = git lfs version 2>$null
        if ($LASTEXITCODE -eq 0) { $lfsVersion } else { "未安装" }
    } else { "未安装" }
    DotNetSDK = if (Get-Command dotnet -ErrorAction SilentlyContinue) {
        $sdks = @(dotnet --list-sdks)
        if ($sdks.Count -gt 0) { $sdks -join ", " } else { "未安装（仅有运行时）" }
    } else { "未安装" }
} | Format-List

if (-not $engine) {
    Write-Warning "请通过 Epic Games 启动器安装 UE 5.8，并勾选模板和功能包。"
    exit 1
}
