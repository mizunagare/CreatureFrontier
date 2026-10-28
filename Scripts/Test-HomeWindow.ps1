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
$run=Join-Path $projectRoot ('Saved\WindowPersistenceQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "窗墙跨进程证据：$run"
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierWindowQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '窗墙测试槽正在使用'}
$backups=@();$results=@()
try {
    foreach($name in @('WindowGood','WindowLegacy31')){
        $path=Join-Path $saveRoot ($name+'.sav');$exists=Test-Path -LiteralPath $path
        $backup=Join-Path $run ($name+'-before.sav')
        $before=if($exists){(Get-FileHash -LiteralPath $path).Hash}else{''}
        if($exists){Copy-Item -LiteralPath $path -Destination $backup}
        $backups+=@{Path=$path;Existed=$exists;Backup=$backup;Hash=$before;Name=$name}
    }
    $inputHash=''
    foreach($case in @(@{Name='Write';Flag='CFWindowTest';Marker='CF_WINDOW PASS built=1 hole=1 frame=1 pawn=1 support=1 legacy=1 save=1'},
                       @{Name='Read1';Flag='CFWindowRead';Marker='CF_WINDOW_READ PASS windows=2 loads=2'},
                       @{Name='Read2';Flag='CFWindowRead';Marker='CF_WINDOW_READ PASS windows=2 loads=2'})){
        $log=Join-Path $run ($case.Name+'.log')
        $arguments=@('-game','-nullrhi','-nosound','-unattended','-nop4','-CFHomeTest',('-'+$case.Flag),('-abslog="'+$log+'"'))
        if($Editor){$arguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'))+$arguments}
        $process=Start-Process -FilePath $exe -WorkingDirectory $root -WindowStyle Hidden -PassThru -ArgumentList $arguments
        if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw '窗墙测试超时'}
        if($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch $case.Marker -Quiet) -or
           (Select-String -LiteralPath $log -Pattern '\bFAIL\b|Fatal error|Assertion failed' -Quiet)){throw "窗墙测试失败：$log"}
        if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '窗墙测试期间程序指纹变化'}
        $current=(Get-FileHash -LiteralPath (Join-Path $saveRoot 'WindowGood.sav')).Hash
        if($case.Name -eq 'Write'){$inputHash=$current}elseif($current -ne $inputHash){throw '窗墙读取改写了输入文件'}
        $results += [pscustomobject]@{Case=$case.Name;Pass=$true;BuildSHA256=$hash;InputSHA256=$inputHash;Log=$log}
        $results | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
        Write-Host "$($case.Name) 通过"
    }
} finally {
    try {
        foreach($entry in $backups){
            if(Test-Path -LiteralPath $entry.Path){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ($entry.Name+'-generated.sav'))}
            if($entry.Existed){
                Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path
                if((Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.Hash){throw '原窗墙测试槽恢复校验失败'}
            } elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}
        }
    } finally {$mutex.ReleaseMutex();$mutex.Dispose()}
}
Write-Host '窗墙一写两读通过，输入未改写，原专用槽已还原；图形与导航仍需另验'
