param([switch]$Editor,[string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if($Editor){
    $exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    $fingerprint=Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'
    $saveRoot=Join-Path $projectRoot 'Saved\SaveGames'
}else{
    if(!$BuildRoot){throw '请指定Editor或BuildRoot'}
    $exe=Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
    $fingerprint=$exe
    $saveRoot=Join-Path $BuildRoot 'CreatureFrontier\Saved\SaveGames'
}
if(!(Test-Path -LiteralPath $exe)){throw '找不到测试程序'}
$hash=(Get-FileHash -LiteralPath $fingerprint).Hash
$evidence=Join-Path $projectRoot ('Saved\RescueMemoryQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $evidence | Out-Null
Write-Host "脱险经历证据：$evidence"
$slots=@('RescueMemoryDowned','RescueMemoryRescued','RescueMemoryHome','RescueMemoryBad','RescueMemoryLegacy28')
$backups=@()
$results=@()
try{
    foreach($slot in $slots){
        $path=Join-Path $saveRoot ($slot+'.sav')
        if(Test-Path -LiteralPath $path){
            $backup=Join-Path $evidence ($slot+'-before.sav')
            Copy-Item -LiteralPath $path -Destination $backup
            $backups+=@{Path=$path;Backup=$backup;Hash=(Get-FileHash -LiteralPath $path).Hash}
        }
    }
    $flags=@('CFRescueMemoryTest','CFRescueMemoryWrite','CFRescueMemoryRescueRead','CFRescueMemoryHomeRead','CFRescueMemoryFinalRead')
    for($index=0;$index -lt $flags.Count;$index++){
        $flag=$flags[$index];$log=Join-Path $evidence ($flag+'.log')
        $testArgs=@('-game',('-'+$flag),'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
        if($Editor){$testArgs=@((Join-Path $projectRoot 'CreatureFrontier.uproject'))+$testArgs}
        $process=Start-Process -FilePath $exe -ArgumentList $testArgs -WindowStyle Hidden -PassThru
        if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw "测试超时：$log"}
        $marker='CF_RESCUE_MEMORY PASS mode='+$index
        if($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch $marker -Quiet) -or (Select-String -LiteralPath $log -Pattern 'CF_\w+ FAIL|Fatal error|Assertion failed' -Quiet)){throw "测试未通过：$log"}
        if((Get-FileHash -LiteralPath $fingerprint).Hash -ne $hash){throw '测试期间模块发生变化'}
        $results += [pscustomobject]@{Case=$flag;Pass=$true;BuildSHA256=$hash;Log=$log}
        $results | Export-Csv -LiteralPath (Join-Path $evidence 'Results.csv') -NoTypeInformation
        Write-Host "$flag 已通过"
    }
    foreach($slot in $slots){$path=Join-Path $saveRoot ($slot+'.sav');if(Test-Path -LiteralPath $path){Copy-Item -LiteralPath $path -Destination (Join-Path $evidence ($slot+'.sav'))}}
}finally{
    foreach($entry in $backups){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.Hash){throw '原测试槽恢复校验失败'}}
}
