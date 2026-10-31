param([Parameter(Mandatory=$true)][string]$ReadInputPath,[string]$BuildRoot='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $root 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $root 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module;$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$run=Join-Path $root ('Saved/WorkRestRulesQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8));New-Item -ItemType Directory -Path $run|Out-Null
$slots=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slots -Force|Out-Null
$slot=Join-Path $slots 'WorkRestDetourInput.sav';if($slot -eq $ReadInputPath){throw '来源必须使用独立副本'}
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierWorkRestRulesQA');if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '工人绕行边界验证正在运行'}
$before=''
try{
    if(Test-Path $slot){$before=Get-FrontierBuildFingerprint -LiteralPath $slot;Copy-Item -LiteralPath $slot -Destination (Join-Path $run 'Input-before.sav')}
    Copy-Item -LiteralPath $ReadInputPath -Destination $slot
    $log=Join-Path $run 'Rules.log'
    $arguments=@('-CFWorkRestDetourRules','-CFFarmApproachTrace','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    $exe=if($BuildRoot){$module}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    [pscustomobject]@{BuildSHA256=$hash;Input=$ReadInputPath;InputSHA256=$inputHash;Scope='无路障碍／指令覆盖／拆除目标的隔离边界夹具，不代表真实生产'}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    Write-Host ('工人绕行边界证据：'+$run)
    $p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(90000)){$p.Kill();$p.WaitForExit();throw '工人绕行边界验证超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('CF_WORK_REST_RULES PASS reason=complete blocked=1 cancelled=1 removed=1') -or ([regex]::Matches($raw,'CF_WORK_REST_DETOUR actor=')).Count -lt 2 -or !$raw.Contains('LogExit: Exiting.')){throw ('工人绕行边界失败：'+$log)}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash){throw '程序或来源发生变化'}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '无路保留、绕行中跟随覆盖及拆除退款／取消导航通过；不是完整人工体验验收'
}finally{try{if($before){Copy-Item -LiteralPath (Join-Path $run 'Input-before.sav') -Destination $slot;if((Get-FrontierBuildFingerprint -LiteralPath $slot) -ne $before){throw '原测试槽恢复失败'}}elseif(Test-Path $slot){Remove-Item -LiteralPath $slot}}finally{$mutex.ReleaseMutex();$mutex.Dispose()}}
