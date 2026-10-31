param([Parameter(Mandatory=$true)][string]$ReadInputPath,[string]$BuildRoot='',[ValidateSet('Test','Read','Rules')][string]$Mode='Test',[switch]$Preview,[ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
if($Preview -and $Mode -eq 'Rules'){throw '逻辑夹具不生成真实取粮图形证据'}
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $root 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $root 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module;$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$run=Join-Path $root ('Saved/FoodPortionQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8));New-Item -ItemType Directory -Path $run|Out-Null
$slots=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slots -Force|Out-Null;$protected=@();$started=$null
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierFoodPortionQA');if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '单份取粮验证正在运行'}
try{
    foreach($name in @('FoodPortionInput','FoodPortionDone')){$path=Join-Path $slots ($name+'.sav');if($path -eq $ReadInputPath){throw '来源必须使用独立副本'};$backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''};if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}}
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slots 'FoodPortionInput.sav')
    $log=Join-Path $run 'Portion.log';$arguments=@(('-CFFoodPortion'+$Mode),'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFFoodPortionPreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    [pscustomobject]@{BuildSHA256=$hash;InputSHA256=$inputHash;Input=$ReadInputPath;Mode=$Mode;Preview=[bool]$Preview;Resolution=$Resolution}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    $started=[DateTime]::UtcNow;Write-Host ('单份取粮证据：'+$run);$p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(90000)){$p.Kill();$p.WaitForExit();throw '单份取粮验证超时'}
    $raw=Get-Content -LiteralPath $log -Raw;$marker=if($Mode -eq 'Rules'){'CF_FOOD_PORTION_RULES PASS bulk=1 deposit=1 full=1 empty=1 wrong=1 farm=1 occluded=1'}else{'CF_FOOD_PORTION PASS bagFood=1 stock=2 conserved=3 saved=1'}
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains($marker) -or !$raw.Contains('LogExit: Exiting.')){throw ('单份取粮验证失败：'+$log)}
    if($Preview){$id=[regex]::Match($raw,'CF_FOOD_PORTION_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$id.Success){throw '缺少截图目录'};$path=Join-Path $saved ('FoodPortionPreview/'+$id.Groups[1].Value+'/Single.png');if(!(Test-Path $path) -or (Get-Item $path).LastWriteTimeUtc -lt $started){throw '缺少本轮新图'};$b=[IO.File]::ReadAllBytes($path);if($b.Length -lt 1000 -or $b[0] -ne 137){throw '截图格式无效'};$w=[int]$b[16]*16777216+[int]$b[17]*65536+[int]$b[18]*256+[int]$b[19];$h=[int]$b[20]*16777216+[int]$b[21]*65536+[int]$b[22]*256+[int]$b[23];if(($w.ToString()+'x'+$h) -ne $Resolution){throw '截图尺寸不符'};Copy-Item -LiteralPath $path -Destination (Join-Path $run 'Single.png')}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash){throw '程序或来源发生变化'}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;Mode=$Mode;Log=$log;VisualReviewRequired=[bool]$Preview;Scope=$(if($Mode -eq 'Rules'){'隔离库存／分类／遮挡夹具'}else{'真实来源单份取粮／存读，非制种或补种整链'})}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '单份取粮本模式验证通过；不是制种或补种完整体验，图形须目视'
}finally{try{foreach($entry in $protected){if($started -and (Test-Path $entry.Path) -and (Get-Item $entry.Path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ((Split-Path $entry.Path -LeafBase)+'-generated.sav'))};if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原测试槽恢复失败'}}elseif(Test-Path $entry.Path){Remove-Item -LiteralPath $entry.Path}}}finally{$mutex.ReleaseMutex();$mutex.Dispose()}}
