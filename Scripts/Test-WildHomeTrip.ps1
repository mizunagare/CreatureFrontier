param([Parameter(Mandatory=$true)][string]$ReadInputPath,[string]$BuildRoot='',[switch]$Read,[switch]$Weave,[switch]$Preview,
      [ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $projectRoot 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module;$sourceHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$run=Join-Path $projectRoot ('Saved/WildHomeTripQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$log=Join-Path $run 'WildHome.log';$protected=@();$started=$null
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierWildHomeQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '野生生态返家专项正在运行'}
try{
    $slotRoot=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slotRoot -Force|Out-Null
    foreach($slot in @('WildHomeInput','WildHomeMid','WildHomeDone','WildHomeBlocked')){
        $path=Join-Path $slotRoot ($slot+'.sav');if($path -eq $ReadInputPath){throw '输入须为独立副本，不能使用测试槽本身'}
        $backup=Join-Path $run ($slot+'-before.sav');$before=if(Test-Path $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slotRoot 'WildHomeInput.sav')
    $arguments=@($(if($Read){'-CFWildHomeRead'}else{'-CFWildHomeTest'}),'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Weave){$arguments+='-CFWildHomeWeave'}
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFWildHomePreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    $started=[DateTime]::UtcNow
    [pscustomobject]@{Read=[bool]$Read;Weave=[bool]$Weave;Preview=[bool]$Preview;BuildRoot=$BuildRoot;BuildSHA256=$hash;InputSHA256=$sourceHash;Resolution=$Resolution}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    Write-Host ('野生生态探索返家：'+$run)
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $timeout=if($Weave){420000}else{180000}
    if(!$process.WaitForExit($timeout)){$process.Kill();$process.WaitForExit();throw '野生生态探索返家超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('LogExit: Exiting.')){throw ('野生生态探索返家失败：'+$log)}
    $markers=@('CF_WILD_HOME_SOURCE PASS positionRetained=1','CF_WILD_HOME_CRAFT PASS material=0 food=2 seeds=1 input=1','CF_WILD_HOME_LIFE PASS version=38 ecology=3 wildActive=1 material=0 food=2 seeds=1 positionRetained=1','CF_WILD_HOME PASS reason=complete')
    $markers+=if($Read){'CF_WILD_HOME_RESTORE PASS material=2 food=1 seeds=1'}else{'CF_WILD_HOME_GATHER PASS input=1 material=2 nodePaid=2 ecology=3'}
    if($Weave){
        $markers=@('CF_WILD_HOME_SOURCE PASS positionRetained=1','CF_WEAVE_CRAFT PASS input=1 material=4 fiber=0 food=2 seeds=1','CF_WILD_HOME_LIFE PASS version=38 ecology=3 wildActive=1 material=4 food=2 seeds=1 positionRetained=1','CF_WILD_HOME PASS reason=complete')
        $markers+=if($Read){'CF_WILD_HOME_RESTORE PASS material=1 food=2 seeds=1'}else{@('CF_WEAVE_GUIDE PASS part=0','CF_WEAVE_GUIDE PASS part=1','CF_WEAVE_GATHER PASS part=0 gained=1 nodePaid=1','CF_WEAVE_GATHER PASS part=1 gained=2 nodePaid=2')}
    }
    foreach($marker in $markers){if(!$raw.Contains($marker)){throw ('缺少生态探索证据：'+$marker)}}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $sourceHash){throw '程序或输入源发生改变'}
    if($Preview){
        $root=[regex]::Match($raw,'CF_WILD_HOME_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$root.Success){throw '没有独立截图目录'}
        $scenes=if($Read){if($Weave){@('Crafting','Home')}else{@('Home')}}elseif($Weave){@('GuideMaterial','GuideFiber','Crafting','Home')}else{@('Meadow','Home')}
        if($Weave -and $raw.Contains('CF_WEAVE_REGEN PASS')){$scenes+=@('Regrowing','Regrown')}
        foreach($name in $scenes){
            $path=Join-Path $saved ('WildHomePreview/'+$root.Groups[1].Value+'/'+$name+'.png')
            if(!(Test-Path $path) -or (Get-Item $path).LastWriteTimeUtc -lt $started){throw '截图缺失或不是本轮生成'}
            $bytes=[IO.File]::ReadAllBytes($path);if($bytes.Length -lt 1000 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80){throw '截图不是有效PNG'}
            $width=[int]$bytes[16]*16777216+[int]$bytes[17]*65536+[int]$bytes[18]*256+[int]$bytes[19];$height=[int]$bytes[20]*16777216+[int]$bytes[21]*65536+[int]$bytes[22]*256+[int]$bytes[23]
            if(($width.ToString()+'x'+$height) -ne $Resolution){throw '截图尺寸不符'}
            Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'.png'))
        }
    }
    [pscustomobject]@{Read=[bool]$Read;Weave=[bool]$Weave;Preview=[bool]$Preview;Pass=$true;BuildSHA256=$hash;InputSHA256=$sourceHash;MidSaveSHA256=$(if($Read){''}else{Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'WildHomeMid.sav')});DoneSaveSHA256=(Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'WildHomeDone.sav'));Log=$log}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    if($Weave){Write-Host '按实时配方线索实际采料、返家编织4份建材及存读通过；自然生态保持运行'}else{Write-Host '野生AI正常运行下探索采料、返家制作与存读通过；不代表全部生态和自由探索验收'}
}finally{
    try{
        if($started){foreach($slot in @('WildHomeMid','WildHomeDone','WildHomeBlocked')){$path=Join-Path $slotRoot ($slot+'.sav');if((Test-Path $path) -and (Get-Item $path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $path -Destination (Join-Path $run ($slot+'-generated.sav'))}}}
        foreach($entry in $protected){if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原生态测试槽恢复失败'}}elseif(Test-Path $entry.Path){Remove-Item -LiteralPath $entry.Path}}
    }finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
