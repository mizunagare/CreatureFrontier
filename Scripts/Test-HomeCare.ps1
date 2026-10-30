param([Parameter(Mandatory=$true)][string]$ReadInputPath,[string]$BuildRoot='',[switch]$Read,[switch]$ReservedRead,[switch]$ResumeOnly,[switch]$Resupply,[switch]$RestCycle,[switch]$Preview,
      [ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
if($RestCycle -and ($Resupply -or $ReservedRead)){throw '休息续工模式不能与采集补给或预留阻塞模式混用'}
if($Resupply -and $ReservedRead){throw '实际采集补种模式不能与预留阻塞档恢复混用'}
if($ReservedRead -and !$Read){throw '预留种子阻塞档恢复须同时指定读取模式'}
if($ResumeOnly -and !$ReservedRead){throw '仅复工验证须同时指定预留种子阻塞档恢复；不代表完整照料验收'}
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $projectRoot 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved/HomeCareQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$log=Join-Path $run 'HomeCare.log';$protected=@();$started=$null
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierHomeCareQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '家园生产照料专项正在运行'}
try{
    $slotRoot=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slotRoot -Force|Out-Null
    foreach($slot in @('HomeCareInput','HomeCareWork','HomeCareDone','HomeCareBlocked')){
        $path=Join-Path $slotRoot ($slot+'.sav');if($path -eq $ReadInputPath){throw '输入须为独立副本，不能使用受保护测试槽'}
        $backup=Join-Path $run ($slot+'-before.sav');$before=if(Test-Path $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slotRoot 'HomeCareInput.sav')
    $arguments=@($(if($Read){'-CFHomeCareRead'}else{'-CFHomeCareTest'}),'-CFFarmApproachTrace','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($ReservedRead){$arguments+='-CFHomeCareReservedRead'}
    if($ResumeOnly){$arguments+='-CFHomeCareResumeOnly'}
    if($Resupply){$arguments+='-CFHomeCareResupply'}
    if($RestCycle){$arguments+='-CFHomeCareRestCycle'}
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFHomeCarePreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    $started=[DateTime]::UtcNow
    [pscustomobject]@{Read=[bool]$Read;ReservedRead=[bool]$ReservedRead;ResumeOnly=[bool]$ResumeOnly;Resupply=[bool]$Resupply;RestCycle=[bool]$RestCycle;Preview=[bool]$Preview;Resolution=$Resolution;BuildSHA256=$hash;InputSHA256=$inputHash;StartedUTC=$started.ToString('o')}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    Write-Host ('原家园生产与照料：'+$run)
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(180000)){$process.Kill();$process.WaitForExit();throw '家园生产照料专项超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('LogExit: Exiting.')){throw ('家园生产照料专项失败：'+$log)}
    $markers=@('CF_HOME_CARE_HARVEST PASS food=1 xp=9 noDrain=1 duplicate=0','CF_HOME_CARE_LIFE PASS version=38 modules=25 facilities=2 seeds=0 food=0 xp=9 bond=12 observe=0 positionRetained=1','CF_HOME_CARE PASS reason=complete')
    if($ResumeOnly){$markers=@('CF_HOME_CARE_BLOCKED_HINT PASS taskRetained=1 reserved=1','CF_HOME_CARE_RESUME PASS taskRetained=1 planted=1 walked=1 xp=8','CF_HOME_CARE PASS reason=resume')}
    $markers+=if($ReservedRead){@('CF_HOME_CARE_RESTORE_RESERVED PASS version=38 planted=0 reserved=1 xp=8 positionRetained=1','CF_HOME_CARE_WORK PASS planted=1 seeds=0 xp=8')}elseif($Read){'CF_HOME_CARE_RESTORE PASS version=38 planted=1 seeds=0 xp=8'}else{@('CF_HOME_CARE_ORDER PASS seeds=0 reserved=1 aimed=1','CF_HOME_CARE_WORK PASS planted=1 seeds=0 xp=8')}
    if($Resupply){
        $markers=@('CF_HOME_CARE_HARVEST PASS food=1 xp=10 noDrain=1 duplicate=0','CF_HOME_CARE_LIFE PASS version=38 modules=25 facilities=2 seeds=2 food=0 xp=10 bond=15 observe=0 positionRetained=1','CF_HOME_CARE PASS reason=complete')
        $markers+=if($Read){'CF_HOME_CARE_RESTORE PASS version=38 planted=1 seeds=0 xp=9'}else{@('CF_HOME_RESUPPLY_GATHER PASS part=0 gained=2','CF_HOME_RESUPPLY_GATHER PASS part=1 gained=1','CF_HOME_RESUPPLY_CRAFT PASS material=0 fiber=0 food=0 seeds=3 menuInput=1','CF_HOME_CARE_ORDER PASS seeds=2 reserved=1 aimed=1','CF_HOME_CARE_WORK PASS planted=1 seeds=0 xp=9')}
    }
    if($RestCycle){
        $markers=@('CF_HOME_CARE_HARVEST PASS food=1 xp=11 noDrain=1 duplicate=0','CF_HOME_CARE_LIFE PASS version=38 modules=25 facilities=2 seeds=1 food=1 xp=11 bond=15 observe=0 positionRetained=1','CF_HOME_CARE PASS reason=complete','CF_HOME_REST_RESUME PASS taskRetained=1')
        $markers+=if($Read){'CF_HOME_REST_RESTORE PASS taskRetained=1 positionRetained=1'}else{@('CF_HOME_REST_ORDER PASS input=1 taskRetained=1','CF_HOME_CARE_WORK PASS planted=1 seeds=0 xp=10')}
    }
    foreach($marker in $markers){if(!$raw.Contains($marker)){throw ('缺少生产照料证据：'+$marker)}}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash){throw '程序或输入副本改变'}
    if($Preview){
        $root=[regex]::Match($raw,'CF_HOME_CARE_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$root.Success){throw '缺少独立截图目录'}
        $scenes=if($ResumeOnly){@('Blocked','Working')}elseif($ReservedRead){@('Blocked','Working','Harvest','Fed')}elseif($Read){@('Harvest','Fed')}else{@('Farm','Working','Harvest','Fed')}
        if($Resupply -and !$Read){$scenes=@('Material','Fiber','Recipe')+$scenes}
        if($Resupply -and $raw.Contains('CF_HOME_RESUPPLY_HUNGER_GATE PASS')){$scenes+=@('NotHungry')}
        if($RestCycle){$scenes=if($Read){@('Resumed','Harvest','Provisions')}else{@('Farm','Resting','Resumed','Harvest','Provisions')}}
        foreach($name in $scenes){
            $source=Join-Path $saved ('HomeCarePreview/'+$root.Groups[1].Value+'/'+$name+'.png')
            if(!(Test-Path $source) -or (Get-Item $source).LastWriteTimeUtc -lt $started){throw '生产照料截图缺失或不是本轮生成'}
            $bytes=[IO.File]::ReadAllBytes($source);if($bytes.Length -lt 1000 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80){throw '生产照料截图格式错误'}
            $width=[int]$bytes[16]*16777216+[int]$bytes[17]*65536+[int]$bytes[18]*256+[int]$bytes[19];$height=[int]$bytes[20]*16777216+[int]$bytes[21]*65536+[int]$bytes[22]*256+[int]$bytes[23]
            if(($width.ToString()+'x'+$height) -ne $Resolution){throw '生产照料截图尺寸不符'}
            Copy-Item -LiteralPath $source -Destination (Join-Path $run ($name+'.png'))
        }
    }
    [pscustomobject]@{Read=[bool]$Read;ReservedRead=[bool]$ReservedRead;ResumeOnly=[bool]$ResumeOnly;Resupply=[bool]$Resupply;RestCycle=[bool]$RestCycle;Preview=[bool]$Preview;Resolution=$Resolution;Pass=$true;BuildSHA256=$hash;InputSHA256=$inputHash;WorkSaveSHA256=$(if($Read -and !$ReservedRead){''}else{Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'HomeCareWork.sav')});DoneSaveSHA256=$(if($ResumeOnly){''}else{Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'HomeCareDone.sav')});Log=$log}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    if($RestCycle){Write-Host '主动休息保留任务、恢复生产、备粮与存读通过；不代表长期经营'}elseif($ResumeOnly){Write-Host '让开通路后原任务复工通过；仅为复工范围，不代表完整产粮照料验收'}else{Write-Host '原种子产粮、取粮照料、实际往返与存读通过；受控路线及图形目视边界保留'}
}finally{
    try{
        if($started){foreach($slot in @('HomeCareWork','HomeCareDone','HomeCareBlocked')){$path=Join-Path $slotRoot ($slot+'.sav');if((Test-Path $path) -and (Get-Item $path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $path -Destination (Join-Path $run ($slot+'-generated.sav'))}}}
        foreach($entry in $protected){if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '生产照料原测试槽恢复失败'}}elseif(Test-Path $entry.Path){Remove-Item -LiteralPath $entry.Path}}
    }finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
