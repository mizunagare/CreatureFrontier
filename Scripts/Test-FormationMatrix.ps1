param(
    [string]$BuildRoot='',
    [switch]$Editor,
    [string]$EngineRoot='D:\Epic\UE_5.8',
    [int[]]$FrameCaps=@(30,60,120),
    [int[]]$LeaderForms=@(0,1,2),
    [int[]]$SideOrders=@(0,1),
    [ValidateRange(1,10)][int]$Repeats=2,
    [switch]$Trace
)

$ErrorActionPreference='Stop'
if(!$FrameCaps.Count -or !$LeaderForms.Count -or !$SideOrders.Count) { throw '矩阵轴不能为空' }
foreach($cap in $FrameCaps) { if($cap -ne 0 -and ($cap -lt 15 -or $cap -gt 240)) { throw '帧率上限须为0（不限）或15到240' } }
foreach($leader in $LeaderForms) { if($leader -lt 0 -or $leader -gt 2) { throw '领队形态须为0（草叶）、1（炎火）或2（岩石）' } }
foreach($order in $SideOrders) { if($order -ne 0 -and $order -ne 1) { throw '队列顺序须为0（循环）或1（逆序）' } }
if(@($FrameCaps | Select-Object -Unique).Count -ne $FrameCaps.Count -or
    @($LeaderForms | Select-Object -Unique).Count -ne $LeaderForms.Count -or
    @($SideOrders | Select-Object -Unique).Count -ne $SideOrders.Count) { throw '矩阵轴不能有重复值' }
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
if($Editor){
    if($BuildRoot){throw 'Editor与BuildRoot不能同时指定'}
    $resolvedRoot=$projectRoot
    $exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    $module=Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'
}else{
    if(!$BuildRoot){throw '请指定Editor或BuildRoot'}
    $resolvedRoot=(Resolve-Path -LiteralPath $BuildRoot).Path
    $exe=Join-Path $resolvedRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
    $module=$exe
}
if(!(Test-Path -LiteralPath $exe)) { throw "找不到测试程序：$exe" }
$binaryHash=Get-FrontierBuildFingerprint -LiteralPath $module
$runName=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$logRoot=Join-Path $projectRoot "Saved\FormationMatrix\$runName"
New-Item -ItemType Directory -Path $logRoot | Out-Null
Write-Host "队形矩阵证据：$logRoot"
Write-Host "程序模块 SHA256：$binaryHash"
$results=@()
$saveRoot=if($Editor){Join-Path $projectRoot 'Saved\SaveGames'}else{Join-Path $resolvedRoot 'CreatureFrontier\Saved\SaveGames'}
$slot=Join-Path $saveRoot 'FrontierFormationAutomation.sav'
$backup=Join-Path $logRoot 'FrontierFormationAutomation-before.sav'
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierFormationMatrixQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '队形矩阵专用测试槽正在使用'}
$hadSlot=Test-Path -LiteralPath $slot
try {
if($hadSlot){$slotHash=(Get-FileHash -LiteralPath $slot).Hash;Copy-Item -LiteralPath $slot -Destination $backup}
foreach($leader in $LeaderForms) {
  foreach($order in $SideOrders) {
    foreach($cap in $FrameCaps) {
        foreach($attempt in 1..$Repeats) {
            $label="Lead-$leader-Reverse-$order-FPS-$cap-Run-$attempt"
            $log=Join-Path $logRoot "$label.log"
            $arguments=@('-nullrhi','-nosound','-unattended','-nop4','-FixedSeed','-CFFormationTest',
                "-CFFormationLead=$leader","-ExecCmds=`"t.MaxFPS $cap`"","-abslog=`"$log`"")
            if($Trace) { $arguments+='-CFFormationTrace' }
            if($order) { $arguments+='-CFFormationReverse' }
            if($Editor){$arguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'),'-game')+$arguments}
            $process=Start-Process -FilePath $exe -WorkingDirectory $resolvedRoot -ArgumentList $arguments -WindowStyle Hidden -PassThru
            $finished=$process.WaitForExit(60000)
            if(!$finished) { $process.Kill(); $process.WaitForExit(5000) | Out-Null }
            $fixture=$false; $marker=$false
            if(Test-Path -LiteralPath $log) {
                $fixture=Select-String -LiteralPath $log -SimpleMatch "CF_FORMATION_FIXTURE lead=$leader center=$leader named=1 reverse=$order" -Quiet
                $marker=Select-String -LiteralPath $log -SimpleMatch 'CF_FORMATION PASS' -Quiet
            }
            $bad=$false
            if(Test-Path -LiteralPath $log){$bad=Select-String -LiteralPath $log -Pattern '\bFAIL\b|Fatal error|Assertion failed' -Quiet}
            $passed=$finished -and $process.ExitCode -eq 0 -and $fixture -and $marker -and !$bad
            if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $binaryHash){throw '队形矩阵期间程序模块改变，不能合并验收'}
            $results += [pscustomobject]@{Case=$label;Leader=$leader;Reverse=$order;FrameCap=$cap;Finished=$finished;ExitCode=$process.ExitCode;Fixture=$fixture;PassMarker=$marker;Passed=$passed;BuildHash=$binaryHash;Log=$log}
            $results | Export-Csv -LiteralPath (Join-Path $logRoot 'Results.csv') -NoTypeInformation -Encoding utf8
            Write-Host "${label}：通过=$passed 完成=$finished 夹具=$fixture 成功标记=$marker 退出码=$($process.ExitCode)"
        }
    }
  }
}
$results | Export-Csv -LiteralPath (Join-Path $logRoot 'Results.csv') -NoTypeInformation -Encoding utf8
$failures=@($results | Where-Object { !$_.Passed })
if($failures.Count) { throw "$($failures.Count)/$($results.Count) 个队形场景失败，请检查：$logRoot" }
Write-Host "队形矩阵测试通过：$($results.Count)/$($results.Count)（FORMATION_MATRIX PASS）"
} finally {
    try {
        try {if(Test-Path -LiteralPath $slot){Copy-Item -LiteralPath $slot -Destination (Join-Path $logRoot 'FrontierFormationAutomation-generated.sav')}} finally {
        if($hadSlot){
            if(Test-Path -LiteralPath $backup){Copy-Item -LiteralPath $backup -Destination $slot;if((Get-FileHash -LiteralPath $slot).Hash -ne $slotHash){throw '队形测试槽原档恢复校验失败'}}
        }elseif(Test-Path -LiteralPath $slot){Remove-Item -LiteralPath $slot}
        }
    } finally {$mutex.ReleaseMutex();$mutex.Dispose()}
}
