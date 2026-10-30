param([string]$BuildRoot='',[switch]$Preview,[ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $projectRoot 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved/FarmJourneyQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$protected=@();$started=[DateTime]::UtcNow;$log=Join-Path $run 'FarmJourney.log'
try{
    $slots=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slots -Force|Out-Null
    foreach($name in @('FrontierObjectiveClean','FrontierObjectiveReady','FrontierObjectiveAutomation','FrontierObjectiveInvalid','FrontierObjectiveLegacy','FrontierObjectiveOwnedAlpha','FrontierFarmJourneyPreview')){
        $path=Join-Path $slots ($name+'.sav');$backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    $arguments=@('-CFObjectiveTest','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFFarmJourneyPreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    Write-Host ('农业旅程引导：'+$run)
    $p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(90000)){$p.Kill();$p.WaitForExit();throw '农业旅程测试超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('CF_OBJECTIVE PASS') -or !$raw.Contains('CF_OBJECTIVE_GUIDANCE PASS cases=5 readonly=1')){throw ('农业旅程验证失败：'+$log)}
    if($Preview){
        if(!$raw.Contains('CF_FARM_JOURNEY_PREVIEW PASS scenes=5')){throw '缺少五状态预览标记'}
        $root=[regex]::Match($raw,'CF_FARM_JOURNEY_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$root.Success){throw '缺少独立截图目录'}
        foreach($name in @('MissingSeeds','BackpackSeeds','Ready','Full','Working')){
            $path=Join-Path $saved ('FarmJourneyPreview/'+$root.Groups[1].Value+'/'+$name+'.png')
            if(!(Test-Path $path) -or (Get-Item $path).LastWriteTimeUtc -lt $started){throw '缺少本轮新图'}
            $bytes=[IO.File]::ReadAllBytes($path);if($bytes.Length -lt 1000 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80){throw '截图格式错误'}
            $w=[int]$bytes[16]*16777216+[int]$bytes[17]*65536+[int]$bytes[18]*256+[int]$bytes[19];$h=[int]$bytes[20]*16777216+[int]$bytes[21]*65536+[int]$bytes[22]*256+[int]$bytes[23]
            if(($w.ToString()+'x'+$h) -ne $Resolution){throw '截图尺寸错误'};Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'.png'))
        }
    }
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '程序指纹改变'}
    [pscustomobject]@{BuildRoot=$BuildRoot;Preview=[bool]$Preview;Resolution=$Resolution;Pass=$true;BuildSHA256=$hash;Log=$log}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '农业五状态引导与原旅程推进、拒绝、产出、存读及迁移通过；预览图仍须目视'
}finally{
    foreach($entry in $protected){if((Test-Path $entry.Path) -and (Get-Item $entry.Path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ((Split-Path $entry.Path -LeafBase)+'-generated.sav'))}}
    foreach($entry in $protected){if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原旅程测试槽恢复失败'}}elseif(Test-Path $entry.Path){Remove-Item -LiteralPath $entry.Path}}
}
