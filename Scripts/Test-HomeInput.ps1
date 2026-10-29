param([string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8',[switch]$Room,[switch]$Work,[ValidateSet('Test','Write','Read')][string]$Mode='Test',[switch]$Fence,[switch]$FenceNavigation,[switch]$FencePreview,[switch]$Courtyard,[switch]$Gathered,[switch]$Carried,[switch]$Befriended,[switch]$Walking,[switch]$Living)
$ErrorActionPreference='Stop'
if($Living){$Walking=$true}
if($Walking){$Befriended=$true}
if($Befriended){$Carried=$true}
if($Carried){$Gathered=$true}
if($Gathered){if($Mode -ne 'Test'){throw '真实采集投入仅支持完整普通链'};$Courtyard=$true}
if($Courtyard){if($Fence -or $FenceNavigation -or $FencePreview){throw '组合庭院不能与孤立栅栏探针混用'};$Work=$true}
if($FencePreview){if($Mode -ne 'Test' -or $FenceNavigation){throw '栅栏预览仅用于普通基础模式'};$Fence=$true}
if($FenceNavigation){if($Mode -ne 'Test'){throw '栅栏导航仅支持普通模式'};$Fence=$true}
if($Fence -and ($Room -or $Work)){throw '栅栏专项不能与房屋工作组合'}
if($Mode -ne 'Test' -and !$Work -and !$Fence){throw '存读模式仅用于房屋工作或栅栏链'}
if($Work){$Room=$true}
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$projectRoot=Split-Path $PSScriptRoot -Parent
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved\HomeInputQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$log=Join-Path $run 'HomeInput.log'
Write-Host "房屋预览输入绑定验证：$run"
# 旧程序可能忽略短模式参数而运行完整房屋测试；保护其全部模式零测试槽。
$saveRoot=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved\SaveGames'}else{Join-Path $projectRoot 'Saved\SaveGames'}
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierHomeInputQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '房屋输入测试正在运行'}
$protected=@()
try {
$slots=@('FrontierHomeAutomation','FrontierHomeInvalid','FrontierHomeLegacy');if($Gathered){$slots+='HomeGatheredAutomation'}
if($Living){$slots+='HomeLifeAutomation'}
foreach($name in $slots){
    $path=Join-Path $saveRoot ($name+'.sav');$existed=Test-Path -LiteralPath $path
    $backup=Join-Path $run ($name+'-before.sav');$beforeHash=''
    if($existed){$beforeHash=(Get-FileHash -LiteralPath $path).Hash;Copy-Item -LiteralPath $path -Destination $backup}
    $protected+= [pscustomobject]@{Path=$path;Existed=$existed;Backup=$backup;Hash=$beforeHash}
}
$arguments=@('-CFHomeTest','-CFHomeInputOnly','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
if($Room){$arguments+='-CFHomeRoomInput'}
if($Fence){$arguments+='-CFHomeFenceTest'}
if($FenceNavigation){$arguments+='-CFHomeFenceNavigation'}
if($Work){$arguments+=@('-CFHomeRoomWork','-CFFurnishedFarm')}
if($Mode -eq 'Write'){$arguments+='-CFHomeRoomWorkWrite'}
if($Mode -eq 'Read'){$arguments=@('-CFDoorFarmRead','-CFFurnishedFarm','-CFHomeRoomWorkRead','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))}
if($Fence -and $Mode -ne 'Test'){$arguments=@('-CFHomeTest',('-CFHomeFence'+$Mode),'-CFHomeFenceTest','-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))}
if($Courtyard){$arguments+='-CFHomeCourtyardWork'}
if($Gathered){$arguments+='-CFHomeGatheredWork'}
if($Carried){$arguments+='-CFHomeCarriedWork'}
if($Befriended){$arguments+='-CFHomeBefriendedWork'}
if($Walking){$arguments+='-CFHomeWalkingWork'}
if($Living){$arguments+='-CFHomeLivingWork'}
if($FencePreview){$arguments=@($arguments | Where-Object {$_ -ne '-nullrhi'});$arguments+=@('-CFHomeFencePreview','-RenderOffscreen','-ForceRes','-ResX=1280','-ResY=720','-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60"')}
$exe=$module
if(!$BuildRoot){$exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe';$arguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'),'-game')+$arguments}
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
$timeout=if($Living){600000}elseif($Walking){480000}elseif($Befriended){300000}elseif($Carried){240000}elseif($Gathered){180000}else{60000}
if(!$process.WaitForExit($timeout)){$process.Kill();$process.WaitForExit();throw '房屋输入绑定专项超时'}
if($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $log)){throw '房屋输入绑定专项未正常退出'}
$body=Get-Content -LiteralPath $log -Raw
$completion=if($Courtyard -and $Mode -eq 'Read'){'CF_HOME_COURTYARD_RESTORE PASS identity=1 inventory=1 task=1'}elseif($Courtyard){'CF_HOME_ROOM_INPUT PASS modules=14 material=96 noSave=1'}elseif($FenceNavigation){'CF_HOME_FENCE_NAV PASS phase=4 entered=1 exited=1 continuous=1 noCross=1'}elseif($Fence -and $Mode -eq 'Write'){'CF_HOME_FENCE_WRITE PASS modules=4 material=180'}elseif($Fence -and $Mode -eq 'Read'){'CF_HOME_FENCE_READ PASS identity=1 badVersion=1 material=180'}elseif($Fence){'CF_HOME_FENCE PASS modules=3 material=182 collision=1 removed=1 floorGuard=1 noSave=1'}elseif($Mode -eq 'Read'){'CF_FURNISHED_RESTORE PASS identity=1 inventory=1'}elseif($Room){'CF_HOME_ROOM_INPUT PASS modules=11 material=112 noSave=1'}else{'CF_HOME_INPUT_ONLY PASS noSave=1'}
if($Gathered){$completion='CF_HOME_ROOM_INPUT PASS modules=14 material=21 noSave=1'}
if($FenceNavigation -and !$body.Contains('CF_HOME_FENCE_NAV_OPEN PASS closedWait=1 refund=2')){throw '缺少封闭等待与正常拆除返还标记'}
$initialAfterFloor=if($Carried){103}elseif($Gathered){117}else{192}
if($body -match '\bFAIL\b|Fatal error|Assertion failed' -or !$body.Contains($completion) -or ($Mode -ne 'Read' -and !$body.Contains('CF_HOME_INPUT_BINDINGS PASS floor=1 rotated=1 cancelled=1 material='+$initialAfterFloor))){throw "房屋输入绑定专项未通过或构建不支持：$log"}
if($Gathered){$funding=if($Befriended){'CF_HOME_BEFRIEND_FUNDING PASS material=103 prepaid=22 raw=103 foodCost=2 sharedFood=3 seeds=3 food=0'}elseif($Carried){'CF_HOME_CARGO_FUNDING PASS material=103 prepaid=22 raw=101 carried=3 seeds=3'}else{'CF_HOME_FUNDING PASS material=125 seeds=3 food=2 raw=101 fiber=13 weave=6'};foreach($marker in @($funding,'CF_HOME_GATHERED_SAVE PASS modules=14 material=0 seeds=2 stored=3 task=1 source=1')){if(!$body.Contains($marker)){throw '采集投入、加工或建成存档标记缺失'}};if(([regex]::Matches($body,'CF_HOME_FUND_SOURCE PASS')).Count -ne 6){throw '缺少六处实际资源采集记录'}}
if($Carried){
    foreach($marker in @('CF_HOME_CARGO_CAPITAL PASS modules=3 cost=22 material=82','CF_HOME_CARGO_DELIVER PASS source=0 cargo=0 stored=3 material=82','CF_HOME_CARGO_WITHDRAW PASS material=85 stored=0 parked=1','CF_HOME_CARGO_REUSE PASS capital=3','CF_HOME_CARGO_FARM PASS','CF_HOME_CARGO_SAVED PASS')){if(!$body.Contains($marker)){throw '伙伴材料运输、取用或原伙伴复用标记缺失'}}
    $petIds=@(foreach($stage in @('DELIVER','WITHDRAW','REUSE','FARM','SAVED')){
        $found=[regex]::Match($body,('CF_HOME_CARGO_'+$stage+' PASS[^\r\n]* pet=([A-F0-9]{32})'))
        if(!$found.Success){throw '伙伴材料链缺少有效个体身份记录'}
        $found.Groups[1].Value
    })
    if(@($petIds | Select-Object -Unique).Count -ne 1){throw '材料运输与农圃工作的伙伴身份不一致'}
    if($Befriended){
        foreach($marker in @('CF_HOME_BEFRIEND_FOOD PASS material=4 food=4 cost=2','CF_HOME_BEFRIEND_JOIN PASS material=4 food=1 capture=5 bond=3','CF_HOME_FUND_SOURCE PASS id=6 taken=7 remaining=11','CF_HOME_BEFRIEND_ESCORT PASS')){if(!$body.Contains($marker)){throw '真实结交、实际接近、同行或食物预算标记缺失'}}
        foreach($stage in @('ORIGIN','JOIN','ESCORT','SAVED')){
            $found=[regex]::Match($body,('CF_HOME_BEFRIEND_'+$stage+' PASS[^\r\n]* pet=([A-F0-9]{32})'))
            if(!$found.Success -or $found.Groups[1].Value -ne $petIds[0]){throw '地图原生个体与结交、运输、存读身份不一致'}
        }
        if(!$body.Contains('food=0 bond=3')){throw '结交后最终食物与亲密状态核对缺失'}
        if($Walking){
            $sources=[regex]::Matches($body,'CF_HOME_WALK_SOURCE PASS id=(\d+)')
            if($sources.Count -ne 6 -or (@($sources | ForEach-Object {$_.Groups[1].Value}) -join ',') -ne '1,2,3,4,6,8'){throw '实际步行采集缺少六处来源到达记录'}
            $walk=[regex]::Match($body,'CF_HOME_WALK_ROUTE PASS sources=6 returned=1 continuous=1 travel=([\d.]+) petTravel=[\d.]+ material=104 fiber=13 food=1 pet=([A-F0-9]{32})')
            if(!$walk.Success -or [double]::Parse($walk.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture) -lt 7500 -or $walk.Groups[2].Value -ne $petIds[0]){throw '实际资源往返未完成或伙伴身份不一致'}
        }
    }
}
if($Living){
    foreach($marker in @('CF_HOME_LIFE_WITHDRAW PASS entered=1 food=1 stored=0','CF_HOME_LIFE_EMPTY PASS food=1 stored=0','CF_HOME_LIFE_FEED PASS food=0 stored=0 bond=6',
        'CF_HOME_LIFE_FEED_REJECT PASS food=0 bond=6','CF_HOME_LIFE_REJOIN PASS entered=1 exited=1 petExited=1 continuous=1',
        'CF_HOME_LIFE_SAVE PASS modules=14 stored=3 farmFood=0 food=0 seeds=2 bond=6 follow=1','CF_HOME_LIFE PASS phase=2 reason=complete')){
        if(!$body.Contains($marker)){throw '缺少实际家园生活、拒绝路径、归队或生活状态存读记录'}
    }
    foreach($stage in @('WITHDRAW','FEED','REJOIN','SAVE')){
        $found=[regex]::Match($body,('CF_HOME_LIFE_'+$stage+' PASS[^\r\n]* pet=([A-F0-9]{32})'))
        if(!$found.Success -or $found.Groups[1].Value -ne $petIds[0]){throw '进屋生活与原结交运输伙伴身份不一致'}
    }
    $rejoin=[regex]::Match($body,'CF_HOME_LIFE_REJOIN PASS[^\r\n]* heroTravel=([\d.]+) petTravel=([\d.]+)')
    if(!$rejoin.Success -or [double]::Parse($rejoin.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture) -lt 1400 -or
        [double]::Parse($rejoin.Groups[2].Value,[Globalization.CultureInfo]::InvariantCulture) -lt 600){throw '家园生活往返实际位移不足'}
}
if($Room -and $Mode -ne 'Read' -and ([regex]::Matches($body,'CF_HOME_ROOM_INPUT_PART PASS')).Count -ne $(if($Carried){14}elseif($Courtyard){13}else{10})){throw '整屋输入缺少预期新增模块的实际成功记录'}
if($Courtyard){$marker=if($Mode -eq 'Write'){'CF_HOME_COURTYARD_WRITE PASS modules=14 material=75 closed=1'}else{'CF_HOME_COURTYARD_WORK PASS courtyard=1 doorway=1 noCross=1'};if(!$body.Contains($marker)){throw '缺少指定庭院工作／写档完成标记'}}
if($Work){$number=if($Mode -eq 'Write'){1}elseif($Mode -eq 'Read'){2}else{0};if(!$body.Contains('CF_DOOR_FARM_MODE PASS mode='+$number)){throw '未完成指定的工作存读模式'}}
if($Work){foreach($marker in @('CF_HOME_ROOM_WORK PASS noRebuild=1','CF_DOOR_FARM_CLOSED PASS','CF_FURNISHED_FARM PASS','CF_DOOR_FARM PASS')){if(!$body.Contains($marker)){throw "建房后伙伴工作链缺少必要标记：$marker"}}}
if($Work -and $Mode -ne 'Read'){foreach($marker in @('CF_HOME_FARM_INPUT PASS','CF_HOME_FARM_SEED_INPUT PASS')){if(!$body.Contains($marker)){throw "农圃预览放置或种子输入未实际通过：$marker"}}}
if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '房屋输入测试期间模块发生变化'}
if($FencePreview){
    if(!$body.Contains('CF_HOME_FENCE_PREVIEW PASS')){throw '栅栏预览未完成'}
    $match=[regex]::Match($body,'CF_HOME_FENCE_PREVIEW_ROOT [^\r\n]*?([A-F0-9]{32})\s*\r?\n')
    if(!$match.Success){throw '缺少栅栏独立图片目录'}
    $savedRoot=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved'}else{Join-Path $projectRoot 'Saved'}
    $source=Join-Path $savedRoot ('FencePreview\'+$match.Groups[1].Value+'\Courtyard.png')
    $png=[IO.File]::ReadAllBytes($source)
    if($png.Length -lt 24 -or $png[0] -ne 137 -or $png[1] -ne 80){throw '栅栏图片不是有效PNG'}
    $width=$png[16]*16777216+$png[17]*65536+$png[18]*256+$png[19];$height=$png[20]*16777216+$png[21]*65536+$png[22]*256+$png[23]
    if($width -ne 1280 -or $height -ne 720){throw '栅栏图片尺寸不符'}
    Copy-Item -LiteralPath $source -Destination (Join-Path $run 'Courtyard.png')
}
$saveHash=''
if($Gathered){$generated=Join-Path $saveRoot 'HomeGatheredAutomation.sav';$saveHash=(Get-FileHash -LiteralPath $generated).Hash;Copy-Item -LiteralPath $generated -Destination (Join-Path $run 'HomeGathered-generated.sav')}
$lifeSaveHash=''
if($Living){$generated=Join-Path $saveRoot 'HomeLifeAutomation.sav';$lifeSaveHash=(Get-FileHash -LiteralPath $generated).Hash;Copy-Item -LiteralPath $generated -Destination (Join-Path $run 'HomeLife-generated.sav')}
[pscustomobject]@{Room=[bool]$Room;Work=[bool]$Work;Fence=[bool]$Fence;FenceNavigation=[bool]$FenceNavigation;FencePreview=[bool]$FencePreview;Courtyard=[bool]$Courtyard;Gathered=[bool]$Gathered;Carried=[bool]$Carried;Befriended=[bool]$Befriended;Walking=[bool]$Walking;Living=[bool]$Living;Mode=$Mode;Pass=$true;BuildSHA256=$hash;GeneratedSaveSHA256=$saveHash;LifeSaveSHA256=$lifeSaveHash;Log=$log} | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
if($Living){Write-Host '原结交伙伴采集建设后，玩家实际进屋取粮、原伙伴进食归队并经门离开及生活状态存读通过；建造仍固定站位，未验收完整自然风险或硬件操作'}
elseif($Walking){Write-Host '原生结交后六处资源实际步行采集返家、同一伙伴运输建设与农圃存读通过；建造仍固定观察站位，未验收完整自然风险或硬件操作'}
elseif($Befriended){Write-Host '实际走近地图原生个体、支付食物结交、同一伙伴运输建设与农圃存读通过；后续手采建造仍固定站位，不是完整自然路线'}
elseif($Carried){Write-Host '伙伴实采携带入箱、玩家取用建设与原伙伴继续农圃工作及建成存读通过；仍为固定站位和初始伙伴夹具'}
elseif($Gathered){Write-Host '实际采集加工投入整屋、伙伴工作与建成存读通过；站位及伙伴仍为夹具，不是自然探索路线验收'}
elseif($Courtyard){Write-Host ('组合庭院'+$Mode+'模式通过；完整冷启动链需由外层脚本汇总，不代表自然采集或硬件输入验收')}
elseif($FenceNavigation){Write-Host '伙伴绕入绕出、封闭等待及拆除后恢复进入通过；不代表多伙伴或长期导航验收'}
elseif($Fence -and $Mode -eq 'Write'){Write-Host '栅栏预览建造与保存通过；尚需独立读取'}
elseif($Fence -and $Mode -eq 'Read'){Write-Host '栅栏身份、支撑、成本与物资恢复及伪旧版本拒绝通过'}
elseif($Fence){Write-Host '栅栏预览放置、旋转、阻挡、拆除返还和非承重规则通过；存读与伙伴导航尚待'}
elseif($Mode -eq 'Write'){Write-Host '输入建房并保存闭门等待任务通过；仍需独立进程读取后继续工作'}
elseif($Mode -eq 'Read'){Write-Host '输入建房存档的身份、物资及原任务恢复通过；完整链结果由存档脚本汇总'}
elseif($Work){Write-Host '输入建房与原房伙伴闭门等待、开门工作及物资核对通过；未验收系统鼠标或自然移动建房'}
elseif($Room){Write-Host '双格门窗房屋及储物箱共11模块预览输入通过；不是系统鼠标或自然移动建房验收'}
else{Write-Host '地板预览放置、墙预览旋转与取消绑定通过；不是系统鼠标或整屋人工建造验收'}
} finally {
    try {foreach($entry in $protected){
        if($entry.Existed){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.Hash){throw '房屋输入测试原槽恢复失败'}}
        elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}
    }} finally {$mutex.ReleaseMutex();$mutex.Dispose()}
}
