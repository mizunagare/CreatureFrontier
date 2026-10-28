param([switch]$Editor,[string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8',[switch]$TraceRoaming,
      [int[]]$FrameCaps=@(30,60,120),[ValidateRange(1,5)][int]$Repeats=2)
$ErrorActionPreference='Stop'
if(!$FrameCaps.Count -or @($FrameCaps | Select-Object -Unique).Count -ne $FrameCaps.Count){throw '帧率轴不能为空或重复'}
foreach($cap in $FrameCaps){if($cap -lt 15 -or $cap -gt 240){throw '帧率上限须为15到240'}}
if($Editor -and $BuildRoot){throw 'Editor和BuildRoot不能同时指定'}
if(!$Editor -and !$BuildRoot){throw '请指定Editor或BuildRoot'}
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$root=if($Editor){$projectRoot}else{(Resolve-Path -LiteralPath $BuildRoot).Path}
$exe=if($Editor){Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}else{Join-Path $root 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}
$module=if($Editor){Join-Path $root 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}else{$exe}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved\HollowRecoveryMatrix\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "洞窟恢复帧率矩阵：$run"
$saveRoot=if($Editor){Join-Path $root 'Saved\SaveGames'}else{Join-Path $root 'CreatureFrontier\Saved\SaveGames'}
$slot=Join-Path $saveRoot 'HollowRecoveryCross.sav'
$backup=Join-Path $run 'HollowRecoveryCross-before.sav'
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierHollowRecoveryMatrixQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '恢复矩阵测试槽正在使用'}
$existed=Test-Path -LiteralPath $slot
$results=@()
try {
    if($existed){$originalHash=(Get-FileHash -LiteralPath $slot).Hash;Copy-Item -LiteralPath $slot -Destination $backup}
    foreach($cap in $FrameCaps){foreach($attempt in 1..$Repeats){
        $label="FPS-$cap-Run-$attempt";$log=Join-Path $run ($label+'.log')
        $arguments=@('-game','-nullrhi','-nosound','-unattended','-nop4','-CFHollowRecoveryTest',('-ExecCmds="t.MaxFPS '+$cap+'"'),('-abslog="'+$log+'"'))
        if($TraceRoaming){$arguments+='-CFRoamTrace'}
        if($Editor){$arguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'))+$arguments}
        $process=Start-Process -FilePath $exe -WorkingDirectory $root -WindowStyle Hidden -PassThru -ArgumentList $arguments
        $done=$process.WaitForExit(210000)
        if(!$done){$process.Kill();$process.WaitForExit()}
        $pass=$done -and $process.ExitCode -eq 0 -and (Test-Path -LiteralPath $log)
        if($pass){
            foreach($marker in @('CF_HOLLOW_RECOVERY_PREPARED PASS',
                'CF_HOLLOW_RECOVERY_AVOID PASS',
                'CF_HOLLOW_RECOVERY_DEATH_GATES south=1 near=1 enemyAlive=1 enemyHurt=1 petHurt=1 resident=1 noFocus=1 noWindup=1 noSlow=1',
                'CF_HOLLOW_RECOVERY_RESPAWN PASS','CF_HOLLOW_RECOVERY_REJOIN PASS','CF_HOLLOW_ADVENTURE_BATTLE PASS',
                'CF_HOLLOW_RECOVERY PASS',('t.MaxFPS = "'+$cap+'"'))){
                $pass=$pass -and (Select-String -LiteralPath $log -SimpleMatch $marker -Quiet)
            }
            if(Select-String -LiteralPath $log -Pattern '\bFAIL\b|Fatal error|Assertion failed' -Quiet){$pass=$false}
        }
        if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '恢复矩阵期间程序指纹变化'}
        $results += [pscustomobject]@{Case=$label;FrameCap=$cap;Attempt=$attempt;TraceRoaming=[bool]$TraceRoaming;Pass=$pass;BuildSHA256=$hash;Log=$log}
        $results | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
        Write-Host "$label 通过：$pass"
        if(!$pass){throw "恢复矩阵失败：$log"}
    }}
} finally {
    try {
        if($existed){
            Copy-Item -LiteralPath $backup -Destination $slot
            if((Get-FileHash -LiteralPath $slot).Hash -ne $originalHash){throw '恢复矩阵原测试槽还原校验失败'}
        } elseif(Test-Path -LiteralPath $slot){
            Copy-Item -LiteralPath $slot -Destination (Join-Path $run 'HollowRecoveryCross-generated.sav')
            Remove-Item -LiteralPath $slot
        }
    } finally {
        $mutex.ReleaseMutex();$mutex.Dispose()
    }
}
Write-Host '恢复帧率矩阵通过，原测试槽已还原；不代表真实键鼠输入或完整游戏验收'
