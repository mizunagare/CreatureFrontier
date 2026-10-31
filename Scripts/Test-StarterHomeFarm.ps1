param([string]$BuildRoot='',[Parameter(Mandatory=$true)][string]$ReadInputPath,[switch]$Resume,[switch]$Preview,[switch]$Reseed,[ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $root 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved'}else{Join-Path $root 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module;$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$prefix=if($Reseed){'StarterHomeSeed'}else{'StarterHomeFarm'}
$run=Join-Path $root ('Saved\'+$prefix+'QA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8));New-Item -ItemType Directory -Path $run|Out-Null
$slots=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slots -Force|Out-Null
$protected=@();$started=$null;$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierStarterHomeFarmQA');if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '家园农圃补给验证正在运行'}
try{
    foreach($suffix in @('Input','Mid','Done','Blocked')){$name=$prefix+$suffix;$path=Join-Path $slots ($name+'.sav');if($path -eq $ReadInputPath){throw '来源必须是独立副本'};$backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''};if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}}
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slots ($prefix+'Input.sav'))
    $log=Join-Path $run 'Farm.log';$flag='-CF'+$prefix+$(if($Resume){'Read'}else{'Test'})
    $arguments=@($flag,'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFStarterHomeFarmPreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'}else{'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    [pscustomobject]@{BuildRoot=$BuildRoot;Resume=[bool]$Resume;Preview=[bool]$Preview;Resolution=$Resolution;BuildSHA256=$hash;Input=$ReadInputPath;InputSHA256=$inputHash}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    $started=[DateTime]::UtcNow;Write-Host ('家园农圃补给证据：'+$run);$p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(210000)){$p.Kill();$p.WaitForExit();throw '家园农圃补给验证超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    $final=if($Reseed){'seeds=2 xp=39'}else{'seeds=0 xp=38'};$xp=if($Reseed){39}else{38}
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('CF_STARTER_HOME_FARM_LIFE PASS modules=7 paid=58 food=2 '+$final+' returned=1 closed=1 identity=1') -or !$raw.Contains('LogExit: Exiting.')){throw ('家园农圃补给验证失败：'+$log)}
    if(!$raw.Contains('CF_STARTER_HOME_FARM_HARVEST PASS seedPaid=1 produced=1 withdrawn=1 food=2 xp='+$xp)){throw '缺少真实生产与取粮记录'}
    if($Reseed){if(!$raw.Contains('CF_STARTER_HOME_SEED_CRAFT PASS fiberPaid=1 foodPaid=1 seeds=3 menuInput=1') -or (!$Resume -and $raw -notmatch 'CF_STARTER_HOME_SEED_MID PASS fiber=1 remaining=\d+ food=2 seeds=0')){throw '缺少实际纤维采集或配方支付'}}elseif(!$Resume -and !$raw.Contains('CF_STARTER_HOME_FARM_MID PASS planted=1 food=1 xp=37')){throw '缺少实际生长中途存档'}
    if($Preview){$id=[regex]::Match($raw,'CF_STARTER_HOME_FARM_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$id.Success){throw '缺少截图目录'};$scenes=@('Harvested','Home');if(!$Resume){$scenes=@('Ready','Seeded','Growing')+$scenes};if($Reseed){$scenes=@('Recipe','Crafted')+$scenes;if(!$Resume){$scenes=@('Fiber')+$scenes}};foreach($name in $scenes){$path=Join-Path $saved ('StarterHomeFarmPreview/'+$id.Groups[1].Value+'/'+$name+'.png');if(!(Test-Path $path) -or (Get-Item $path).LastWriteTimeUtc -lt $started){throw '缺少本轮新图'};$b=[IO.File]::ReadAllBytes($path);$w=[int]$b[16]*16777216+[int]$b[17]*65536+[int]$b[18]*256+[int]$b[19];$h=[int]$b[20]*16777216+[int]$b[21]*65536+[int]$b[22]*256+[int]$b[23];if($b.Length -lt 1000 -or $b[0] -ne 137 -or ($w.ToString()+'x'+$h) -ne $Resolution){throw '截图格式或尺寸不符'};Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'.png'))}}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash){throw '程序或来源变化'}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log;VisualReviewRequired=[bool]$Preview}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '原种子实际消耗、伙伴农圃收获取粮、共同返屋关门及存读通过；图形须目视'
}finally{try{foreach($entry in $protected){if($started -and (Test-Path $entry.Path) -and (Get-Item $entry.Path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ((Split-Path $entry.Path -LeafBase)+'-generated.sav'))};if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原农圃测试槽恢复失败'}}elseif(Test-Path $entry.Path){Remove-Item -LiteralPath $entry.Path}}}finally{$mutex.ReleaseMutex();$mutex.Dispose()}}
