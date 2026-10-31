param([Parameter(Mandatory=$true)][string]$ReadInputPath,[switch]$Resume,[switch]$Funded,[switch]$ReturnOnly,[switch]$Trace,[string]$BuildRoot='',[switch]$Preview,[ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
if($ReturnOnly){$Resume=$true}
if($Resume -and $Funded){throw '不能同时选择存粮恢复和材料恢复'}
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $root 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $root 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module;$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$run=Join-Path $root ('Saved/StarterPantryQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8));New-Item -ItemType Directory -Path $run|Out-Null
$slots=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slots -Force|Out-Null;$protected=@();$started=$null
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierStarterPantryQA');if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '真实食槽整合验证正在运行'}
try{
    foreach($name in @('StarterPantryInput','StarterPantryMid','StarterPantryDone','StarterPantryBlocked','StarterPantryFunded')){
        $path=Join-Path $slots ($name+'.sav');if($path -eq $ReadInputPath){throw '来源必须使用独立副本'}
        $backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slots 'StarterPantryInput.sav')
    $log=Join-Path $run 'Pantry.log';$flag=if($Resume){'-CFStarterPantryRead'}else{'-CFStarterPantryTest'}
    $arguments=@($flag,'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Funded){$arguments+='-CFStarterPantryFunded'}
    if($ReturnOnly){$arguments+='-CFStarterPantryReturn'}
    if($Trace){$arguments+='-CFFarmApproachTrace'}
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFStarterPantryPreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    [pscustomobject]@{BuildSHA256=$hash;Input=$ReadInputPath;InputSHA256=$inputHash;Resume=[bool]$Resume;Funded=[bool]$Funded;ReturnOnly=[bool]$ReturnOnly;Trace=[bool]$Trace;BuildRoot=$BuildRoot;Preview=[bool]$Preview;Resolution=$Resolution}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    $started=[DateTime]::UtcNow;Write-Host ('真实食槽整合证据：'+$run)
    $p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(240000)){$p.Kill();$p.WaitForExit();throw '真实食槽整合验证超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('CF_STARTER_PANTRY_LIFE PASS paid=12 harvested=12 stock=2 bagFood=0 naturalMeal=1 originalJob=1 saved=1') -or !$raw.Contains('LogExit: Exiting.')){throw ('真实食槽整合失败：'+$log)}
    if(!$Resume -and !$Funded -and !$raw.Contains('CF_STARTER_PANTRY_FUND PASS')){throw '缺少实际采集记录'}
    if(!$Resume -and !$raw.Contains('CF_STARTER_PANTRY_MID PASS')){throw '缺少付费存粮检查点'}
    if($Preview){
        $id=[regex]::Match($raw,'CF_STARTER_PANTRY_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$id.Success){throw '缺少截图目录'}
        $scenes=if($Resume){@('Returned')}else{@('Preview','Stocked','Returned')}
        foreach($name in $scenes){$path=Join-Path $saved ('StarterPantryPreview/'+$id.Groups[1].Value+'/'+$name+'.png');if(!(Test-Path $path) -or (Get-Item $path).LastWriteTimeUtc -lt $started){throw '缺少本轮新图'};$b=[IO.File]::ReadAllBytes($path);if($b.Length -lt 1000 -or $b[0] -ne 137){throw '截图格式无效'};$w=[int]$b[16]*16777216+[int]$b[17]*65536+[int]$b[18]*256+[int]$b[19];$h=[int]$b[20]*16777216+[int]$b[21]*65536+[int]$b[22]*256+[int]$b[23];if(($w.ToString()+'x'+$h) -ne $Resolution){throw '截图尺寸不符'};Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'.png'))}
    }
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash){throw '程序或来源变化'}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log;VisualReviewRequired=[bool]$Preview;Scope='所列构建真实操作与存读，有图形时必须另行目视'}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '真实食槽操作与存读通过，图形和Windows仍需独立验收'
}finally{try{foreach($entry in $protected){
    if($started -and (Test-Path $entry.Path) -and (Get-Item $entry.Path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ((Split-Path $entry.Path -LeafBase)+'-generated.sav'))}
    if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原测试槽恢复失败'}}elseif(Test-Path $entry.Path){Remove-Item -LiteralPath $entry.Path}
}}finally{$mutex.ReleaseMutex();$mutex.Dispose()}}
