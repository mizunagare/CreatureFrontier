param([Parameter(Mandatory=$true)][string]$ReadInputPath,[string]$BuildRoot='',[switch]$Read,[switch]$Preview,
      [ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $projectRoot 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module;$sourceHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$run=Join-Path $projectRoot ('Saved/HomeRailingQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$log=Join-Path $run 'HomeRailing.log';$protected=@();$started=$null
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierHomeRailingQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '加工建材护栏专项正在运行'}
try{
    $slotRoot=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slotRoot -Force|Out-Null
    foreach($slot in @('HomeRailingInput','HomeRailingMid','HomeRailingDone','HomeRailingBlocked')){
        $path=Join-Path $slotRoot ($slot+'.sav');if($path -eq $ReadInputPath){throw '输入必须使用独立副本'}
        $backup=Join-Path $run ($slot+'-before.sav');$before=if(Test-Path $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slotRoot 'HomeRailingInput.sav')
    $arguments=@($(if($Read){'-CFHomeRailingRead'}else{'-CFHomeRailingTest'}),'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFHomeRailingPreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    $started=[DateTime]::UtcNow
    [pscustomobject]@{Read=[bool]$Read;Preview=[bool]$Preview;BuildRoot=$BuildRoot;BuildSHA256=$hash;InputSHA256=$sourceHash;Resolution=$Resolution}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    Write-Host ('加工材料用于家园护栏：'+$run)
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(300000)){$process.Kill();$process.WaitForExit();throw '加工材料护栏专项超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('LogExit: Exiting.')){throw ('护栏专项失败：'+$log)}
    $markers=@('CF_RAILING_SOURCE PASS','CF_RAILING_BUILT PASS paid=4 modules=26','CF_RAILING_COLLISION PASS','CF_RAILING_LIFE PASS version=38 ecology=3 positionRetained=1 material=0 food=2 seeds=1','CF_HOME_RAILING PASS reason=complete')
    if(!$Read){$markers+=@('CF_RAILING_STORE PASS amount=4 input=1','CF_RAILING_PLAN PASS input=1 material=0 pending=1','CF_RAILING_MID PASS cargo=3 crate=1 site=0')}
    foreach($marker in $markers){if(!$raw.Contains($marker)){throw ('缺少护栏证据：'+$marker)}}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $sourceHash){throw '程序或输入源发生改变'}
    if($Preview){
        $root=[regex]::Match($raw,'CF_RAILING_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$root.Success){throw '没有独立截图目录'}
        $scenes=if($Read){@('Working','Collision','Home')}else{@('Stored','Preview','Planned','Carrying','Working','Collision','Home')}
        foreach($name in $scenes){
            $path=Join-Path $saved ('HomeRailingPreview/'+$root.Groups[1].Value+'/'+$name+'.png')
            if(!(Test-Path $path) -or (Get-Item $path).LastWriteTimeUtc -lt $started){throw '截图缺失或不是本轮生成'}
            $bytes=[IO.File]::ReadAllBytes($path);if($bytes.Length -lt 1000 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80){throw '截图不是有效PNG'}
            $width=[int]$bytes[16]*16777216+[int]$bytes[17]*65536+[int]$bytes[18]*256+[int]$bytes[19];$height=[int]$bytes[20]*16777216+[int]$bytes[21]*65536+[int]$bytes[22]*256+[int]$bytes[23]
            if(($width.ToString()+'x'+$height) -ne $Resolution){throw '截图尺寸不符'}
            Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'.png'))
        }
    }
    [pscustomobject]@{Read=[bool]$Read;Preview=[bool]$Preview;Pass=$true;BuildSHA256=$hash;InputSHA256=$sourceHash;DoneSHA256=(Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'HomeRailingDone.sav'));Log=$log}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '真实材料供箱、伙伴施工、护栏碰撞、上下楼返回及存读通过；图形仍须目视，不代表任意布局验收'
}finally{
    try{
        if($started){foreach($slot in @('HomeRailingMid','HomeRailingDone','HomeRailingBlocked')){$path=Join-Path $slotRoot ($slot+'.sav');if((Test-Path $path) -and (Get-Item $path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $path -Destination (Join-Path $run ($slot+'-generated.sav'))}}}
        foreach($entry in $protected){if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原护栏测试槽恢复失败'}}elseif(Test-Path $entry.Path){Remove-Item -LiteralPath $entry.Path}}
    }finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
