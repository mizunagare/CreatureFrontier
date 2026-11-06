param([Parameter(Mandatory=$true)][string]$ReadInputPath,[string]$BuildRoot='',[switch]$Resume,[switch]$Life,[switch]$Inspect,[switch]$RecoverFood,[switch]$Trace,[switch]$ContinueLife,[switch]$CarryLife,[switch]$Preview,[switch]$Invalid,[ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
if($ContinueLife){$Life=$true}
if($CarryLife){$Life=$true}
if($CarryLife -and $ContinueLife){throw '携种恢复与返岗失败恢复不能同时启用'}
if($Preview -and !$Life){throw '图形入口仅用于正常来源流程'}
if($Invalid -and ($Resume -or $Life -or $Inspect -or $RecoverFood)){throw '异常存档拒绝测试必须独立运行'}
if($Life -and $Resume){throw '真实来源整链与夹具恢复模式不能同时启用'}
if($Inspect -and ($Life -or $Resume)){throw '现场只读诊断必须独立运行'}
if($RecoverFood -and ($Life -or $Resume -or $Inspect)){throw '携粮失败现场恢复必须独立运行'}
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $root 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $root 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module;$sourceHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$run=Join-Path $root ('Saved/SeedPersistenceQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$slots=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slots -Force|Out-Null
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierSeedPersistenceQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '携种存读验证正在运行'}
$protected=@();$started=$null
try{
    foreach($name in @('SeedSupplyInput','SeedSupplyCarry','SeedSupplyDone','SeedSupplyBlocked','SeedSupplyLifeCarry','SeedSupplyLifeDone','SeedSupplyLifeBlocked','SeedSupplyBefore','SeedSupplyAfter','SeedSupplyBad')){
        $path=Join-Path $slots ($name+'.sav');if([IO.Path]::GetFullPath($path) -eq $ReadInputPath){throw '来源必须是独立副本'}
        $backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slots 'SeedSupplyInput.sav')
    $log=Join-Path $run 'Persistence.log'
    $scope=if($Life){'正常取粮、实采纤维、玩家原配方制种、存箱和伙伴运输生产；不注入物资'}else{'家园副本存读夹具；首次初始化投入3测试种子，恢复不补发；非真实生产来源'}
    if($Inspect){$scope='读取独立副本后立即记录现场并退出，不保存、不注入、不推进整链'}
    if($RecoverFood){$scope='原失败档携粮2恢复；普通AI送达，不补物、不移动主人或伙伴'}
    if($ContinueLife){$scope='原正常制种补给后的3种返岗失败档恢复；仅观察后段，不重播制种或注入物资'}
    if($CarryLife){$scope='正常制种来源自身携种3档独立恢复，继续实际补种生产送粮，不补物'}
    if($Invalid){$scope='正值携种来源，8类异常快照实际写盘后事务拒绝，完整世界快照不变'}
    [pscustomobject]@{BuildSHA256=$hash;SourceSHA256=$sourceHash;Source=$ReadInputPath;BuildRoot=$BuildRoot;Resume=[bool]$Resume;Life=[bool]$Life;CarryLife=[bool]$CarryLife;ContinueLife=[bool]$ContinueLife;Inspect=[bool]$Inspect;RecoverFood=[bool]$RecoverFood;Trace=[bool]$Trace;Preview=[bool]$Preview;Resolution=$Resolution;Scope=$scope}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    $arguments=@('-CFSeedSupplyPersistence','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Resume){$arguments+='-CFSeedSupplyRead'}
    if($Invalid){$arguments+='-CFSeedSupplyInvalid'}
    if($Inspect){$arguments+='-CFSeedSupplyInspect'}
    if($RecoverFood){$arguments+=@('-CFSeedFoodRecovery','-CFHollowDetourTrace','-CFFarmApproachTrace')}
    if($Trace){$arguments+=@('-CFHollowDetourTrace','-CFFarmApproachTrace')}
    if($Life){$arguments=@($arguments|Where-Object {$_ -ne '-CFSeedSupplyPersistence'})+'-CFSeedSupplyLife'}
    if($ContinueLife){$arguments+='-CFSeedLifeContinue'}
    if($CarryLife){$arguments+='-CFSeedLifeCarry'}
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFSeedLifePreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if($Preview -and !$BuildRoot){$exe='D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    $started=[DateTime]::UtcNow;Write-Host ('携种存读证据：'+$run)
    $p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit($(if($Life){220000}else{110000}))){$p.Kill();$p.WaitForExit();throw '携种存读验证超时'}
    $raw=Get-Content -LiteralPath $log -Raw;$mode=if($Resume){1}else{0}
    $marker=if($Life){'CF_SEED_LIFE PASS reason=seeded-harvested-delivered'}else{'CF_SEED_PERSIST PASS read='+$mode}
    if($Inspect){$marker='CF_SEED_INSPECT PASS loaded=1 noSave=1'}
    if($RecoverFood){$marker='CF_SEED_FOOD_RECOVERY PASS'}
    if($Invalid){$marker='CF_SEED_INVALID PASS rejected=8 worldUnchanged=1'}
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains($marker) -or !$raw.Contains('LogExit: Exiting.')){throw ('携种验证未通过：'+$log)}
    $done=Join-Path $slots $(if($Life){'SeedSupplyLifeDone.sav'}elseif($Resume -or $RecoverFood){'SeedSupplyDone.sav'}else{'SeedSupplyCarry.sav'})
    if(!$Inspect -and !$Invalid -and (!(Test-Path $done) -or (Get-Item $done).LastWriteTimeUtc -lt $started)){throw '缺少本次新输出档'}
    if($Life -and !$raw.Contains('CF_SEED_LIFE_LEDGER PASS')){throw '缺少实际食物守恒账'}
    if($Preview){$id=[regex]::Match($raw,'CF_SEED_LIFE_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$id.Success){throw '缺少截图目录'};foreach($name in @('Carrying','Delivered')){
        $path=Join-Path $saved ('SeedLifePreview/'+$id.Groups[1].Value+'/'+$name+'.png');if(!(Test-Path $path) -or (Get-Item $path).LastWriteTimeUtc -lt $started){throw '缺少本次截图'}
        $bytes=[IO.File]::ReadAllBytes($path);if($bytes.Length -lt 1000 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80 -or $bytes[2] -ne 78 -or $bytes[3] -ne 71){throw '截图不是有效PNG'}
        $width=[int]$bytes[16]*16777216+[int]$bytes[17]*65536+[int]$bytes[18]*256+[int]$bytes[19];$height=[int]$bytes[20]*16777216+[int]$bytes[21]*65536+[int]$bytes[22]*256+[int]$bytes[23]
        if(($width.ToString()+'x'+$height) -ne $Resolution){throw '截图尺寸与本次要求不一致'}
        Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'.png'))}}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $sourceHash){throw '程序或来源指纹变化'}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;SourceSHA256=$sourceHash;Scope=$scope;Resume=[bool]$Resume;CarryLife=[bool]$CarryLife;Log=$log;VisualReviewRequired=[bool]$Preview}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host ($scope+'：限定验证通过')
}finally{try{foreach($entry in $protected){
    if($started -and (Test-Path $entry.Path) -and (Get-Item $entry.Path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ((Split-Path $entry.Path -LeafBase)+'-generated.sav'))}
    if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原测试槽恢复失败'}}elseif(Test-Path $entry.Path){Remove-Item -LiteralPath $entry.Path}
}}finally{$mutex.ReleaseMutex();$mutex.Dispose()}}
