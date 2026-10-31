param([string]$BuildRoot='',[Parameter(Mandatory=$true)][string]$ReadInputPath,[switch]$Resume,[switch]$Preview,[ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $root 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved'}else{Join-Path $root 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module;$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$run=Join-Path $root ('Saved\StarterStudyQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8));New-Item -ItemType Directory -Path $run|Out-Null
$slots=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slots -Force|Out-Null
$protected=@();$started=$null;$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierStarterStudyQA');if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '真实采样研究验证正在运行'}
try{
    foreach($name in @('StarterStudyInput','StarterStudyMid','StarterStudyDone','StarterStudyBlocked')){$path=Join-Path $slots ($name+'.sav');if($path -eq $ReadInputPath){throw '来源必须是独立副本'};$backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''};if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}}
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slots 'StarterStudyInput.sav')
    $log=Join-Path $run 'Study.log';$flag=if($Resume){'-CFStarterStudyRead'}else{'-CFStarterStudyTest'}
    $arguments=@($flag,'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFStarterStudyPreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'}else{'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    [pscustomobject]@{BuildRoot=$BuildRoot;Resume=[bool]$Resume;Preview=[bool]$Preview;Resolution=$Resolution;BuildSHA256=$hash;Input=$ReadInputPath;InputSHA256=$inputHash}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    $started=[DateTime]::UtcNow;Write-Host ('真实采样研究证据：'+$run);$p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(210000)){$p.Kill();$p.WaitForExit();throw '真实采样研究验证超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('CF_STARTER_STUDY_LIFE PASS modules=7 paid=58 material=0 crystal=1 study=1 food=2 seeds=2 returned=1 closed=1 identity=1') -or !$raw.Contains('LogExit: Exiting.')){throw ('真实采样研究验证失败：'+$log)}
    if(!$raw.Contains('CF_STARTER_STUDY_RESEARCH PASS materialUsed=2 crystalUsed=1 repeated=0 repairUnlocked=1')){throw '缺少原研究支付或能力许可记录'}
    if(!$Resume -and (!$raw.Contains('CF_STARTER_STUDY_SAMPLE PASS materialPaid=2 crystalPaid=2 sampled=1') -or !$raw.Contains('CF_STARTER_STUDY_MID PASS material=2 crystal=2 sample=1 study=0 safe=1'))){throw '缺少实际采样或撤离中途记录'}
    if($Preview){$id=[regex]::Match($raw,'CF_STARTER_STUDY_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$id.Success){throw '缺少截图目录'};$scenes=@('Research','Studied','Home');if(!$Resume){$scenes=@('Departure','Materials','Sample','Retreated')+$scenes};foreach($name in $scenes){$path=Join-Path $saved ('StarterStudyPreview/'+$id.Groups[1].Value+'/'+$name+'.png');if(!(Test-Path $path) -or (Get-Item $path).LastWriteTimeUtc -lt $started){throw '缺少本轮新图'};$b=[IO.File]::ReadAllBytes($path);$w=[int]$b[16]*16777216+[int]$b[17]*65536+[int]$b[18]*256+[int]$b[19];$h=[int]$b[20]*16777216+[int]$b[21]*65536+[int]$b[22]*256+[int]$b[23];if($b.Length -lt 1000 -or $b[0] -ne 137 -or ($w.ToString()+'x'+$h) -ne $Resolution){throw '截图格式或尺寸不符'};Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'.png'))}}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash){throw '程序或来源变化'}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log;VisualReviewRequired=[bool]$Preview}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '真实材料／水晶采样、撤离研究、原许可解锁及返屋存读通过；图形须目视'
}finally{try{foreach($entry in $protected){if($started -and (Test-Path $entry.Path) -and (Get-Item $entry.Path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ((Split-Path $entry.Path -LeafBase)+'-generated.sav'))};if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原研究测试槽恢复失败'}}elseif(Test-Path $entry.Path){Remove-Item -LiteralPath $entry.Path}}}finally{$mutex.ReleaseMutex();$mutex.Dispose()}}
