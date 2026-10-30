param([string]$BuildRoot='',[Parameter(Mandatory=$true)][string]$LegacyInputPath)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$LegacyInputPath=(Resolve-Path -LiteralPath $LegacyInputPath).Path
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $root 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved'}else{Join-Path $root 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module;$inputHash=Get-FrontierBuildFingerprint -LiteralPath $LegacyInputPath
$run=Join-Path $root ('Saved\MeleeCooldownQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$slots=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slots -Force|Out-Null
$protected=@();$started=$null
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierMeleeCooldownQA');if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '普攻冷却专项已在运行'}
try{
    foreach($name in @('CooldownInput','CooldownRoundTrip','CooldownBoundary','CooldownInvalid')){
        $path=Join-Path $slots ($name+'.sav');if($path -eq $LegacyInputPath){throw '输入必须是独立副本'}
        $backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    Copy-Item -LiteralPath $LegacyInputPath -Destination (Join-Path $slots 'CooldownInput.sav')
    $log=Join-Path $run 'Cooldown.log';$exe=if($BuildRoot){$module}else{'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}
    $arguments=@('-CFMeleeCooldownTest','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    [pscustomobject]@{BuildRoot=$BuildRoot;BuildSHA256=$hash;LegacyInput=$LegacyInputPath;InputSHA256=$inputHash}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    $started=[DateTime]::UtcNow;Write-Host ('普攻冷却证据：'+$run)
    $p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(60000)){$p.Kill();$p.WaitForExit();throw '普攻冷却专项超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('LogExit: Exiting.')){throw ('普攻冷却专项失败：'+$log)}
    foreach($marker in @('CF_MELEE_COOLDOWN_LEGACY PASS','CF_MELEE_COOLDOWN_BOUNDARY PASS zero=1 maximum=1 positive=1 roundtrip=1','CF_MELEE_COOLDOWN_REJECT PASS cases=6 worldRetained=1','CF_MELEE_COOLDOWN PASS reason=complete')){if(!$raw.Contains($marker)){throw ('缺少冷却验收标记：'+$marker)}}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $LegacyInputPath) -ne $inputHash){throw '程序或旧输入发生变化'}
    Write-Host '旧档迁移、零值／上限／正值存读和六类非法记录事务拒绝通过'
}finally{
    try{foreach($entry in $protected){if($started -and (Test-Path $entry.Path) -and (Get-Item $entry.Path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ((Split-Path $entry.Path -LeafBase)+'-generated.sav'))};if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '普攻冷却测试槽恢复失败'}}elseif(Test-Path $entry.Path){Remove-Item -LiteralPath $entry.Path}}}finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
