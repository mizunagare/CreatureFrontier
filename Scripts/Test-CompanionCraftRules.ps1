param([string]$BuildRoot='',[switch]$Preview,[ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $root 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $root ('Saved/CompanionCraftRulesQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$log=Join-Path $run 'Rules.log'
$argsList=@('-CFCompanionCraftRules','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
$exe=if($BuildRoot){$module}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
if(!$BuildRoot){$argsList=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$argsList}
if($Preview){
    $size=$Resolution.Split('x');$argsList=@($argsList|Where-Object {$_ -ne '-nullrhi'})+@('-CFWorkshopPreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass')
    if(!$BuildRoot){$exe='D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}
}
Write-Host ('伙伴加工规则夹具：'+$run)
$p=Start-Process -FilePath $exe -ArgumentList $argsList -WindowStyle Hidden -PassThru
if(!$p.WaitForExit(60000)){$p.Kill();$p.WaitForExit();throw '伙伴加工规则验证超时'}
$raw=Get-Content -LiteralPath $log -Raw
if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('CF_COMPANION_CRAFT_RULES PASS missing=1 full=1 partial=1 paused=1 rest=1 exclusive=1 finished=1 once=1') -or !$raw.Contains('LogExit: Exiting.')){throw ('伙伴加工规则失败：'+$log)}
if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '规则验证期间程序变化'}
foreach($marker in @('CF_WORKSHOP_INPUT PASS open=1 deposit=1 withdraw=1 close=1','CF_WORKSHOP_STOCK_RULES PASS validation=1 empty=1 dismantleProtected=1','CF_WORKSHOP_LOCAL PASS order=1 missing=1 full=1 partial=1 cancel=1 reorder=1 finish=1 mode=1')){
    if(!$raw.Contains($marker)){throw ('缺少设施库存与本地加工证据：'+$marker)}
}
if(!$raw.Contains('CF_WORKSHOP_CARGO_SNAPSHOT PASS')){throw '缺少设施回运快照互斥校验证据'}
if($Preview){
    if(!$raw.Contains('CF_WORKSHOP_PREVIEW PASS')){throw '制作设施图形检查未通过'}
    $match=[regex]::Match($raw,'CF_WORKSHOP_PREVIEW_ROOT ([^\r\n]+)');if(!$match.Success){throw '缺少本次截图目录'}
    foreach($name in @('Stock','Order','Status')){
        $path=Join-Path $match.Groups[1].Value ($name+'.png');$bytes=[IO.File]::ReadAllBytes($path)
        if($bytes.Length -lt 1000 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80){throw '无效PNG'}
        $width=[int]$bytes[16]*16777216+[int]$bytes[17]*65536+[int]$bytes[18]*256+[int]$bytes[19];$height=[int]$bytes[20]*16777216+[int]$bytes[21]*65536+[int]$bytes[22]*256+[int]$bytes[23]
        if(($width.ToString()+'x'+$height) -ne $Resolution){throw '截图尺寸不符'}
        Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'.png'))
    }
}
[pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log;Preview=[bool]$Preview;Resolution=$Resolution;Scope='独立库存规则夹具，非真实采集或移动；图形须另行目视'}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
Write-Host '缺料／满格等待、暂停／休息、单工人及一次结算规则通过；不作为实际AI移动或工作耗时证据'
Write-Host '设施库存按键存取、拆除保护及新旧加工库存隔离／取消／版本校验通过；不代表实际存读或完整生产运输'
