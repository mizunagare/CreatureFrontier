param([Parameter(Mandatory=$true)][string]$ReadInputPath,[switch]$Inspect,[switch]$TraceReturn)
$ErrorActionPreference='Stop'
if($TraceReturn){$Inspect=$true}
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$module=Join-Path $root 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $root ('Saved/IndoorFeedingQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$slot=Join-Path $root 'Saved/SaveGames/IndoorFeedingInput.sav'
if([IO.Path]::GetFullPath($slot) -eq $ReadInputPath){throw '来源必须使用独立副本'}
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierIndoorFeedingQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '室内进食验证正在运行'}
$before=''
try{
    if(Test-Path $slot){$before=Get-FrontierBuildFingerprint -LiteralPath $slot;Copy-Item -LiteralPath $slot -Destination (Join-Path $run 'Input-before.sav')}
    $inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
    Copy-Item -LiteralPath $ReadInputPath -Destination $slot
    $log=Join-Path $run 'Feeding.log'
    $scope=if($Inspect){'来源状态检查／可选5秒原行为观察，不发放物资或修改角色'}else{'隔离室内食槽寻路夹具，非真实饥饿或资源来源'}
    [pscustomobject]@{BuildSHA256=$hash;SourceSHA256=$inputHash;Source=$ReadInputPath;Scope=$scope}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    $arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game','-CFIndoorFeedingTest','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Inspect){$arguments+='-CFPantryInspect'}
    if($TraceReturn){$arguments+=@('-CFPantryReturnInspect','-CFFarmApproachTrace')}
    Write-Host ('室内进食诊断：'+$run)
    $p=Start-Process -FilePath 'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(90000)){$p.Kill();$p.WaitForExit();throw '室内进食诊断超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    $marker=if($Inspect){'CF_PANTRY_INSPECT PASS noSave=1 noGrant=1'}else{'CF_INDOOR_FEED PASS meal=1 task=1 live=1'}
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains($marker) -or !$raw.Contains('LogExit: Exiting.')){throw ('室内进食验证失败：'+$log)}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash){throw '程序或来源发生变化'}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    if($Inspect){Write-Host '来源物资与伙伴状态检查完成，未保存或发放物资'}else{Write-Host '室内食槽实际寻路、一次取食与原任务返回通过；不证明真实资源链或图形'}
}finally{
    try{if($before){Copy-Item -LiteralPath (Join-Path $run 'Input-before.sav') -Destination $slot;if((Get-FrontierBuildFingerprint -LiteralPath $slot) -ne $before){throw '原测试槽恢复失败'}}elseif(Test-Path $slot){Remove-Item -LiteralPath $slot}}finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
