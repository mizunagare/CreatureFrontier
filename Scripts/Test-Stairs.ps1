param([string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8',
      [ValidateSet('Test','Read','Legacy')][string]$Mode='Test',[string]$ReadInputPath='',
      [switch]$Preview,[string]$Resolution='1280x720',[switch]$Work)
$ErrorActionPreference='Stop'
if($Work -and ($Mode -eq 'Legacy' -or $Preview)){throw '二层农圃目前仅支持普通或独立任务恢复，不可混用旧档和楼梯预览'}
if(($Mode -eq 'Test' -and $ReadInputPath) -or ($Mode -ne 'Test' -and !$ReadInputPath)){throw '普通模式不接收外部档；读取与旧档模式须明确指定输入副本'}
if($Preview -and $Mode -ne 'Test'){throw '楼梯预览仅用于完整普通模式'}
if($Resolution -notmatch '^(\d{3,4})x(\d{3,4})$'){throw '分辨率须为宽x高'}
$width=[int]$Matches[1];$height=[int]$Matches[2]
if($width -lt 960 -or $height -lt 540 -or $width -gt 3840 -or $height -gt 2160){throw '楼梯预览分辨率须在960×540至3840×2160内'}
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$projectRoot=Split-Path $PSScriptRoot -Parent
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved'}else{Join-Path $projectRoot 'Saved'}
$run=Join-Path $projectRoot ('Saved\StairsQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$log=Join-Path $run 'Stairs.log'
$inputHash=''
if($ReadInputPath){$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path;$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath}
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierStairsQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '另一个楼梯专项正在运行'}
$protected=@()
try{
    $slotRoot=Join-Path $saved 'SaveGames'
    $slots=@('StairsAutomation','StairsInvalid')
    if($Work){$slots+=@('UpperFarmAutomation','UpperFarmDoneAutomation')}
    foreach($slot in $slots){
        $path=Join-Path $slotRoot ($slot+'.sav');$backup=Join-Path $run ($slot+'-before.sav')
        $before=if(Test-Path -LiteralPath $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup}
        $protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    if($ReadInputPath){
        $destination=Join-Path $slotRoot $(if($Work){'UpperFarmAutomation.sav'}else{'StairsAutomation.sav'})
        if($ReadInputPath -eq $destination){throw '输入必须是独立副本，不能直接使用会被测试恢复的槽'}
        New-Item -ItemType Directory -Path $slotRoot -Force | Out-Null
        Copy-Item -LiteralPath $ReadInputPath -Destination $destination
    }
    $flag=if($Mode -eq 'Read'){'-CFStairsRead'}elseif($Mode -eq 'Legacy'){'-CFStairsLegacy'}else{'-CFStairsTest'}
    $arguments=@($flag,'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Work){$arguments+='-CFStairsWork'}
    if($Preview){$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFStairsPreview','-RenderOffscreen','-ForceRes',('-ResX='+$width),('-ResY='+$height),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}else{Join-Path $EngineRoot ('Engine\Binaries\Win64\'+$(if($Preview){'UnrealEditor.exe'}else{'UnrealEditor-Cmd.exe'}))}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    $started=[DateTime]::UtcNow
    Write-Host ('楼梯与二层专项：'+$run)
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(120000)){$process.Kill();$process.WaitForExit();throw ('楼梯专项超时：'+$log)}
    if($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $log)){throw ('楼梯专项未正常退出：'+$log)}
    $body=Get-Content -LiteralPath $log -Raw
    if($body -match '\bFAIL\b|Fatal error|Assertion failed' -or $body -notmatch 'LogExit: Exiting\.') {throw ('楼梯专项出现失败或未正常关闭：'+$log)}
    $markers=if($Work){
        @('CF_UPPER_FARM_PRODUCED PASS upstairs=1 actualSteps=1 seed=0 food=1 xp=1',
          'CF_UPPER_FARM_WITHDRAW PASS upstairs=1 food=1 stored=0 follow=1',
          'CF_UPPER_FARM_RETURN PASS downstairs=1 continuous=1 food=1 seeds=1 material=94',
          'CF_UPPER_FARM_DONE_SAVE PASS version=34 modules=12 farm=1 food=1 seeds=1 material=94 follow=1',
          ('CF_UPPER_FARM PASS mode='+$(if($Mode -eq 'Read'){1}else{0})+' phase=4 reason=complete'))
    }elseif($Mode -eq 'Legacy'){@('CF_STAIRS_LEGACY PASS version=33 modules=14 identity=1 inventory=1')}else{
        @('CF_STAIRS_DOWN PASS continuous=1','CF_STAIRS_OCCUPIED PASS rejected=1 material=109','CF_STAIRS_DISMANTLE PASS refund=6 blocked=1 modules=11 material=115',('CF_STAIRS PASS mode='+$(if($Mode -eq 'Read'){1}else{0})+' phase=6 reason=complete'))
    }
    if($Work){
        if($Mode -eq 'Test'){$markers+=@('CF_STAIRS_INPUT PASS modules=12 material=112 upper=1 stairs=1 rotate4=1 support=1 headroom=1','CF_STAIRS_FACILITY_SPACE PASS valid=1 support=1 noBuild=1','CF_UPPER_FARM_INPUT PASS modules=12 material=94 seeds=1 reserved=1','CF_UPPER_FARM_ORDER PASS downstairs=1 aimed=1','CF_UPPER_FARM_SAVED PASS version=34 downstairs=1 task=1 seed=1 food=0')}
        else{$markers+='CF_UPPER_FARM_RESTORE PASS version=34 modules=12 farm=1 downstairs=1 task=1 seed=1 food=0'}
    }else{
        if($Mode -eq 'Test'){$markers+=@('CF_STAIRS_INPUT PASS modules=12 material=112 upper=1 stairs=1 rotate4=1 support=1 headroom=1','CF_STAIRS_FACILITY_SPACE PASS valid=1 support=1 noBuild=1','CF_STAIRS_NAV PASS partial=0','CF_STAIRS_UP PASS','landed=1 actualSteps=1 stored=3 material=109','CF_STAIRS_SAVE PASS version=34 identity=1 inventory=1 upper=1 bad33=1','CF_STAIRS_RESUME_INPUT PASS slow=1')}
        if($Mode -eq 'Read'){$markers+='CF_STAIRS_RESTORE PASS version=34 modules=12 stored=3 material=109'}
    }
    foreach($marker in $markers){if(!$body.Contains($marker)){throw ('缺少指定楼梯行为标记：'+$marker)}}
    if($Work){
        $identities=[regex]::Matches($body,'CF_UPPER_FARM_(?:ORDER|SAVED|RESTORE|PRODUCED|WITHDRAW|RETURN|DONE_SAVE) PASS[^\r\n]* pet=([A-F0-9]{32}) farm(?:Id)?=([A-F0-9]{32})')
        if($identities.Count -ne $(if($Mode -eq 'Read'){5}else{6}) -or @($identities|ForEach-Object {$_.Groups[1].Value}|Select-Object -Unique).Count -ne 1 -or @($identities|ForEach-Object {$_.Groups[2].Value}|Select-Object -Unique).Count -ne 1){throw '二层工作、生产和保存恢复的伙伴或农圃编号不完整或不一致'}
    }elseif($Mode -ne 'Legacy'){
        $identities=[regex]::Matches($body,'CF_STAIRS_(?:UP|SAVE|RESTORE|DOWN) PASS[^\r\n]* pet=([A-F0-9]{32})')
        if($identities.Count -ne $(if($Mode -eq 'Read'){2}else{3}) -or @($identities|ForEach-Object {$_.Groups[1].Value}|Select-Object -Unique).Count -ne 1){throw '上下楼与保存恢复的伙伴编号记录不完整或不一致'}
    }
    if($Preview){
        $rootMatch=[regex]::Match($body,'CF_STAIRS_PREVIEW_ROOT [^\r\n]*?([A-F0-9]{32})\s*\r?\n')
        if(!$rootMatch.Success){throw '未记录楼梯独立截图目录'}
        foreach($scene in @('Preview','Upper','Occupied')){
            if($body -notmatch ('CF_STAIRS_SHOT scene='+$scene+' viewport='+$Resolution+' frame=\d+')){throw '缺少本轮实际渲染帧记录'}
            $source=Join-Path $saved ('StairsPreview\'+$rootMatch.Groups[1].Value+'\'+$scene+'.png')
            if(!(Test-Path -LiteralPath $source) -or (Get-Item -LiteralPath $source).LastWriteTimeUtc -lt $started){throw '楼梯截图缺失或不是本轮新图'}
            $png=[IO.File]::ReadAllBytes($source)
            if($png.Length -lt 1000 -or $png[0] -ne 137 -or $png[1] -ne 80){throw '楼梯截图不是有效PNG'}
            $pngWidth=[int]$png[16]*16777216+[int]$png[17]*65536+[int]$png[18]*256+[int]$png[19]
            $pngHeight=[int]$png[20]*16777216+[int]$png[21]*65536+[int]$png[22]*256+[int]$png[23]
            if($pngWidth -ne $width -or $pngHeight -ne $height){throw '楼梯截图实际尺寸不符'}
            Copy-Item -LiteralPath $source -Destination (Join-Path $run ($scene+'.png'))
        }
    }
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '楼梯验证期间程序发生变化'}
    if($ReadInputPath -and (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash){throw '楼梯读取源副本被改变'}
    $saveHash='';$doneHash=''
    if($Work){
        $generated=Join-Path $slotRoot 'UpperFarmAutomation.sav';$saveHash=Get-FrontierBuildFingerprint -LiteralPath $generated
        Copy-Item -LiteralPath $generated -Destination (Join-Path $run 'UpperFarm-generated.sav')
        $done=Join-Path $slotRoot 'UpperFarmDoneAutomation.sav';$doneHash=Get-FrontierBuildFingerprint -LiteralPath $done
        Copy-Item -LiteralPath $done -Destination (Join-Path $run 'UpperFarmDone-generated.sav')
        if($Mode -eq 'Read' -and $saveHash -ne $inputHash){throw '二层工作态读取后源测试槽被改写'}
    }elseif($Mode -ne 'Legacy'){$generated=Join-Path $slotRoot 'StairsAutomation.sav';$saveHash=Get-FrontierBuildFingerprint -LiteralPath $generated;Copy-Item -LiteralPath $generated -Destination (Join-Path $run 'Stairs-generated.sav')}
    [pscustomobject]@{Mode=$Mode;Work=[bool]$Work;Preview=[bool]$Preview;Resolution=$Resolution;Pass=$true;BuildSHA256=$hash;ReadInputSHA256=$inputHash;GeneratedSaveSHA256=$saveHash;DoneSaveSHA256=$doneHash;Log=$log}|Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    $label=if($Work){'二层农圃工作'}else{'楼梯'}
    $modeLabel=switch($Mode){'Test'{'普通'} 'Read'{'独立恢复'} 'Legacy'{'旧档'}}
    Write-Host ($label+$modeLabel+'专项通过；图形模式仍须逐图检查，固定夹具不代表完整自由建造验收')
}finally{
    try{foreach($entry in $protected){
        if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '楼梯测试原槽恢复不一致'}}
        elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}
    }}finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
