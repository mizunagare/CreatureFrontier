param([Parameter(Mandatory=$true)][string]$ReadInputPath,[string]$BuildRoot='',[switch]$Plan,[switch]$Repair,[switch]$Funded,[switch]$Read)
$ErrorActionPreference='Stop'
if(@($Plan,$Repair,$Funded,$Read | Where-Object {$_}).Count -gt 1){throw '只读布局、资源修复、材料到位续测及冷启动不能混用'}
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved'}else{Join-Path $projectRoot 'Saved'}
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved\HomeExpansionQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$log=Join-Path $run 'Expansion.log';$protected=@()
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierHomeExpansionQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '原家园扩建专项正在运行'}
try{
    $slotRoot=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slotRoot -Force | Out-Null
    foreach($name in @('HomeExpansionInput','HomeExpansionPartial','HomeExpansionFunded','HomeExpansionDone')){
        $path=Join-Path $slotRoot ($name+'.sav');if($path -eq $ReadInputPath){throw '输入必须是独立副本，不能使用受保护槽本身'}
        $backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path -LiteralPath $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slotRoot 'HomeExpansionInput.sav')
    $flag=if($Plan){'-CFHomeExpansionPlan'}elseif($Repair){'-CFHomeExpansionRepair'}elseif($Funded){'-CFHomeExpansionFunded'}elseif($Read){'-CFHomeExpansionRead'}else{'-CFHomeExpansionTest'}
    $arguments=@($flag,'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    $exe=if($BuildRoot){$module}else{'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    Write-Host ('原家园扩建专项：'+$run)
    $started=[DateTime]::UtcNow
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(600000)){$process.Kill();$process.WaitForExit();throw '原家园扩建专项超时'}
    $body=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $body -match '\bFAIL\b|Fatal error|Assertion failed' -or !$body.Contains('LogExit: Exiting.')){throw ('扩建专项失败：'+$log)}
    $restoredVersion=0
    if($Read){$versionMatch=[regex]::Match($body,'CF_HOME_EXPAND_RESTORE PASS version=(34|35|36|37|38) ');if(!$versionMatch.Success){throw '缺少受支持的实际扩建恢复版本'};$restoredVersion=$versionMatch.Groups[1].Value}
    $markers=if($Read){@("CF_HOME_EXPAND_RESTORE PASS version=$restoredVersion modules=19 farms=2 material=2 food=1 seeds=1",'CF_HOME_EXPAND_COLD_UP PASS hero=1 pet=1 food=1 seeds=1 material=2','CF_HOME_EXPAND_SAVE PASS version=38 modules=19 farms=2 material=2 food=1 seeds=1 bond=6')}else{@('CF_HOME_EXPAND_DOOR_CONFLICT PASS rejected=1',$(if($Funded){'CF_HOME_EXPAND_PLAN_FUNDED PASS modules=19 cost=86 refund=14 material=74 stored=0 original=14'}else{'CF_HOME_EXPAND_PLAN PASS modules=19 cost=86 refund=14 stored=3 required=69 original=14'}))}
    $markers+=$(if($Plan){'CF_HOME_EXPAND_PLAN_ONLY PASS readonly=1'}elseif($Repair){'CF_RESOURCE_REPAIR PASS fresh=1 position=1 progress=1 timer=1'}else{'CF_HOME_EXPAND PASS reason=complete'})
    if(!$Plan -and !$Funded -and !$Read){$markers+=@('CF_RESOURCE_POSITION_PROGRESS PASS fresh=1 remaining=13 cooldown=0.4 inventory=1','CF_RESOURCE_TIMER_PRECISION PASS steps=1400000 delta=0.0001 elapsed=140 remaining=3.04 regrew=1')}
    if($Funded){$markers+='CF_HOME_EXPAND_FUNDED_RESTORE PASS modules=14 material=74 stored=0 seeds=2'}
    if(!$Plan -and !$Repair -and !$Read){$markers+=@('CF_HOME_EXPAND_FUNDED PASS modules=14 material=74 stored=0 seeds=2 carried=3','CF_HOME_EXPAND_BUILD PASS modules=19 kept=12 material=2 farms=2 fixedObserver=1','CF_HOME_EXPAND_UPPER PASS hero=1 pet=1 food=1 seeds=1 material=2','CF_HOME_EXPAND_SAVE PASS version=38 modules=19 farms=2 material=2 food=1 seeds=1 bond=6');if(!$Funded){$markers+=@('CF_HOME_EXPAND_HARVEST PASS id=6 taken=11 remaining=0','CF_HOME_EXPAND_HARVEST PASS id=5 taken=30 remaining=0','CF_HOME_EXPAND_HARVEST PASS id=8 taken=4 remaining=1','CF_HOME_EXPAND_HARVEST PASS id=1 taken=21 remaining=3','CF_HOME_EXPAND_REGROW PASS','CF_HOME_EXPAND_CARGO PASS source=0 cargo=0 stored=6 material=68')}}
    foreach($marker in $markers){if(!$body.Contains($marker)){throw ('缺少扩建标记：'+$marker)}}
    if((Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash -or (Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '输入源副本或测试程序发生变化'}
    $fundedHash='';$doneHash=''
    if(!$Plan -and !$Repair){$doneHash=Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'HomeExpansionDone.sav');if(!$Read){$fundedHash=Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'HomeExpansionFunded.sav')}}
    [pscustomobject]@{PlanOnly=[bool]$Plan;RepairOnly=[bool]$Repair;FundedRestore=[bool]$Funded;ColdRead=[bool]$Read;Pass=$true;BuildSHA256=$hash;ReadInputSHA256=$inputHash;FundedSaveSHA256=$fundedHash;DoneSaveSHA256=$doneHash;Log=$log}|Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host $(if($Plan){'只读布局及成本检查通过；未施工，不作为两层家园完成验收'}elseif($Repair){'资源点旧档纠正与独立精度专项通过；不作为两层扩建完成验收'}else{'原家园扩建专项通过'})
}finally{
    try{
        # 失败时也保留本轮真实生成的中间档，以便诊断，不重复采集或改写原输入。
        if($started){foreach($name in @('HomeExpansionFunded','HomeExpansionDone')){$path=Join-Path $slotRoot ($name+'.sav');if((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'-generated.sav'))}}}
        foreach($entry in $protected){if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '扩建原测试槽恢复失败'}}elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}}
    }
    finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
