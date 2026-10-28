param([Parameter(Mandatory=$true)][string]$InputPath,[switch]$Editor,[string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8',[switch]$Dynamic,[switch]$WaitForPartner,[ValidateSet(30,60,120)][int]$FrameCap=60)
$ErrorActionPreference='Stop'
if($WaitForPartner -and !$Dynamic){throw '原地等待伙伴仅适用于动态观察模式'}
if(($Editor -and $BuildRoot) -or (!$Editor -and !$BuildRoot)){throw '请只指定Editor或BuildRoot之一'}
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$root=if($Editor){$projectRoot}else{(Resolve-Path -LiteralPath $BuildRoot).Path}
$exe=if($Editor){Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}else{Join-Path $root 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}
$module=if($Editor){Join-Path $root 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}else{$exe}
$buildHash=Get-FrontierBuildFingerprint -LiteralPath $module
$inputHash=(Get-FileHash -LiteralPath $InputPath).Hash
$run=Join-Path $projectRoot ('Saved\HollowFailureInspection\'+(Get-Date -Format yyyyMMdd-HHmmss)+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$saveRoot=if($Editor){Join-Path $root 'Saved\SaveGames'}else{Join-Path $root 'CreatureFrontier\Saved\SaveGames'}
New-Item -ItemType Directory -Path $saveRoot -Force|Out-Null
$slot=Join-Path $saveRoot 'HollowFailureInspectInput.sav'
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierFailureInspectionQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '失败现场检查槽正在使用'}
try {
    $existed=Test-Path -LiteralPath $slot
    $backup=Join-Path $run 'before.sav'
    $oldHash=if($existed){(Get-FileHash -LiteralPath $slot).Hash}else{''}
    if($existed){Copy-Item -LiteralPath $slot -Destination $backup}
    try {
        Copy-Item -LiteralPath $InputPath -Destination (Join-Path $run 'input.sav')
        Copy-Item -LiteralPath (Join-Path $run 'input.sav') -Destination $slot
        $log=Join-Path $run 'Inspection.log'
        $arguments=@('-game','-nullrhi','-nosound','-unattended','-nop4','-CFHollowRecoveryTest','-CFHollowInspectFailure',('-abslog="'+$log+'"'))
        if($Editor){$arguments=@((Join-Path $root 'CreatureFrontier.uproject'))+$arguments}
        if($Dynamic){$arguments+=@('-CFHollowInspectDynamic',('-ExecCmds="t.MaxFPS '+$FrameCap+'"'))}
        if($WaitForPartner){$arguments+='-CFHollowInspectWait'}
        $process=Start-Process -FilePath $exe -WorkingDirectory $root -WindowStyle Hidden -PassThru -ArgumentList $arguments
        if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw '现场检查超时'}
        $s=Get-Content -LiteralPath $log -Raw
        if($process.ExitCode -ne 0 -or $s -match '\bFAIL\b|Fatal error|Assertion failed' -or !$s.Contains('CF_HOLLOW_INSPECT PASS snapshotOnly=1 transientReplay=0')){throw "现场检查失败：$log"}
        foreach($m in @('CF_HOLLOW_INSPECT_ACTOR owned=1','CF_HOLLOW_INSPECT_ACTOR owned=0','label=hero-route6','label=hero-enemy','label=pet-enemy','CF_HOLLOW_INSPECT_SWEEP','CF_HOLLOW_INSPECT_SIGHT')){if(!$s.Contains($m)){throw "缺少现场诊断标记：$m"}}
        if($Dynamic -and (!$s.Contains('CF_HOLLOW_DYNAMIC_BEGIN reconstructedFocus=1 deterministicReplay=0') -or !$s.Contains('CF_HOLLOW_DYNAMIC OBSERVED'))){throw '动态观察未完整结束'}
        if($Dynamic -and (!$s.Contains('waitForPartner='+[int][bool]$WaitForPartner) -or !$s.Contains('t.MaxFPS = "'+$FrameCap+'"'))){throw '动态观察模式或帧率不符'}
        foreach($path in @($InputPath,$slot,(Join-Path $run 'input.sav'))){if((Get-FileHash -LiteralPath $path).Hash -ne $inputHash){throw '现场检查改写了输入'}}
        if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $buildHash){throw '检查期间程序指纹变化'}
        [pscustomobject]@{Pass=$true;SnapshotOnly=!$Dynamic;DynamicObservation=[bool]$Dynamic;WaitForPartner=[bool]$WaitForPartner;FrameCap=$FrameCap;BuildSHA256=$buildHash;InputSHA256=$inputHash;Log=$log}|Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
        Write-Host "失败现场检查完成：$run；动态观察=$Dynamic；不等同恢复路线或瞬态重放通过"
    } finally {
        if($existed){Copy-Item -LiteralPath $backup -Destination $slot;if((Get-FileHash -LiteralPath $slot).Hash -ne $oldHash){throw '现场检查原槽恢复失败'}}
        elseif(Test-Path -LiteralPath $slot){Remove-Item -LiteralPath $slot}
    }
} finally {$mutex.ReleaseMutex();$mutex.Dispose()}
