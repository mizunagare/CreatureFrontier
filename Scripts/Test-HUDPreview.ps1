param([string]$BuildRoot='',
      [string[]]$Resolutions=@('960x540','1280x720','1920x1080'),[switch]$Rescue,
      [switch]$Editor,[switch]$AvoidCombat,[switch]$HomeMode,[switch]$Orientation,[string]$EngineRoot='D:\Epic\UE_5.8')
$ErrorActionPreference='Stop'
if(($Editor -and $BuildRoot) -or (!$Editor -and !$BuildRoot)){throw '请只指定Editor或BuildRoot之一'}
if(!$Resolutions.Count -or @($Resolutions | Select-Object -Unique).Count -ne $Resolutions.Count){throw '分辨率列表不能为空或重复'}
foreach($resolution in $Resolutions){
    if($resolution -notmatch '^(\d{3,4})x(\d{3,4})$'){throw '分辨率须为宽x高，例如960x540'}
    if([int]$Matches[1] -lt 640 -or [int]$Matches[1] -gt 4096 -or [int]$Matches[2] -lt 360 -or [int]$Matches[2] -gt 2160){throw '分辨率超出测试安全范围'}
}
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$working=if($Editor){$projectRoot}else{(Resolve-Path -LiteralPath $BuildRoot).Path}
$exe=if($Editor){Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'}else{Join-Path $working 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}
$module=if($Editor){Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}else{$exe}
if(!(Test-Path -LiteralPath $exe)){throw '找不到预览程序'}
$buildHash=Get-FrontierBuildFingerprint -LiteralPath $module
$evidence=Join-Path $projectRoot ('Saved\HUDRenderQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $evidence | Out-Null
Write-Host "中文界面图形证据：$evidence"
$scenes=@('Companion','Roster','RosterDanger','HelpDanger','Resource','Garden','Building','Downed')
if($Rescue){$scenes+=@('CompanionDownedNoMedicine','CompanionDownedMedicine')}
$results=@()
foreach($resolution in $Resolutions){
    if($resolution -notmatch '^(\d{3,4})x(\d{3,4})$'){throw '分辨率须为宽x高'}
    $width=[int]$Matches[1];$height=[int]$Matches[2]
    if($width -lt 640 -or $width -gt 4096 -or $height -lt 360 -or $height -gt 2160){throw '分辨率超出测试安全范围'}
    $log=Join-Path $evidence ($resolution+'.log')
    $previewArguments=@('-CFHUDPreview','-RenderOffscreen','-ForceRes',('-ResX='+$width),('-ResY='+$height),'-nosound','-unattended','-nop4','-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.DefaultFeature.MotionBlur 0,r.AntiAliasingMethod 1"',('-abslog="'+$log+'"'))
    if($Rescue){$previewArguments+='-CFHUDRescuePreview'}
    if($HomeMode){$previewArguments+='-CFHUDHomePreview'}
    if($AvoidCombat){$previewArguments+='-CFHUDStancePreview'}
    if($Editor){$previewArguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'),'-game')+$previewArguments}
    $process=Start-Process -FilePath $exe -WorkingDirectory $working -ArgumentList $previewArguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw "界面预览超时：$log"}
    $contents=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $contents -notmatch ('CF_HUD_PREVIEW PASS scenes='+$scenes.Count+'\b') -or $contents -match 'CF_HUD.*FAIL|Fatal error|Assertion failed'){throw "界面行为检查失败：$log"}
    if($HomeMode -and $contents -notmatch 'CF_HUD_HOME_HINT PASS'){throw '未实际验证房屋模式的完整操作提示，请使用包含该功能的构建'}
    if($AvoidCombat -and $contents -notmatch 'CF_HUD_STANCE PASS avoid=1'){throw '缺少F8实际避战状态标记'}
    if($Orientation){
        if(!$contents.Contains('CF_ORIENTATION PASS headings=40 relative=16 boundary=6 height=3')){throw '缺少朝向与基地相对方向计算证据'}
        foreach($scene in $scenes){
            $lines=if($scene -in @('Roster','RosterDanger','HelpDanger')){0}else{2}
            if(!$contents.Contains(('CF_ORIENTATION_FRAME PASS scene='+$scene+' compassLines='+$lines+' interface='))){throw ('方位面板实际行数不符：'+$scene)}
        }
    }
    foreach($scene in $scenes){if($contents -notmatch ('CF_HUD_FRAME PASS scene='+$scene+' viewport='+$resolution+' ')){throw "缺少实际 $resolution 渲染帧：$scene"}}
    $rootMatch=[regex]::Match($contents,'CF_HUD_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})')
    if(!$rootMatch.Success){throw '未记录本次截图目录'}
    $savedRoot=if($Editor){Join-Path $projectRoot 'Saved'}else{Join-Path $working 'CreatureFrontier\Saved'}
    $shots=Join-Path $savedRoot ('HUDPreview\'+$rootMatch.Groups[1].Value)
    foreach($scene in $scenes){
        $source=Join-Path $shots ($scene+'.png')
        if(!(Test-Path -LiteralPath $source) -or (Get-Item -LiteralPath $source).Length -lt 1000){throw "截图缺失：$source"}
        $header=New-Object byte[] 24;$stream=[IO.File]::OpenRead($source)
        try {$read=$stream.Read($header,0,24)} finally {$stream.Dispose()}
        if($read -ne 24 -or $header[0] -ne 137 -or $header[1] -ne 80){throw 'PNG文件头无效'}
        $pngWidth=[int]$header[16]*16777216+[int]$header[17]*65536+[int]$header[18]*256+[int]$header[19]
        $pngHeight=[int]$header[20]*16777216+[int]$header[21]*65536+[int]$header[22]*256+[int]$header[23]
        if($pngWidth -ne $width -or $pngHeight -ne $height){throw "PNG尺寸不符：$source"}
        Copy-Item -LiteralPath $source -Destination (Join-Path $evidence ($resolution+'-'+$scene+'.png'))
    }
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $buildHash){throw '界面检查期间程序指纹变化'}
    $results += [pscustomobject]@{Resolution=$resolution;Scenes=$scenes.Count;AvoidCombat=[bool]$AvoidCombat;Orientation=[bool]$Orientation;Editor=[bool]$Editor;Pass=$true;BuildSHA256=$buildHash;Log=$log}
    $results | Export-Csv -LiteralPath (Join-Path $evidence 'Results.csv') -NoTypeInformation
    Write-Host "$resolution : $($scenes.Count)个渲染场景通过，PNG尺寸已验证"
}
Write-Host "界面自动检查结束，仍需逐张目视检查：$evidence"
