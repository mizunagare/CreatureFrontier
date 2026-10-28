param([switch]$Editor,[string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8')
$ErrorActionPreference='Stop'
if(($Editor -and $BuildRoot) -or (!$Editor -and !$BuildRoot)){throw '请只指定Editor或BuildRoot之一'}
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$root=if($Editor){$projectRoot}else{(Resolve-Path -LiteralPath $BuildRoot).Path}
$exe=if($Editor){Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}else{Join-Path $root 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}
$module=if($Editor){Join-Path $root 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}else{$exe}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$saveRoot=if($Editor){Join-Path $root 'Saved\SaveGames'}else{Join-Path $root 'CreatureFrontier\Saved\SaveGames'}
$run=Join-Path $projectRoot ('Saved\StancePersistenceQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "伙伴姿态跨进程证据：$run"
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierStanceQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '姿态测试槽正在使用'}
$backups=@();$results=@()
try {
    foreach($name in @('StanceGood','StanceBad','StanceLegacy30')){
        $slot=Join-Path $saveRoot ($name+'.sav');$backup=Join-Path $run ($name+'-before.sav');$exists=Test-Path -LiteralPath $slot
        $before=if($exists){(Get-FileHash -LiteralPath $slot).Hash}else{''}
        if($exists){Copy-Item -LiteralPath $slot -Destination $backup}
        $backups+=@{Slot=$slot;Backup=$backup;Existed=$exists;Hash=$before;Name=$name}
    }
    $inputHash=''
    foreach($case in @(@{Flag='CFStanceTest';Name='Write';Marker='CF_STANCE PASS'},@{Flag='CFStanceRead';Name='Read1';Marker='CF_STANCE_READ PASS avoid=2 guard=2'},@{Flag='CFStanceRead';Name='Read2';Marker='CF_STANCE_READ PASS avoid=2 guard=2'})){
        $log=Join-Path $run ($case.Name+'.log')
        $arguments=@('-game','-nullrhi','-nosound','-unattended','-nop4','-CFDefenseTest',('-'+$case.Flag),('-abslog="'+$log+'"'))
        if($Editor){$arguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'))+$arguments}
        $process=Start-Process -FilePath $exe -WorkingDirectory $root -WindowStyle Hidden -PassThru -ArgumentList $arguments
        if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw '姿态专项超时'}
        if($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch $case.Marker -Quiet) -or
            (Select-String -LiteralPath $log -Pattern '\bFAIL\b|Fatal error|Assertion failed' -Quiet)){throw "姿态专项失败：$log"}
        if($case.Name -eq 'Write' -and !(Select-String -LiteralPath $log -SimpleMatch 'CF_STANCE_BOUNDARIES PASS scope=1 guard=1 bounds=1 work=1' -Quiet)){throw '姿态写入缺少边界标记'}
        if($case.Name -eq 'Write' -and !(Select-String -LiteralPath $log -SimpleMatch 'CF_STANCE_SKILL PASS explicit=1 cancel=1' -Quiet)){throw '姿态写入缺少技能执行与取消标记'}
        $input=Join-Path $saveRoot 'StanceGood.sav';$current=(Get-FileHash -LiteralPath $input).Hash
        if(!$inputHash){$inputHash=$current;Copy-Item -LiteralPath $input -Destination (Join-Path $run 'Input.sav')}
        if($current -ne $inputHash){throw '姿态读取改写了输入'}
        if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '姿态专项程序指纹变化'}
        $results += [pscustomobject]@{Case=$case.Name;Pass=$true;InputSHA256=$inputHash;BuildSHA256=$hash;Log=$log}
        $results | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
        Write-Host "$($case.Name) 通过"
    }
} finally {
    try {
        foreach($entry in $backups){
            if(Test-Path -LiteralPath $entry.Slot){Copy-Item -LiteralPath $entry.Slot -Destination (Join-Path $run ($entry.Name+'-generated.sav'))}
            if($entry.Existed){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Slot;if((Get-FileHash -LiteralPath $entry.Slot).Hash -ne $entry.Hash){throw '姿态QA槽还原失败'}}
            elseif(Test-Path -LiteralPath $entry.Slot){Remove-Item -LiteralPath $entry.Slot}
        }
    } finally {$mutex.ReleaseMutex();$mutex.Dispose()}
}
Write-Host '姿态一写两读通过，输入未改写，原专用槽已还原；图形与实际移动另验'
