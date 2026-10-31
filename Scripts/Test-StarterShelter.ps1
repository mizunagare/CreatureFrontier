param([string]$BuildRoot='',[Parameter(Mandatory=$true)][string]$ReadInputPath,[switch]$Resume,[switch]$Preview,[ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $root 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved'}else{Join-Path $root 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module;$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$run=Join-Path $root ('Saved\StarterShelterQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8));New-Item -ItemType Directory -Path $run|Out-Null
$slots=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slots -Force|Out-Null
$protected=@();$started=$null;$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierStarterShelterQA');if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '初步家园验证已在运行'}
try{
    foreach($name in @('StarterShelterInput','StarterShelterMid','StarterShelterDone','StarterShelterBlocked')){$path=Join-Path $slots ($name+'.sav');if($path -eq $ReadInputPath){throw '来源必须是独立副本'};$backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''};if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}}
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slots 'StarterShelterInput.sav')
    $log=Join-Path $run 'Shelter.log';$flag=if($Resume){'-CFStarterShelterRead'}else{'-CFStarterShelterTest'}
    $arguments=@($flag,'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFStarterShelterPreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'}else{'D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    [pscustomobject]@{BuildRoot=$BuildRoot;Resume=[bool]$Resume;Preview=[bool]$Preview;Resolution=$Resolution;BuildSHA256=$hash;Input=$ReadInputPath;InputSHA256=$inputHash}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    $started=[DateTime]::UtcNow;Write-Host ('初步家园证据：'+$run);$p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(180000)){$p.Kill();$p.WaitForExit();throw '初步家园验证超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('CF_STARTER_SHELTER_LIFE PASS version=38 modules=5 paid=36 stored=0 entered=1 identity=1 collision=3 supportReject=1') -or !$raw.Contains('LogExit: Exiting.')){throw ('初步家园验证失败：'+$log)}
    foreach($part in @('part=0 paid=6','part=1 paid=6','part=2 paid=10')){if(!$raw.Contains('CF_STARTER_SHELTER_PART PASS '+$part)){throw '缺少伙伴实际完工记录'}}
    if(!$Resume -and (!$raw.Contains('CF_STARTER_SHELTER_GATHER PASS input=1 sourcePaid=8 total=36') -or !$raw.Contains('CF_STARTER_SHELTER_MID PASS carried=3 paid=14 total=36'))){throw '缺少实采或携料中途记录'}
    if($Preview){$id=[regex]::Match($raw,'CF_STARTER_SHELTER_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$id.Success){throw '缺少截图目录'};$scenes=@('Wall1','Wall2','Roof','Inside');if(!$Resume){$scenes=@('Gathered','Stocked')+$scenes};foreach($name in $scenes){$path=Join-Path $saved ('StarterShelterPreview/'+$id.Groups[1].Value+'/'+$name+'.png');if(!(Test-Path $path) -or (Get-Item $path).LastWriteTimeUtc -lt $started){throw '缺少本轮新图'};$b=[IO.File]::ReadAllBytes($path);$w=[int]$b[16]*16777216+[int]$b[17]*65536+[int]$b[18]*256+[int]$b[19];$h=[int]$b[20]*16777216+[int]$b[21]*65536+[int]$b[22]*256+[int]$b[23];if($b.Length -lt 1000 -or $b[0] -ne 137 -or ($w.ToString()+'x'+$h) -ne $Resolution){throw '截图格式或尺寸不符'};Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'.png'))}}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash){throw '程序或来源发生变化'}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log;VisualReviewRequired=[bool]$Preview}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '真实补采8材料、原箱供料、伙伴两墙与屋顶施工、双方进入及存读通过；图形仍须目视'
}finally{try{foreach($entry in $protected){if($started -and (Test-Path $entry.Path) -and (Get-Item $entry.Path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ((Split-Path $entry.Path -LeafBase)+'-generated.sav'))};if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原初步家园槽恢复失败'}}elseif(Test-Path $entry.Path){Remove-Item -LiteralPath $entry.Path}}}finally{$mutex.ReleaseMutex();$mutex.Dispose()}}
