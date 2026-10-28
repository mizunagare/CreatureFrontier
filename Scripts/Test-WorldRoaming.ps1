param(
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [int[]]$Seeds = @((1..12) + @(1549,3028)),
    [int[]]$FrameCaps = @(0,60),
    [ValidateRange(1,10)][int]$Repeats = 1
)

$ErrorActionPreference = 'Stop'
if (!$Seeds.Count -or !$FrameCaps.Count) { throw '随机种子和帧率上限列表不能为空' }
if (@($Seeds | Select-Object -Unique).Count -ne $Seeds.Count) { throw '随机种子不能重复' }
if (@($FrameCaps | Select-Object -Unique).Count -ne $FrameCaps.Count) { throw '帧率上限不能重复' }
foreach ($cap in $FrameCaps) { if ($cap -lt 0 -or $cap -gt 240) { throw '帧率上限必须在 0 至 240 之间' } }
$binary = Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
if (!(Test-Path -LiteralPath $binary)) { throw "未找到游戏程序：$binary" }
$binary = (Resolve-Path -LiteralPath $binary).Path
$BuildRoot = (Resolve-Path -LiteralPath $BuildRoot).Path
$binaryHash = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
$projectRoot = Split-Path $PSScriptRoot -Parent
$runName = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$logRoot = Join-Path $projectRoot "Saved\WorldRoaming\$runName"
New-Item -ItemType Directory -Path $logRoot | Out-Null
Write-Host "日志目录：$logRoot"
$results = @()
$cases = @()
foreach ($seed in $Seeds) { foreach ($cap in $FrameCaps) { foreach ($repeat in 1..$Repeats) {
    $cases += [pscustomobject]@{Seed=$seed;Cap=$cap;Repeat=$repeat;Frozen=$false}
} } }
foreach ($cap in $FrameCaps) { $cases += [pscustomobject]@{Seed=1549;Cap=$cap;Repeat=1;Frozen=$true} }
foreach ($case in $cases) {
    $seed=$case.Seed; $cap=$case.Cap; $repeat=$case.Repeat
    $label=if($case.Frozen){'Frozen'}else{'Roaming'}
    $logPath = Join-Path $logRoot "$label-Seed-$seed-Fps-$cap-Run-$repeat.log"
    $arguments = @('-nullrhi','-nosound','-unattended','-nop4','-CFWorldTest','-CFRoamTrace',
        "-CFRoamSeed=$seed", "-ExecCmds=`"t.MaxFPS $cap`"", "-abslog=`"$logPath`"")
    if($case.Frozen) { $arguments += '-CFWorldFreeze' }
    $process = Start-Process -FilePath $binary -WorkingDirectory $BuildRoot -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $timeout = !$process.WaitForExit(60000)
    if ($timeout) { $process.Kill(); $process.WaitForExit(5000) | Out-Null }
    $exitCode = $process.ExitCode
    $seedConfirmed = $false; $capConfirmed = $false; $marker = $false; $fixture = $false
    if (Test-Path -LiteralPath $logPath) {
        $seedConfirmed = Select-String -LiteralPath $logPath -Pattern "CF_WORLD_SEED $seed$" -Quiet
        $capConfirmed = Select-String -LiteralPath $logPath -SimpleMatch "t.MaxFPS = `"$cap`"" -Quiet
        $fixture = Select-String -LiteralPath $logPath -SimpleMatch "CF_WORLD_FIXTURE frozen=$([int]$case.Frozen)" -Quiet
        if($case.Frozen) {
            $marker = (Select-String -LiteralPath $logPath -SimpleMatch 'CF_WORLD FAIL scene=1 roam=0 save=1 immune=1 reset=1 tame=1' -Quiet) -and
                (Select-String -LiteralPath $logPath -SimpleMatch 'CF_WORLD_ROAM pass=0 travel=0.00 path=0.00 legs=0 continuous=1 within=1' -Quiet)
        } else { $marker = Select-String -LiteralPath $logPath -SimpleMatch 'CF_WORLD PASS' -Quiet }
    }
    # UE 调用 RequestExitWithStatus(false,1) 后仍可能返回零。
    # 负向测试以明确拒绝标记及 OTHER 世界未改变的断言为准。
    $validExit=if($case.Frozen){$exitCode -in @(0,1)}else{$exitCode -eq 0}
    $pass = !$timeout -and $validExit -and $seedConfirmed -and $capConfirmed -and $fixture -and $marker
    $expected=if($case.Frozen){'RejectFrozen'}else{'AcceptRoaming'}
    $results += [pscustomobject]@{Seed=$seed;FrameCap=$cap;Repeat=$repeat;Expected=$expected;Pass=$pass;Timeout=$timeout;
        ExitCode=$exitCode;SeedConfirmed=$seedConfirmed;CapConfirmed=$capConfirmed;Fixture=$fixture;SHA256=$binaryHash;Log=$logPath}
    $results | Export-Csv -LiteralPath (Join-Path $logRoot 'Results.csv') -NoTypeInformation -Encoding UTF8
    Write-Host "随机种子=$seed 帧率上限=$cap 重复次数=$repeat 预期=$expected 通过=$pass"
}
$failed = @($results | Where-Object { !$_.Pass })
if ($failed.Count) { throw "$($failed.Count)/$($results.Count) 项失败；请检查日志目录：$logRoot" }
Write-Host "世界巡游：$($results.Count)/$($results.Count) 项通过；程序指纹 SHA256=$binaryHash"
