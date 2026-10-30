param([Parameter(Mandatory=$true)][string]$ReadInputPath,[string]$BuildRoot='',[switch]$Read,[switch]$Preview,[switch]$StairWork,[switch]$SupplyPause,[switch]$WalkHarvest,[switch]$WalkSupply,
      [ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
if($SupplyPause -and !$StairWork){throw '补料核对须同时指定楼梯伙伴施工模式'}
if($WalkHarvest -and $Read){throw '徒步采料只用于开始施工前的完整流程，不能与中途读取组合'}
if($WalkSupply -and (!$WalkHarvest -or !$StairWork -or !$SupplyPause -or $Read)){throw '徒步开箱供料须同时指定徒步采料、楼梯施工和缺料补给，不能用于中途读取'}
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $projectRoot 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved/UpperDeckConstructionQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$log=Join-Path $run 'UpperDeck.log';$protected=@();$started=$null
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierUpperDeckQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '二层楼板施工专项正在运行'}
try{
    $slotRoot=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slotRoot -Force | Out-Null
    foreach($name in @('UpperDeckInput','UpperDeckMid','UpperDeckStairMid','UpperDeckStairCheck','UpperDeckTop','UpperDeckDone','UpperDeckBlocked')){
        $path=Join-Path $slotRoot ($name+'.sav');if($path -eq $ReadInputPath){throw '输入须为独立副本，不能使用受保护测试槽'}
        $backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path -LiteralPath $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slotRoot 'UpperDeckInput.sav')
    $arguments=@($(if($Read){'-CFUpperDeckRead'}else{'-CFUpperDeckTest'}),'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($StairWork){$arguments+='-CFUpperDeckStairWork'}
    if($SupplyPause){$arguments+='-CFUpperDeckSupplyPause'}
    if($WalkHarvest){$arguments+='-CFUpperDeckWalkHarvest'}
    if($WalkSupply){$arguments+=@('-CFUpperDeckWalkSupply','-CFRenovationTrace')}
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFUpperDeckPreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    $started=[DateTime]::UtcNow
    [pscustomobject]@{Read=[bool]$Read;StairWork=[bool]$StairWork;SupplyPause=[bool]$SupplyPause;WalkHarvest=[bool]$WalkHarvest;WalkSupply=[bool]$WalkSupply;Preview=[bool]$Preview;Resolution=$Resolution;BuildSHA256=$hash;ReadInputSHA256=$inputHash;StartedUTC=$started.ToString('o')}|Export-Csv -LiteralPath (Join-Path $run 'Run.csv') -NoTypeInformation
    Write-Host ('原遮棚二层楼板施工：'+$run)
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit($(if($WalkHarvest){450000}else{330000}))){$process.Kill();$process.WaitForExit();throw '二层楼板施工专项超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('LogExit: Exiting.')){throw ('二层楼板施工专项失败：'+$log)}
    if($WalkSupply){
        foreach($entry in @(@{Trip=1;Amount=8},@{Trip=2;Amount=9},@{Trip=3;Amount=3})){
            if(!$raw.Contains(('CF_UPPER_DECK_STORE_INPUT PASS trip={0} amount={1} sameTask=1 open=1 closed=1' -f $entry.Trip,$entry.Amount)) -or !$raw.Contains(('CF_UPPER_DECK_STORE_RETURN PASS trip={0} ' -f $entry.Trip))){throw '缺少徒步开箱供料／返回守恒标记'}
        }
    }
    if($WalkHarvest -and !$raw.Contains('CF_UPPER_DECK_WALK_HARVEST PASS count=15 material=20')){throw '缺少原输入徒步采料及返家守恒标记'}
    foreach($marker in @('CF_UPPER_DECK_BUILT PASS modules=25 floor=8 stairs=12 material=0','CF_UPPER_DECK_JOIN PASS outside=1','CF_UPPER_DECK_TOP PASS hero=1 pet=1 supportRetained=1 version=38','CF_UPPER_DECK_LIFE PASS version=38 retained=23 modules=25 heroUp=1 petUp=1 returned=1 material=0 seeds=1 food=0 bond=9','CF_UPPER_DECK PASS reason=complete')){
        if(!$raw.Contains($marker)){throw ('缺少二层施工／生活标记：'+$marker)}
    }
    $heldMaterial=if($SupplyPause){3}else{0}
    $stairRestore='CF_UPPER_DECK_STAIR_RESTORE PASS version=38 carried=3 material='+$heldMaterial+' retained=23'
    $stairRead=$Read -and $raw.Contains($stairRestore)
    [array]$required=if($stairRead){@($stairRestore)}elseif($Read){@()}else{@('CF_UPPER_DECK_REFUND PASS roof=5 retained=23','CF_UPPER_DECK_HARVEST count=15 material=20','CF_UPPER_DECK_GUARDS PASS pendingLanding=0 material=12 crate=8 anchorBelow=1','CF_UPPER_DECK_MID PASS version=38 carried=3')}
    if($stairRead){$required+='CF_UPPER_DECK_STAIR_POSITION PASS preserved=1'}
    if($Read -and !$stairRead -and $raw -notmatch 'CF_UPPER_DECK_RESTORE PASS version=(37|38) carried=3 material=12 retained=23'){throw '缺少真实旧版或当前楼板在途恢复标记'}
    if($StairWork){
        if($SupplyPause){$required+='CF_UPPER_DECK_SUPPLY_WAIT PASS noDrain=1 sameTask=1 actualRefill=3'}
        $required+='CF_UPPER_DECK_STAIR_WORK PASS paid=12 carried=0 taskCleared=1'
        $required+='CF_UPPER_DECK_STAIR_PENDING PASS lowerFloorHit=1 upperPath=0 collision=1'
        if(!$stairRead){$required+=@('CF_UPPER_DECK_STAIR_GUARDS PASS supportRetained=1 legacy37Rejected=1 worldRetained=1',('CF_UPPER_DECK_STAIR_MID PASS version=38 carried=3 material='+$(if($SupplyPause){3}else{0})))}
    }
    foreach($marker in $required){if(!$raw.Contains($marker)){throw ('缺少二层施工投入／恢复标记：'+$marker)}}
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash){throw '程序或源输入副本改变'}
    if($Preview){
        if(!$stairRead -and !$raw.Contains('CF_UPPER_DECK_PREVIEW_AIM PASS target=1')){throw '没有实际瞄准下层施工料箱'}
        if($StairWork -and !$raw.Contains('CF_UPPER_DECK_STAIR_PREVIEW_AIM PASS target=1')){throw '没有实际瞄准楼梯施工料箱'}
        $root=[regex]::Match($raw,'CF_UPPER_DECK_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$root.Success){throw '缺少二层施工截图独立目录'}
        $scenes=if($stairRead){@('Stairs','Occupied')}else{@('Pending','FloorDone','Stairs','Occupied')}
        if($StairWork){$scenes+='StairsPending'}
        if($WalkSupply){$scenes+=@('Store1','Store2','Store3')}
        foreach($scene in $scenes){
            $source=Join-Path $saved ('UpperDeckPreview/'+$root.Groups[1].Value+'/'+$scene+'.png')
            if(!(Test-Path -LiteralPath $source) -or (Get-Item -LiteralPath $source).LastWriteTimeUtc -lt $started){throw '二层施工截图缺失或不是本轮生成'}
            $bytes=[IO.File]::ReadAllBytes($source);if($bytes.Length -lt 1000 -or $bytes[0] -ne 137 -or $bytes[1] -ne 80){throw '二层施工截图格式错误'}
            $width=[int]$bytes[16]*16777216+[int]$bytes[17]*65536+[int]$bytes[18]*256+[int]$bytes[19];$height=[int]$bytes[20]*16777216+[int]$bytes[21]*65536+[int]$bytes[22]*256+[int]$bytes[23]
            if(($width.ToString()+'x'+$height) -ne $Resolution){throw '二层施工截图尺寸不符'}
            Copy-Item -LiteralPath $source -Destination (Join-Path $run ($scene+'.png'))
        }
    }
    $midHash=if(!$Read){Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'UpperDeckMid.sav')}else{''}
    $stairMidHash=if($StairWork -and !$stairRead){Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'UpperDeckStairMid.sav')}else{''}
    [pscustomobject]@{Read=[bool]$Read;StairWork=[bool]$StairWork;SupplyPause=[bool]$SupplyPause;WalkHarvest=[bool]$WalkHarvest;WalkSupply=[bool]$WalkSupply;StairRead=[bool]$stairRead;Preview=[bool]$Preview;Resolution=$Resolution;Pass=$true;BuildSHA256=$hash;ReadInputSHA256=$inputHash;MidSaveSHA256=$midHash;StairMidSaveSHA256=$stairMidHash;TopSaveSHA256=(Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'UpperDeckTop.sav'));DoneSaveSHA256=(Get-FrontierBuildFingerprint -LiteralPath (Join-Path $slotRoot 'UpperDeckDone.sav'));Log=$log}|Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '原遮棚由原伙伴实际建成二层楼板、楼梯连接及双方上下楼存读通过；图形仍须逐图目视，固定夹具不作为自然建造或最终品质验收'
}finally{
    try{
        if($started){foreach($name in @('UpperDeckMid','UpperDeckStairMid','UpperDeckTop','UpperDeckDone','UpperDeckBlocked')){$path=Join-Path $slotRoot ($name+'.sav');if((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'-generated.sav'))}}}
        foreach($entry in $protected){if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '二层施工测试原槽恢复失败'}}elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}}
    }finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
