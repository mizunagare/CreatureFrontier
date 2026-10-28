param([string]$EngineRoot='D:\Epic\UE_5.8',[string[]]$Resolutions=@('960x540','1280x720','1920x1080'))
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$module=Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'
$hash=(Get-FileHash -LiteralPath $module).Hash
$root=Join-Path $projectRoot ('Saved\RescueRecallRenderQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $root | Out-Null
Write-Host "归家回忆图形证据：$root"
$qaMutex=[System.Threading.Mutex]::new($false,'Local\CreatureFrontierRescueMemoryQA')
if(!$qaMutex.WaitOne(0)){$qaMutex.Dispose();throw '脱险经历专项正在使用相同测试槽，请串行运行'}
$doneSlot=Join-Path $projectRoot 'Saved\SaveGames\RescueRecallDone.sav'
$backup=Join-Path $root 'RescueRecallDone-before.sav'
$originalHash=''
if(Test-Path -LiteralPath $doneSlot){$originalHash=(Get-FileHash -LiteralPath $doneSlot).Hash;Copy-Item -LiteralPath $doneSlot -Destination $backup}
$results=@()
try{
    foreach($resolution in $Resolutions){
        if($resolution -notmatch '^(\d+)x(\d+)$'){throw '分辨率必须为宽x高'}
        $width=[int]$Matches[1];$height=[int]$Matches[2]
        if($width -lt 960 -or $height -lt 540){throw '本专项最低960x540'}
        $folder=Join-Path $root $resolution;New-Item -ItemType Directory -Path $folder | Out-Null
        $log=Join-Path $folder 'Preview.log';$started=[DateTime]::UtcNow
        $previewArgs=@((Join-Path $projectRoot 'CreatureFrontier.uproject'),'-game','-CFRescueRecallTest','-CFRescueRecallPreview','-RenderOffscreen','-ForceRes',('-ResX='+$width),('-ResY='+$height),'-nosound','-unattended','-nop4','-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.DefaultFeature.MotionBlur 0,r.AntiAliasingMethod 1"',('-abslog="'+$log+'"'))
        $process=Start-Process -FilePath $exe -ArgumentList $previewArgs -WindowStyle Hidden -PassThru
        if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw '图形专项超时'}
        if($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch 'CF_RESCUE_RECALL PASS' -Quiet) -or (Select-String -LiteralPath $log -Pattern 'CF_\w+ FAIL|Fatal error|Assertion failed' -Quiet)){throw "图形专项失败：$log"}
        foreach($scene in @('Dialog','Roster','Context')){
            $source=Join-Path $projectRoot ('Saved\RescueRecallPreview\'+$scene+'.png')
            $info=Get-Item -LiteralPath $source
            if($info.Length -lt 1000 -or $info.LastWriteTimeUtc -lt $started){throw '截图不是本轮生成'}
            $bytes=[IO.File]::ReadAllBytes($source)
            $actualWidth=[int]$bytes[16]*16777216+[int]$bytes[17]*65536+[int]$bytes[18]*256+[int]$bytes[19]
            $actualHeight=[int]$bytes[20]*16777216+[int]$bytes[21]*65536+[int]$bytes[22]*256+[int]$bytes[23]
            if($actualWidth -ne $width -or $actualHeight -ne $height){throw '截图尺寸不符'}
            Copy-Item -LiteralPath $source -Destination (Join-Path $folder ($scene+'.png'))
        }
        if((Get-FileHash -LiteralPath $module).Hash -ne $hash){throw '模块发生变化'}
        $results += [pscustomobject]@{Resolution=$resolution;Pass=$true;Images=3;BuildSHA256=$hash;Log=$log}
        $results | Export-Csv -LiteralPath (Join-Path $root 'Results.csv') -NoTypeInformation
        Write-Host "$resolution 三张新图检查通过，仍需逐图查看"
    }
}finally{
    try{if($originalHash){Copy-Item -LiteralPath $backup -Destination $doneSlot;if((Get-FileHash -LiteralPath $doneSlot).Hash -ne $originalHash){throw '原测试槽恢复失败'}}}
    finally{$qaMutex.ReleaseMutex();$qaMutex.Dispose()}
}
