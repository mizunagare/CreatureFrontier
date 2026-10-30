param([string]$ReadInputPath='',[string]$BuildRoot='',
      [switch]$Read,[switch]$Preview,[switch]$Occupancy,[switch]$BlockedRead,[switch]$Trace,[ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
if(($Occupancy -and ($Read -or $Preview -or $ReadInputPath -or $BlockedRead)) -or (!$Occupancy -and !$ReadInputPath) -or ($BlockedRead -and $Preview)){throw '占位单元不可混用读档或预览；原家园模式必须指定独立输入副本，阻塞退出复现暂不预览'}
if($BlockedRead){$Read=$true}
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$inputHash='';if($ReadInputPath){$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path;$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath}
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved'}else{Join-Path $projectRoot 'Saved'}
$run=Join-Path $projectRoot ('Saved\RenovationQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$log=Join-Path $run 'Renovation.log';$protected=@()
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierRenovationQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '原位改建专项正在运行'}
try{
    $slotRoot=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slotRoot -Force | Out-Null
    foreach($name in @('RenovationInput','RenovationMid','RenovationDone','RenovationBad','RenovationCancel','RenovationBlocked')){
        $path=Join-Path $slotRoot ($name+'.sav');if($path -eq $ReadInputPath){throw '输入必须为独立副本，不能直接使用受保护测试槽'}
        $before=if(Test-Path -LiteralPath $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''};$backup=Join-Path $run ($name+'-before.sav')
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Hash=$before;Backup=$backup}
    }
    if($ReadInputPath){Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slotRoot 'RenovationInput.sav')}
    $flag=if($Occupancy){'-CFRenovationOccupancy'}elseif($BlockedRead){'-CFRenovationBlockedRead'}elseif($Read){'-CFRenovationRead'}else{'-CFRenovationTest'}
    $arguments=@($flag,'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Trace){$arguments+='-CFRenovationTrace'}
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFRenovationPreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'}else{'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    $started=[DateTime]::UtcNow;Write-Host ('原家园原位改建专项：'+$run)
    [pscustomobject]@{Occupancy=[bool]$Occupancy;BlockedRead=[bool]$BlockedRead;Trace=[bool]$Trace;Read=[bool]$Read;Preview=[bool]$Preview;Resolution=$Resolution;BuildSHA256=$hash;ReadInputSHA256=$inputHash;Module=$module;StartedUTC=$started.ToString('o')}|Export-Csv -LiteralPath (Join-Path $run 'Run.csv') -NoTypeInformation
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(180000)){$process.Kill();$process.WaitForExit();throw '原位改建专项超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('LogExit: Exiting.')){throw ('原位改建专项失败：'+$log)}
    $restoredVersion=0
    if($Read){$prefix=if($BlockedRead){'CF_RENOVATION_BLOCK_RESTORE'}else{'CF_RENOVATION_RESTORE'};$version=[regex]::Match($raw,($prefix+' PASS version=(36|37) '));if(!$version.Success){throw '缺少受支持的实际改建恢复版本'};$restoredVersion=$version.Groups[1].Value}
    $markers=if($Occupancy){@('CF_RENOVATION_REFUND_CAPACITY PASS stock=6 oldCost=8 bag=64 retained=1','CF_OWNER_YIELD_CLEARANCE PASS stepDown=1 highDrop=1 box=1 penetrating=1','CF_RENOVATION_OCCUPANCY_DONE PASS','CF_RENOVATION_OCCUPANCY PASS reason=complete')}else{@('CF_RENOVATION_COMPLETED PASS version=37 modules=19 material=5 cost=8 refund=5 sameId=1 support=1','CF_RENOVATION_LIFE PASS entered=1 petEntered=1 exited=1 petExited=1 food=0 seed=1 bond=9 material=5 retained=18','CF_RENOVATION PASS reason=complete')}
    if($BlockedRead){$markers=@("CF_RENOVATION_BLOCK_RESTORE PASS version=$restoredVersion phase=exitOnly",'CF_RENOVATION_EXIT_REPRO PASS moved=1 exited=1 petExited=1 material=5 food=0 seed=1 bond=9','CF_RENOVATION PASS reason=complete')}
    elseif(!$Occupancy){$markers+=if($Read){"CF_RENOVATION_RESTORE PASS version=$restoredVersion stock=5 carried=3 retained=1"}else{@('CF_RENOVATION_HARVEST count=6 material=8','CF_RENOVATION_FAILURES PASS cases=7 cancel=1 fullBagRetained=1 oldCost=10 inventory=8 support=1','CF_RENOVATION_QUEUED PASS modules=19 paid=10 supplied=0 stock=8 material=0 support=1 mapped=1','CF_RENOVATION_MID PASS version=37 material=0 stock=5 carried=3 retained=1')}}
    foreach($marker in $markers){if(!$raw.Contains($marker)){throw ('原位改建行为标记缺失：'+$marker)}}
    if($BlockedRead -and $Trace -and $raw -notmatch 'CF_OWNER_YIELD [^\r\n]* fallback=1'){throw '阻塞快照未实际使用新的通道前方让行'}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or ($ReadInputPath -and (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash)){throw '原位改建程序或源副本改变'}
    $midHash=if(!$Read -and !$Occupancy){Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'RenovationMid.sav')}else{''}
    $doneHash=if(!$Occupancy){Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'RenovationDone.sav')}else{''}
    if($Preview){
        $match=[regex]::Match($raw,'CF_RENOVATION_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$match.Success){throw '没有原位改建新截图目录'}
        $scenes=if($Read){@('Retained','Completed')}else{@('Planning','Retained','Completed')}
        foreach($scene in $scenes){
            $source=Join-Path $saved ('RenovationPreview\'+$match.Groups[1].Value+'\'+$scene+'.png')
            if(!(Test-Path -LiteralPath $source) -or (Get-Item -LiteralPath $source).LastWriteTimeUtc -lt $started){throw '原位改建截图缺失或不是本轮生成'}
            $bytes=[IO.File]::ReadAllBytes($source);if($bytes.Length -lt 1000 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80){throw '原位改建PNG文件头或大小无效'}
            $width=[int]$bytes[16]*16777216+[int]$bytes[17]*65536+[int]$bytes[18]*256+[int]$bytes[19];$height=[int]$bytes[20]*16777216+[int]$bytes[21]*65536+[int]$bytes[22]*256+[int]$bytes[23]
            if(($width.ToString()+'x'+$height) -ne $Resolution){throw '原位改建PNG实际尺寸不符'}
            Copy-Item -LiteralPath $source -Destination (Join-Path $run ($scene+'.png'))
        }
    }
    [pscustomobject]@{Occupancy=[bool]$Occupancy;BlockedRead=[bool]$BlockedRead;Trace=[bool]$Trace;Read=[bool]$Read;Preview=[bool]$Preview;Resolution=$Resolution;Pass=$true;BuildSHA256=$hash;ReadInputSHA256=$inputHash;MidSaveSHA256=$midHash;DoneSaveSHA256=$doneHash;Log=$log}|Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    if($Occupancy){Write-Host '门洞工人按真实导航离开未来墙体并完工的限定占位单元通过；不代表原家园整链或图形验收'}
    elseif($BlockedRead){Write-Host '真实阻塞快照的退出段复现通过；不代表本进程重新完成采集、改建或进入，完整整链另验'}
    else{Write-Host '原家园原位改建、原伙伴实际供料、承重保留及新入口生活往返通过；图形仍须逐图目视，不代表任意布局或自然风险验收'}
}finally{
    try{
        if($started){foreach($name in @('RenovationMid','RenovationDone','RenovationBlocked')){$path=Join-Path $slotRoot ($name+'.sav');if((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'-generated.sav'))}}}
        foreach($entry in $protected){if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原位改建测试原槽恢复失败'}}elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}}
    }finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
