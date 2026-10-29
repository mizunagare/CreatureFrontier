param([string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8',[ValidateSet('Resource','Cargo','CargoCrystal','CargoOre')][string]$Suite='Resource')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved'}else{Join-Path $projectRoot 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved\ResourceRegressionQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$log=Join-Path $run 'Resources.log';$protected=@()
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierResourceRegressionQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '资源关联回归正在运行'}
try{
    $slots=if($Suite -eq 'Resource'){@('FrontierResourceAutomation','FrontierResourceInvalid','FrontierResourceLegacy')}else{@('CargoBad','CargoCancelled','CargoCross','CargoCrystalMid','CargoDowned','CargoLegacy21','CargoMid','CargoMigrated22','CargoOreMid','CargoOrphan','CargoRest','CargoSourceGone')}
    foreach($slot in $slots){
        $path=Join-Path $saved ('SaveGames\'+$slot+'.sav');$backup=Join-Path $run ($slot+'-before.sav');$before=if(Test-Path -LiteralPath $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    $flag=switch($Suite){'Resource'{'CFResourceTest'} 'Cargo'{'CFCargoTest'} 'CargoCrystal'{'CFCargoCrystalTest'} 'CargoOre'{'CFCargoOreTest'}}
    $marker=switch($Suite){'Resource'{'CF_RESOURCE PASS navigation=1'} 'Cargo'{'CF_CARGO PASS'} 'CargoCrystal'{'CF_CARGO_CRYSTAL PASS'} 'CargoOre'{'CF_CARGO_ORE PASS'}}
    $arguments=@(('-'+$flag),'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    $exe=if($BuildRoot){$module}else{Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    Write-Host ('资源关联回归：'+$run)
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(90000)){$process.Kill();$process.WaitForExit();throw '资源关联回归超时'}
    $body=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $body -match '\bFAIL\b|Fatal error|Assertion failed' -or !$body.Contains($marker) -or !$body.Contains('LogExit: Exiting.')){throw ('资源关联回归失败：'+$log)}
    if($Suite -eq 'Cargo'){foreach($stage in @('cancel_recover','rest_resume','downed_recover','crate_removed','source_removed','owned_camp_retention','legacy21')){if(!$body.Contains('CF_CARGO_RECOVERY '+$stage+'=1')){throw ('货运恢复标记缺失：'+$stage)}}}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '资源回归期间程序改变'}
    [pscustomobject]@{Suite=$Suite;Pass=$true;BuildSHA256=$hash;Log=$log}|Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host $(if($Suite -eq 'Resource'){'原资源共享、事务、快照、非法数据拒绝、再生、迁移及实际寻路关联回归通过'}else{'原货运实际生产／满箱保留／交付／物资守恒关联回归通过；基础货运还核对取消、倒下、休息、目标失效及迁移'})
}finally{
    try{foreach($entry in $protected){if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '资源原测试槽恢复失败'}}elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}}}
    finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
