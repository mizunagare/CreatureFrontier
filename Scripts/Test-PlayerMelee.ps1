param([string]$BuildRoot='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $root 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved'}else{Join-Path $root 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $root ('Saved\PlayerMeleeQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$slot=Join-Path $saved 'SaveGames\FrontierMeleeAutomation.sav'
$backup=Join-Path $run 'FrontierMeleeAutomation-before.sav'
$before=''
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierPlayerMeleeQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '玩家近战专项已在运行'}
try{
    # 原夹具只写此槽，保护原数据；不把固定站位单元测试当作自然接敌证据。
    if(Test-Path -LiteralPath $slot){$before=Get-FrontierBuildFingerprint -LiteralPath $slot;Copy-Item -LiteralPath $slot -Destination $backup}
    $log=Join-Path $run 'Melee.log'
    $arguments=@('-CFMeleeTest','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    $exe=if($BuildRoot){$module}else{'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    [pscustomobject]@{BuildRoot=$BuildRoot;BuildSHA256=$hash;Log=$log}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    Write-Host ('玩家近战回归：'+$run)
    $p=Start-Process -FilePath $exe -WorkingDirectory $(if($BuildRoot){$BuildRoot}else{$root}) -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(60000)){$p.Kill();$p.WaitForExit();throw '玩家近战专项超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($p.ExitCode -ne 0 -or !$raw.Contains('CF_MELEE PASS') -or !$raw.Contains('LogExit: Exiting.') -or $raw -match '\bFAIL\b|Fatal error|Assertion failed'){throw ('玩家近战回归失败：'+$log)}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '测试期间程序指纹变化'}
    Write-Host '原近战时序、射程、遮挡、伙伴保护、转向、取消、存读与徒手规则回归通过'
}finally{
    try{
        if(Test-Path -LiteralPath $slot){Copy-Item -LiteralPath $slot -Destination (Join-Path $run 'FrontierMeleeAutomation-generated.sav')}
        if($before){Copy-Item -LiteralPath $backup -Destination $slot;if((Get-FrontierBuildFingerprint -LiteralPath $slot) -ne $before){throw '近战原测试槽恢复失败'}}elseif(Test-Path -LiteralPath $slot){Remove-Item -LiteralPath $slot}
    }finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
