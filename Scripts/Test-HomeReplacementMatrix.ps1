param([switch]$Editor,[string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8')
$ErrorActionPreference='Stop'
if(($Editor -and $BuildRoot) -or (!$Editor -and !$BuildRoot)){throw '请只指定Editor或BuildRoot之一'}
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$root=if($Editor){$projectRoot}else{(Resolve-Path -LiteralPath $BuildRoot).Path}
$exe=if($Editor){Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}else{Join-Path $root 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}
$module=if($Editor){Join-Path $root 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}else{$exe}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved\HomeReplacementMatrix\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "原位替换本地矩阵证据：$run"
$cases=@()
foreach($yaw in @(0,90)){
    $cases+=@{Name="Structure-$yaw";Yaw=$yaw;Cap=30;Mode='Structure'}
    foreach($cap in @(30,60,120)){$cases+=@{Name="Cancel-$yaw-$cap";Yaw=$yaw;Cap=$cap;Mode='Cancel'}}
}
$results=@()
foreach($case in $cases){
    $log=Join-Path $run ($case.Name+'.log')
    # 此矩阵不传写档参数；冷启动及真实旧档由其他专项保护测试槽。
    $arguments=@('-game','-nullrhi','-nosound','-unattended','-nop4','-CFWindowTest','-CFHomeReplaceTest','-CFHomeReplaceInput',('-ExecCmds="t.MaxFPS '+$case.Cap+'"'),('-abslog="'+$log+'"'))
    if($case.Yaw -eq 90){$arguments+='-CFHomeReplaceRotated'}
    if($case.Mode -eq 'Cancel'){$arguments+='-CFHomeReplaceCancel';$markers=@('CF_HOME_REPLACE_CANCEL PASS phase=2')}
    else{$arguments+=@('-CFHomeReplaceDoor','-CFHomeReplaceCapacity');$markers=@('CF_HOME_REPLACE_DOOR PASS transitions=4','CF_HOME_REPLACE_CAPACITY PASS paused=1 downed=1 far=1 overflow=1 net=1',('CF_HOME_REPLACE PASS support=1 yaw='+$case.Yaw))}
    if($Editor){$arguments=@((Join-Path $root 'CreatureFrontier.uproject'))+$arguments}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '矩阵启动前程序指纹变化'}
    $process=Start-Process -FilePath $exe -WorkingDirectory $root -WindowStyle Hidden -PassThru -ArgumentList $arguments
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw "矩阵超时：$($case.Name)"}
    $s=Get-Content -LiteralPath $log -Raw
    $pass=$process.ExitCode -eq 0 -and $s -notmatch '\bFAIL\b|Fatal error|Assertion failed'
    foreach($marker in $markers){$pass=$pass -and $s.Contains($marker)}
    $pass=$pass -and $s.Contains('t.MaxFPS = "'+$case.Cap+'"')
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '矩阵期间程序指纹变化'}
    $results+=[pscustomobject]@{Case=$case.Name;Yaw=$case.Yaw;Cap=$case.Cap;Mode=$case.Mode;Pass=$pass;BuildSHA256=$hash;Log=$log}
    $results|Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    if(!$pass){throw "矩阵失败：$log"}
    Write-Host "$($case.Name) 通过"
}
Write-Host '原位替换八进程矩阵通过；不代表冷启动、图形或自然通行全部完成'
