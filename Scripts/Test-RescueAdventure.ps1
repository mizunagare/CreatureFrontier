param([string]$BuildRoot='', [switch]$Editor, [switch]$ReuseInput, [switch]$Preview, [switch]$Journey, [switch]$Celebrate,
      [int]$Width=1280, [int]$Height=720,
      [string]$EngineRoot='D:\Epic\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if($Celebrate -and !$Journey){throw 'Celebrate需要Journey模式'}
if($Width -lt 640 -or $Width -gt 4096 -or $Height -lt 360 -or $Height -gt 2160){throw '预览尺寸超出安全范围'}
if($Editor){
    $exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    $working=$projectRoot
    $prefix=@((Join-Path $projectRoot 'CreatureFrontier.uproject'),'/Game/Maps/L_Prototype','-game')
    $saveRoot=Join-Path $projectRoot 'Saved\SaveGames'
}else{
    if(!$BuildRoot){throw '打包模式必须指定BuildRoot'}
    $exe=Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
    $working=$BuildRoot;$prefix=@();$saveRoot=Join-Path $BuildRoot 'CreatureFrontier\Saved\SaveGames'
}
if(!(Test-Path -LiteralPath $exe)){throw '测试程序不存在'}
$run=Join-Path $projectRoot ('Saved\RescueAdventureQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$buildHash=(Get-FileHash -LiteralPath $exe).Hash
$module=if($Editor){Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}else{''}
$moduleHash=if($module){(Get-FileHash -LiteralPath $module).Hash}else{''}
$cases=@()
if(!$ReuseInput){$cases+=@{Flag='CFRescueNaturalWrite';Marker='CF_RESCUE_NATURAL_WRITE PASS';Timeout=115}}
$cases+=@{Flag='CFRescueNaturalResume';Marker='CF_RESCUE_NATURAL_RESUME PASS';Timeout=135}
$cases+=@{Flag='CFRescueNaturalRead';Marker='CF_RESCUE_NATURAL_READ PASS';Timeout=60}
$source=Join-Path $saveRoot 'RescueNaturalDowned.sav'
if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination (Join-Path $run 'Input-before.sav')}
$inputHash=if($ReuseInput){(Get-FileHash -LiteralPath $source).Hash}else{''}
$results=@()
Write-Host "救助证据目录：$run"
foreach($case in $cases){
    $log=Join-Path $run ($case.Flag+'.log')
    $arguments=$prefix+@('-nosound','-unattended','-nop4',('-'+$case.Flag),('-abslog="'+$log+'"'))
    if($Journey){$arguments+='-CFRescueJourney'}
    if($Celebrate){$arguments+='-CFRescueJourneyCelebrate'}
    if($Preview -and $case.Flag -eq 'CFRescueNaturalResume'){
        $arguments+=@('-CFRescuePreview','-RenderOffscreen','-ForceRes',('-ResX='+$Width),('-ResY='+$Height),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.DefaultFeature.MotionBlur 0,r.AntiAliasingMethod 1"')
    }else{$arguments+='-nullrhi'}
    $startedUtc=[DateTime]::UtcNow
    $process=Start-Process -FilePath $exe -WorkingDirectory $working -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit($case.Timeout*1000)){$process.Kill();$process.WaitForExit();throw "救助测试超时：$log"}
    if($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch $case.Marker -Quiet) -or (Select-String -LiteralPath $log -Pattern 'CF_RESCUE.*FAIL|Fatal error|Assertion failed' -Quiet)){
        $failureRecords=@()
        foreach($slot in @('RescueReturnFailure','RescueBattleFailure')){
            $failureSource=Join-Path $saveRoot ($slot+'.sav')
            if((Test-Path -LiteralPath $failureSource) -and (Get-Item -LiteralPath $failureSource).LastWriteTimeUtc -ge $startedUtc){
                Copy-Item -LiteralPath $failureSource -Destination (Join-Path $run ($slot+'.sav'))
                $failureRecords+=[pscustomobject]@{Flag=$case.Flag;Slot=$slot;SHA256=(Get-FileHash -LiteralPath $failureSource).Hash;Pass=$false;Log=$log}
            }
        }
        if($failureRecords.Count){$failureRecords | Export-Csv -LiteralPath (Join-Path $run 'Failures.csv') -NoTypeInformation}
        if($inputHash -and (Get-FileHash -LiteralPath $source).Hash -ne $inputHash){throw "救助失败且原输入被改写：$log"}
        throw "救助测试失败：$log"
    }
    if(!$inputHash){$inputHash=(Get-FileHash -LiteralPath $source).Hash}
    if($Journey){
        $journeyMarker=if($case.Flag -eq 'CFRescueNaturalWrite'){'CF_RESCUE_JOURNEY_SOURCE PASS'}elseif($case.Flag -eq 'CFRescueNaturalRead'){'CF_RESCUE_JOURNEY_READ PASS'}else{'CF_RESCUE_JOURNEY_COMMIT PASS'}
        if(!(Select-String -LiteralPath $log -SimpleMatch $journeyMarker -Quiet)){throw '缺少真实脱险经历成功标记'}
        if($case.Flag -eq 'CFRescueNaturalResume' -and !(Select-String -LiteralPath $log -SimpleMatch 'CF_RESCUE_JOURNEY_RESPONSE PASS' -Quiet)){throw '缺少正常Tick动作回应标记'}
    }
    if((Get-FileHash -LiteralPath $source).Hash -ne $inputHash){throw '倒地输入存档被改写'}
    $inputCopy=Join-Path $run 'Input.sav'
    if(!(Test-Path -LiteralPath $inputCopy)){Copy-Item -LiteralPath $source -Destination $inputCopy}
    if((Get-FileHash -LiteralPath $inputCopy).Hash -ne $inputHash){throw '留存输入副本哈希不符'}
    if($case.Flag -eq 'CFRescueNaturalRead'){Copy-Item -LiteralPath (Join-Path $saveRoot 'RescueNaturalFinal.sav') -Destination (Join-Path $run 'Final.sav')}
    if($Preview -and $case.Flag -eq 'CFRescueNaturalResume'){
        $contents=Get-Content -LiteralPath $log -Raw
        $rootMatch=[regex]::Match($contents,'CF_RESCUE_SHOT_ROOT [^\r\n]*[/\\]([A-F0-9]{32})')
        if(!$rootMatch.Success){throw '缺少新生成取景目录'}
        $shots=Join-Path (Split-Path $saveRoot -Parent) ('RescuePreview\'+$rootMatch.Groups[1].Value)
        $frames=@()
        $scenes=@('Restored','Rescued','Home');if($Journey){$scenes+=@('MemoryResponse','MemoryDialog')}
        foreach($scene in $scenes){
            if($scene -ne 'MemoryDialog'){
                $match=[regex]::Match($contents,('CF_RESCUE_SHOT '+$scene+' viewport='+$Width+'x'+$Height+' frame=(\d+)'))
                if(!$match.Success){throw "缺少实际取景帧：$scene"};$frames+=$match.Groups[1].Value
            }
            $sourceImage=Join-Path $shots ($scene+'.png')
            if(!(Test-Path -LiteralPath $sourceImage) -or (Get-Item -LiteralPath $sourceImage).Length -lt 1000){throw "缺少取景图片：$scene"}
            $header=New-Object byte[] 24;$stream=[IO.File]::OpenRead($sourceImage)
            try{$read=$stream.Read($header,0,24)}finally{$stream.Dispose()}
            $pngWidth=[int]$header[16]*16777216+[int]$header[17]*65536+[int]$header[18]*256+[int]$header[19]
            $pngHeight=[int]$header[20]*16777216+[int]$header[21]*65536+[int]$header[22]*256+[int]$header[23]
            if($read -ne 24 -or $header[0] -ne 137 -or $header[1] -ne 80 -or $pngWidth -ne $Width -or $pngHeight -ne $Height){throw "PNG尺寸或格式不符：$scene"}
            Copy-Item -LiteralPath $sourceImage -Destination (Join-Path $run ($scene+'.png'))
        }
        $expectedFrames=if($Journey){4}else{3}
        if(@($frames | Sort-Object -Unique).Count -ne $expectedFrames){throw '取景布局帧重复'}
    }
    if((Get-FileHash -LiteralPath $exe).Hash -ne $buildHash){throw '测试程序发生变化'}
    if($module -and (Get-FileHash -LiteralPath $module).Hash -ne $moduleHash){throw 'Editor游戏模块发生变化'}
    $results+=[pscustomobject]@{Flag=$case.Flag;Pass=$true;Editor=[bool]$Editor;Preview=[bool]$Preview;Journey=[bool]$Journey;Celebrate=[bool]$Celebrate;Resolution=($Width.ToString()+'x'+$Height);InputSHA256=$inputHash;BuildSHA256=$buildHash;ModuleSHA256=$moduleHash;Log=$log}
    $results | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host "$($case.Flag)：通过，输入文件未改写"
}
