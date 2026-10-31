param([string]$BuildRoot='',[Parameter(Mandatory=$true)][string]$ReadInputPath,[switch]$Resume,[switch]$Preview,[ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $root 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved'}else{Join-Path $root 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module;$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$run=Join-Path $root ('Saved\StarterForgeQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8));New-Item -ItemType Directory -Path $run|Out-Null
$slots=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slots -Force|Out-Null
$protected=@();$started=$null;$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierStarterForgeQA');if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '洞穴废件加工验证正在运行'}
try{
    foreach($name in @('StarterForgeInput','StarterForgeMid','StarterForgeDone','StarterForgeBlocked')){$path=Join-Path $slots ($name+'.sav');if($path -eq $ReadInputPath){throw '来源必须是独立副本'};$backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''};if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}}
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slots 'StarterForgeInput.sav')
    $log=Join-Path $run 'Study.log';$flag=if($Resume){'-CFStarterForgeRead'}else{'-CFStarterForgeTest'}
    $arguments=@($flag,'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFStarterForgePreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'}else{'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    [pscustomobject]@{BuildRoot=$BuildRoot;Resume=[bool]$Resume;Preview=[bool]$Preview;Resolution=$Resolution;BuildSHA256=$hash;Input=$ReadInputPath;InputSHA256=$inputHash}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    $started=[DateTime]::UtcNow;Write-Host ('洞穴废件加工证据：'+$run);$p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(240000)){$p.Kill();$p.WaitForExit();throw '洞穴废件加工验证超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('CF_STARTER_FORGE_LIFE PASS modules=7 paid=58 output=2 crystal=0 scrap=0 food=2 seeds=2 closed=1 identity=1') -or !$raw.Contains('LogExit: Exiting.')){throw ('洞穴废件加工验证失败：'+$log)}
    if(!$raw.Contains('CF_STARTER_FORGE_REPAIR PASS scrapPaid=2 crystalPaid=1 output=2 repeated=0')){throw '缺少原废件加工支付及重复拒绝记录'}
    if(!$Resume -and (!$raw.Contains('CF_STARTER_FORGE_SCRAP PASS sourcePaid=2 actualGather=1') -or !$raw.Contains('CF_STARTER_FORGE_MID PASS scrap=2 crystal=1 safe=1'))){throw '缺少实际废件采集或安全撤离中途记录'}
    if($Preview){$id=[regex]::Match($raw,'CF_STARTER_FORGE_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$id.Success){throw '缺少截图目录'};$scenes=@('Recipe','Repaired','Home');if(!$Resume){$scenes=@('Departure','Prepared','Entrance','Scrap','Retreated')+$scenes};foreach($name in $scenes){$path=Join-Path $saved ('StarterForgePreview/'+$id.Groups[1].Value+'/'+$name+'.png');if(!(Test-Path $path) -or (Get-Item $path).LastWriteTimeUtc -lt $started){throw '缺少本轮新图'};$b=[IO.File]::ReadAllBytes($path);$w=[int]$b[16]*16777216+[int]$b[17]*65536+[int]$b[18]*256+[int]$b[19];$h=[int]$b[20]*16777216+[int]$b[21]*65536+[int]$b[22]*256+[int]$b[23];if($b.Length -lt 1000 -or $b[0] -ne 137 -or ($w.ToString()+'x'+$h) -ne $Resolution){throw '截图格式或尺寸不符'};Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'.png'))}}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash){throw '程序或来源变化'}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log;VisualReviewRequired=[bool]$Preview}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '真实恢复／洞穴采集、撤离加工及返屋存读通过；图形须目视'
}finally{try{foreach($entry in $protected){if($started -and (Test-Path $entry.Path) -and (Get-Item $entry.Path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ((Split-Path $entry.Path -LeafBase)+'-generated.sav'))};if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原研究测试槽恢复失败'}}elseif(Test-Path $entry.Path){Remove-Item -LiteralPath $entry.Path}}}finally{$mutex.ReleaseMutex();$mutex.Dispose()}}
