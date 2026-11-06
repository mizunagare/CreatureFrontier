param([Parameter(Mandatory=$true)][string]$ReadInputPath,[string]$BuildRoot='',[switch]$Resume,[switch]$Recovery,[switch]$Delivery,[switch]$DeliveryResume,[switch]$Preview,[ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
if($DeliveryResume){$Delivery=$true;$Resume=$true}
if($Preview -and (!$Delivery -or $DeliveryResume)){throw '图形验证需要完整建站送粮阶段'}
if($Recovery -and !$Resume){throw '付费治疗撤离仅适用于真实失败档恢复'}
if($Delivery -and (!$Resume -or $Recovery)){throw '运输阶段需独立返家来源与Resume，不能混用治疗'}
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $root 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $root 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$sourceHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$run=Join-Path $root ('Saved/HomeSupplyQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$slots=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slots -Force|Out-Null
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierHomeSupplyQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '真实备料验证正在运行'}
$protected=@();$started=$null
try{
    foreach($name in @('HomeSupplyInput','HomeSupplyReady','HomeSupplyBlocked','HomeDeliveryMid','HomeDeliveryDone','HomeDeliveryBlocked')){
        $path=Join-Path $slots ($name+'.sav');if([IO.Path]::GetFullPath($path) -eq $ReadInputPath){throw '来源必须是独立副本'}
        $backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path -LiteralPath $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup}
        $protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slots 'HomeSupplyInput.sav')
    $log=Join-Path $run 'Supply.log'
    $scope=if($Resume){'真实16材料失败档保留伤势返家；不证明重新采集或原无伤完整链'}else{'真实采16材料返家与持续农业；非建站或运输验收'}
    if($Recovery){$scope='真实失败档消耗现有药品治疗、南侧撤离返家；不证明原无伤完整链'}
    if($Delivery){$scope='真实返家16材料付费建运输站、原伙伴取粮送屋内食槽；非完整采集链'}
    if($DeliveryResume){$scope='独立进程恢复真实携3粮中途档并完成卸货；不证明重新建站或采集'}
    [pscustomobject]@{BuildSHA256=$hash;SourceSHA256=$sourceHash;Source=$ReadInputPath;BuildRoot=$BuildRoot;Resume=[bool]$Resume;Recovery=[bool]$Recovery;Delivery=[bool]$Delivery;DeliveryResume=[bool]$DeliveryResume;Preview=[bool]$Preview;Resolution=$Resolution;Scope=$scope}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    $flag=if($Resume){'-CFHomeSupplyRead'}else{'-CFHomeSupplyTest'}
    $arguments=@($flag,'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Recovery){$arguments+='-CFHomeSupplyRecovery'}
    if($Delivery){$arguments+='-CFHomeDeliveryTest'}
    if($DeliveryResume){$arguments+='-CFHomeDeliveryRead'}
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFHomeDeliveryPreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if($Preview -and !$BuildRoot){$exe='D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    $started=[DateTime]::UtcNow;Write-Host ('真实备料证据：'+$run)
    $p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(150000)){$p.Kill();$p.WaitForExit();throw '真实备料验证超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    $marker=if($Delivery){'CF_HOME_DELIVERY PASS reason=paid-built-delivered-saved'}else{'CF_HOME_SUPPLY PASS reason=gathered16-returned-saved'}
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains($marker) -or !$raw.Contains('LogExit: Exiting.')){throw ('真实备料未通过：'+$log)}
    $ready=Join-Path $slots $(if($Delivery){'HomeDeliveryDone.sav'}else{'HomeSupplyReady.sav'})
    if($Recovery -and !$raw.Contains('CF_HOME_SUPPLY_RECOVERY PASS')){throw '缺少正常药品消耗治疗证据'}
    if($Preview){$id=[regex]::Match($raw,'CF_HOME_DELIVERY_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$id.Success){throw '缺少截图目录'};foreach($name in @('Preview','Carrying','Delivered')){$path=Join-Path $saved ('HomeDeliveryPreview/'+$id.Groups[1].Value+'/'+$name+'.png');if(!(Test-Path $path) -or (Get-Item $path).LastWriteTimeUtc -lt $started){throw '缺少本轮新图'};Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'.png'))}}
    if(!(Test-Path -LiteralPath $ready) -or (Get-Item -LiteralPath $ready).LastWriteTimeUtc -lt $started){throw '缺少本次新完成档'}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $sourceHash){throw '程序或来源指纹变化'}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log;VisualReviewRequired=[bool]$Preview}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host ($scope+'：通过；仅本次限定行为范围，尚非图形或完整阶段验收')
}finally{
    try{foreach($entry in $protected){
        if($started -and (Test-Path -LiteralPath $entry.Path) -and (Get-Item -LiteralPath $entry.Path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ((Split-Path $entry.Path -LeafBase)+'-generated.sav'))}
        if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原测试槽恢复失败'}}elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}
    }}finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
