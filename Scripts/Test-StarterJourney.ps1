param([string]$BuildRoot='',[string]$ReadInputPath='',[switch]$Farm,[switch]$Prepare,[switch]$Scout,[switch]$Rescue,[switch]$Recovery,[switch]$Resume,[switch]$Preview,[ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
if($ReadInputPath){$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path}
if(([int][bool]$Farm+[int][bool]$Prepare+[int][bool]$Scout+[int][bool]$Rescue+[int][bool]$Recovery) -gt 1){throw '农业、装备准备、接敌、倒地救援与基地恢复模式不能同时指定'}
if($Rescue){$Scout=$true}
if(($Farm -or $Prepare -or $Scout -or $Recovery) -and !$ReadInputPath){throw '农业、装备准备、接敌撤离或基地恢复段必须提供真实完成档'}
if($Resume -and !$Farm -and !$Prepare -and !$Scout -and !$Recovery){throw 'Resume仅用于农业、装备、接敌撤离或基地中途恢复；原开局恢复仍只需ReadInputPath'}
$readMode=if($Farm -or $Prepare -or $Scout -or $Recovery){[bool]$Resume}else{[bool]$ReadInputPath}
$prefix=if($Recovery){'StarterRecovery'}elseif($Rescue){'StarterRescue'}elseif($Scout){'StarterScout'}elseif($Prepare){'StarterPrep'}elseif($Farm){'StarterFarm'}else{'StarterJourney'}
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $projectRoot 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module;$sourceHash=if($ReadInputPath){Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath}else{''}
$run=Join-Path $projectRoot ('Saved/'+$prefix+'QA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$protected=@();$started=$null;$log=Join-Path $run 'StarterJourney.log'
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierStarterJourneyQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '正常开局旅程专项正在运行'}
try{
    $slots=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slots -Force|Out-Null
    foreach($name in @(($prefix+'Input'),($prefix+'Mid'),($prefix+'Done'),($prefix+'Blocked'))){
        $path=Join-Path $slots ($name+'.sav');if($path -eq $ReadInputPath){throw '恢复输入必须是独立副本'}
        $backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    if($ReadInputPath){Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slots ($prefix+'Input.sav'))}
    $flag='-CF'+$prefix+$(if($readMode){'Read'}else{'Test'})
    $arguments=@($flag,'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@(('-CF'+$prefix+'Preview'),'-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    $started=[DateTime]::UtcNow
    [pscustomobject]@{BuildRoot=$BuildRoot;Farm=[bool]$Farm;Prepare=[bool]$Prepare;Scout=[bool]$Scout;Rescue=[bool]$Rescue;Recovery=[bool]$Recovery;Resume=$readMode;ReadInputPath=$ReadInputPath;InputSHA256=$sourceHash;Preview=[bool]$Preview;Resolution=$Resolution;BuildSHA256=$hash}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    Write-Host ('正常开局伙伴旅程：'+$run)
    $p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $timeout=if($Farm){300000}else{240000}
    if(!$p.WaitForExit($timeout)){$p.Kill();$p.WaitForExit();throw '开局旅程超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('LogExit: Exiting.')){throw ('开局旅程失败：'+$log)}
    $markers=@('CF_STARTER_SOURCE PASS','CF_STARTER_RESOURCE PASS sourcePaid=3 experience=3','CF_STARTER_PRODUCTION PASS materialPaid=0 produced=3 rewards=24 material=33 claimed=3','CF_STARTER_LIFE PASS version=38 positionRetained=1 claimed=3 material=33 food=0 ecology=3','CF_STARTER_JOURNEY PASS reason=complete')
    if(!$ReadInputPath){$markers+='CF_STARTER_JOIN PASS input=1 foodPaid=3 material=18 capture=7 samePet=1'}
    if($Farm){
        $markers=@('CF_STARTER_FARM_SOURCE PASS','CF_STARTER_FARM_SEEDS PASS foodPaid=1 fiberPaid=1 seeds=3 input=1','CF_STARTER_FARM_ASSIGN PASS input=1 seedsStored=2 seedsRetained=1 samePet=1','CF_STARTER_FARM_HARVEST PASS farmFood=2 experience=5 claimed=4 medicine=3','CF_STARTER_FARM_LIFE PASS version=38 positionRetained=1 material=13 food=2 storedFood=0 seeds=1 claimed=4','CF_STARTER_FARM PASS reason=complete')
        if(!$readMode){$markers+=@('CF_STARTER_FARM_BUILD PASS input=1 paid=18 material=13','CF_STARTER_FARM_GATHER PASS fiber=1 nodePaid=1 input=1')}
    }
    if($Prepare){
        $markers=@('CF_STARTER_PREP_SOURCE PASS','CF_STARTER_PREP_VEST PASS input=1 cost=12 material=0 advisoryOnly=1','CF_STARTER_PREP_HINTS PASS states=5 toggles=2','CF_STARTER_PREP_LIFE PASS version=38 positionRetained=1 material=0 food=2 seeds=1 medicine=3 gear=2 claimed=4','CF_STARTER_PREP PASS reason=complete')
        if(!$readMode){$markers+=@('CF_STARTER_PREP_SPEAR PASS input=1 cost=8 material=5 vestRejected=1','CF_STARTER_PREP_RESOURCE PASS produced=7 sourcePaid=7 experience=12')}
    }
    if($Scout){
        $markers=@('CF_STARTER_SCOUT_SOURCE PASS','CF_STARTER_SCOUT_LIFE PASS version=38 positionRetained=1 homeRetained=1 suppliesRetained=1 claimed=4 bossAlive=1 avoid=1','CF_STARTER_SCOUT PASS reason=complete')
        if(!$readMode -and !$Rescue){$markers+=@('CF_STARTER_SCOUT_COMMAND PASS focus=1 special=1 samePet=1','CF_STARTER_SCOUT_CONTACT PASS')}
        if($Rescue -and !$readMode){$markers+='CF_STARTER_RESCUE_HEAL PASS input=1 medicinePaid=1 restored=40 samePet=1 avoid=1'}
        if($Rescue){$markers+=@('CF_PLACED_HABITAT_SOURCE PASS count=2 positionRetained=1 healthRetained=1 originalRanges=1','CF_PLACED_HABITAT_LIFE PASS count=2 positionRetained=1 homePersisted=1 safeSeconds=2','CF_STARTER_RESCUE_BUDGET PASS')}
    }
    if($Recovery){
        $markers=@('CF_STARTER_RECOVERY_SOURCE PASS','CF_STARTER_RECOVERY_GATHER PASS input=1 material=10 sourcePaid=10 experience=12','CF_STARTER_RECOVERY_MEDICINE PASS crafted=2 materialPaid=10 selfUsed=1 retained=1 fullHealthReject=1','CF_STARTER_RECOVERY_LIFE PASS version=38 positionRetained=1 heroHP=100 petFull=1 medicine=1 material=0 claimed=4','CF_STARTER_RECOVERY PASS reason=complete')
        if(!$readMode){$markers+='CF_STARTER_RECOVERY_REST PASS input=1 samePet=1 facility=1'}
    }
    foreach($marker in $markers){if(!$raw.Contains($marker)){throw ('缺少开局旅程证据：'+$marker)}}
    if($Preview){
        $pattern=if($Scout){'CF_STARTER_SCOUT_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})'}elseif($Prepare){'CF_STARTER_PREP_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})'}elseif($Farm){'CF_STARTER_FARM_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})'}else{'CF_STARTER_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})'}
        if($Recovery){$pattern='CF_STARTER_RECOVERY_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})'}
        $root=[regex]::Match($raw,$pattern);if(!$root.Success){throw '缺少独立截图目录'}
        $scenes=if($ReadInputPath){@('Working','ReadyForFarm')}else{@('Approach','Joined','Working','ReadyForFarm')}
        if($Farm){$scenes=if($readMode){@('Crafting','Assigned','Harvested','Home')}else{@('Planning','Built','Fiber','Crafting','Assigned','Harvested','Home')}}
        if($Prepare){$scenes=if($readMode){@('VestReady','SpearOff','VestOff','Prepared')}else{@('NeedSpear','Spear','Collecting','VestReady','SpearOff','VestOff','Prepared')}}
        if($Scout){$scenes=if($readMode){@('Returned')}else{@('Departure','Contact','Engaged','Returned')}}
        if($Rescue){$scenes=@('Returned')}
        if($Recovery){$scenes=@('Resting','Gathered','Resupplied','Recovered')}
        foreach($name in $scenes){
            $path=Join-Path $saved ($prefix+'Preview/'+$root.Groups[1].Value+'/'+$name+'.png')
            if(!(Test-Path $path) -or (Get-Item $path).LastWriteTimeUtc -lt $started){throw '缺少本轮新图'}
            $bytes=[IO.File]::ReadAllBytes($path);if($bytes.Length -lt 1000 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80){throw '截图格式错误'}
            $w=[int]$bytes[16]*16777216+[int]$bytes[17]*65536+[int]$bytes[18]*256+[int]$bytes[19];$h=[int]$bytes[20]*16777216+[int]$bytes[21]*65536+[int]$bytes[22]*256+[int]$bytes[23]
            if(($w.ToString()+'x'+$h) -ne $Resolution){throw '截图尺寸错误'};Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'.png'))
        }
    }
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or ($ReadInputPath -and (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $sourceHash)){throw '程序或输入源改变'}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;DoneSHA256=(Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slots ($prefix+'Done.sav')));Log=$log}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    if($Recovery){Write-Host '真实返家来源的伙伴休息、实际采料、制作补药和玩家治疗存读通过；图形仍须目视，不代表再次战斗通过'}elseif($Rescue){Write-Host '真实倒地来源的药品救援、有限补给支援、避战返家及存读通过；图形仍须目视，不代表首领击败'}elseif($Scout){Write-Host '真实来源接敌撤离、家园补给保留与存读通过；图形仍须目视，不代表首领击败或倒地救援通过'}elseif($Prepare){Write-Host '真实农业档的装备制作、原料守恒、准备提示与存读通过；不证明首领战斗通过'}elseif($Farm -and $readMode){Write-Host '自身采纤维中途档恢复、返基地制种、伙伴两次收获及第四项领奖通过'}elseif($Farm){Write-Host '真实开局档经原预览建圃、实际采纤维、制种、伙伴两次收获及第四项领奖通过；图形仍须目视'}elseif($ReadInputPath){Write-Host '自身结交中途档独立恢复、实际返基地生产、第三项领奖和存读通过；农业段仍待继续'}else{Write-Host '正常默认补给下实际走近、结交、返基地生产和前三项奖励通过；农业段仍待继续'}
}finally{
    try{
        if($started){foreach($entry in $protected){if((Test-Path $entry.Path) -and (Get-Item $entry.Path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ((Split-Path $entry.Path -LeafBase)+'-generated.sav'))}}}
        foreach($entry in $protected){if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原开局旅程测试槽恢复失败'}}elseif(Test-Path $entry.Path){Remove-Item -LiteralPath $entry.Path}}
    }finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
