param([string]$BuildRoot='',[switch]$Read,[string]$ReadInputPath='',[switch]$Needs,[switch]$InputOnly,[switch]$Preview,[string]$Resolution='1280x720',[switch]$ExistingHome)
$ErrorActionPreference='Stop'
if([bool]($Read -or $ExistingHome) -ne [bool]$ReadInputPath){throw '恢复或原家园模式必须指定独立输入副本；其他模式不接收外部档'}
if(($InputOnly -and ($Read -or $Needs -or $ExistingHome)) -or ($ExistingHome -and ($Needs -or $Preview))){throw '施工输入、原家园及进食预览模式不能混用'}
if($Resolution -notmatch '^(\d{3,4})x(\d{3,4})$'){throw '施工截图分辨率须为宽x高'}
$width=[int]$Matches[1];$height=[int]$Matches[2];if($width -lt 960 -or $width -gt 3840 -or $height -lt 540 -or $height -gt 2160){throw '施工截图分辨率须在960×540至3840×2160之间'}
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved'}else{Join-Path $projectRoot 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$inputHash='';if($ReadInputPath){$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path;$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath}
$run=Join-Path $projectRoot ('Saved\ConstructionQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$log=Join-Path $run 'Construction.log';$protected=@()
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierConstructionQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '施工专项正在运行'}
try{
    $slotRoot=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slotRoot -Force | Out-Null
    foreach($name in @('ConstructionInput','ConstructionMid','ConstructionRest','ConstructionDone','ConstructionShortage','ConstructionBad','ConstructionCancelled','ConstructionHomeMid','ConstructionHomeDone')){
        $path=Join-Path $slotRoot ($name+'.sav');if($ReadInputPath -and $path -eq $ReadInputPath){throw '输入必须是独立副本，不能使用会被恢复的测试槽本身'}
        $backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path -LiteralPath $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    if($ReadInputPath){Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slotRoot 'ConstructionInput.sav')}
    $flag=if($ExistingHome){if($Read){'-CFConstructionHomeRead'}else{'-CFConstructionHome'}}elseif($InputOnly){'-CFConstructionInput'}elseif($Read){'-CFConstructionRead'}else{'-CFConstructionTest'}
    $arguments=@($flag,'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Needs){$arguments+='-CFConstructionNeeds'}
    if($Preview){$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFConstructionPreview','-RenderOffscreen','-ForceRes',('-ResX='+$width),('-ResY='+$height),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'}else{'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    Write-Host ('伙伴施工专项：'+$run);$started=[DateTime]::UtcNow
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(180000)){$process.Kill();$process.WaitForExit();throw '施工专项超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('LogExit: Exiting.')){throw ('施工专项失败：'+$log)}
    $markers=@()
    if($ExistingHome){$markers=@('CF_CONSTRUCTION_HOME_DONE PASS version=35 modules=19 retained=18 material=0 food=1 seeds=1 cost=4','CF_CONSTRUCTION_HOME PASS reason=complete');$markers+=if($Read){'CF_CONSTRUCTION_HOME_RESTORE PASS version=35 carried=3 stock=1 identity=1'}else{@('CF_CONSTRUCTION_HOME_ORIGIN PASS version=34 modules=19 refund=2 originalMaterial=2 stock=4','CF_CONSTRUCTION_HOME_MID PASS version=35 carried=3 identity=1')}}
    elseif($InputOnly){$markers=@('CF_CONSTRUCTION_INPUT PASS modifier=1 mapped=1 preview=1 material=12 paid=0')}
    else{
        $markers=@('CF_CONSTRUCTION_SHORTAGE PASS supplied=3 work=0 reserved=1','CF_CONSTRUCTION_BAD PASS cases=7 retained=1 cancel=1 paused=1 sourceProtected=1','CF_CONSTRUCTION_REST PASS version=35','CF_CONSTRUCTION_DONE PASS version=35 material=66 cost=6 identity=1 actualNav=1 rest=1','CF_CONSTRUCTION PASS reason=complete')
        $markers+=if($Read){'CF_CONSTRUCTION_COLD PASS version=35 carried=3 intent=1'}else{@('CF_CONSTRUCTION_QUEUE PASS material=69 paid=0 support=1 input=1','CF_CONSTRUCTION_MID PASS version=35 carried=3 identity=1 intent=1')}
        $markers+='CF_CONSTRUCTION_CANCEL_CAPACITY PASS retained=2 refunded=2'
        $markers+='CF_CONSTRUCTION_PATH_RETRY PASS calls=60 searches=1 retained=1'
        if(!$Read){$markers+='CF_CONSTRUCTION_CANCEL_CARRIED PASS saved=1 held=3 recovered=3 reassigned=1'}
        if($Needs){$markers+=@('CF_CONSTRUCTION_FED PASS consumed=1 intent=1 workRetained=1','CF_CONSTRUCTION_FOOD_RESUME PASS actualReturn=1 workAdvanced=1')}
    }
    foreach($m in $markers){if(!$raw.Contains($m)){throw ('施工标记缺失：'+$m)}}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or ($ReadInputPath -and (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash)){throw '测试程序或源副本改变'}
    $midHash='';$restHash='';$doneHash=''
    if($ExistingHome){if(!$Read){$midHash=Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'ConstructionHomeMid.sav')};$doneHash=Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'ConstructionHomeDone.sav')}
    elseif(!$InputOnly){if(!$Read){$midHash=Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'ConstructionMid.sav')};$restHash=Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'ConstructionRest.sav');$doneHash=Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'ConstructionDone.sav')}
    if($Preview){
        $rootMatch=[regex]::Match($raw,'CF_CONSTRUCTION_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})')
        if(!$rootMatch.Success){throw '缺少施工截图独立目录'}
        $scenes=if($InputOnly){@('Planning','Scaffold')}else{@('Shortage','Working','Done')}
        foreach($scene in $scenes){
            $path=Join-Path $saved ('ConstructionPreview\'+$rootMatch.Groups[1].Value+'\'+$scene+'.png')
            if(!(Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).LastWriteTimeUtc -lt $started){throw '施工截图缺失或不是本轮生成'}
            $bytes=[IO.File]::ReadAllBytes($path);if($bytes.Length -lt 1000 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80){throw '施工截图不是有效PNG'}
            $actualWidth=[int]$bytes[16]*16777216+[int]$bytes[17]*65536+[int]$bytes[18]*256+[int]$bytes[19];$actualHeight=[int]$bytes[20]*16777216+[int]$bytes[21]*65536+[int]$bytes[22]*256+[int]$bytes[23]
            if($actualWidth -ne $width -or $actualHeight -ne $height){throw '施工截图实际尺寸不符'}
            Copy-Item -LiteralPath $path -Destination (Join-Path $run ($scene+'.png'))
        }
    }
    [pscustomobject]@{Read=[bool]$Read;Needs=[bool]$Needs;Home=[bool]$ExistingHome;Input=[bool]$InputOnly;Preview=[bool]$Preview;Resolution=$Resolution;Pass=$true;BuildSHA256=$hash;ReadInputSHA256=$inputHash;MidSaveSHA256=$midHash;RestSaveSHA256=$restHash;DoneSaveSHA256=$doneHash;Log=$log}|Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    if($InputOnly){Write-Host '施工架预览及已注册输入放置通过；不代表系统鼠标／键盘或自然建房体验验收'}
    elseif($ExistingHome){Write-Host '原家园伙伴实际搬料重建、物资／身份守恒及完成存读通过；不代表任意布局或长期生活验收'}
    else{Write-Host '伙伴实际供料、缺料等待、施工、休息及存读专项通过；独立恢复和图形以各自运行证据为准，不等同完整伙伴建设交付'}
}finally{
    try{
        if($started){foreach($name in @('ConstructionMid','ConstructionRest','ConstructionDone','ConstructionHomeMid','ConstructionHomeDone')){$path=Join-Path $slotRoot ($name+'.sav');if((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'-generated.sav'))}}}
        foreach($entry in $protected){if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '施工原测试槽恢复失败'}}elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}}
    }finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
