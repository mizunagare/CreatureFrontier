param([string]$BuildRoot='', [switch]$Editor,
      [string[]]$Resolutions=@('1280x720'), [string]$EngineRoot='D:\Epic\UE_5.8',
      [string]$LocalCachePath='',
      [ValidateSet('Backpack','WorkshopAccess','Storage','Journal','Session','Name','Memory')]
      [string[]]$MenuNames=@('Backpack','Storage','Journal','Session','Name','Memory'))
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if(($Editor -and $BuildRoot) -or (!$Editor -and !$BuildRoot)){throw '请只指定编辑器模式或打包目录之一'}
if($LocalCachePath){
    if(!$Editor){throw '临时本地缓存路径仅用于编辑器模式'}
    if(![IO.Path]::IsPathFullyQualified($LocalCachePath)){throw '临时本地缓存必须使用绝对路径'}
    $LocalCachePath=[IO.Path]::GetFullPath($LocalCachePath)
    if(!(Test-Path -LiteralPath $LocalCachePath -PathType Container)){
        New-Item -ItemType Directory -Path $LocalCachePath -ErrorAction Stop | Out-Null
    }
}
if(!$Resolutions.Count -or @($Resolutions | Select-Object -Unique).Count -ne $Resolutions.Count){throw '分辨率不能为空或重复'}
foreach($size in $Resolutions){
    if($size -notmatch '^(\d{3,4})x(\d{3,4})$' -or [int]$Matches[1] -lt 960 -or [int]$Matches[2] -lt 540 -or [int]$Matches[1] -gt 3840 -or [int]$Matches[2] -gt 2160){throw '菜单测试分辨率须在960×540至3840×2160之间'}
}
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$working=if($Editor){$projectRoot}else{(Resolve-Path -LiteralPath $BuildRoot).Path}
$exe=if($Editor){Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'}else{Join-Path $working 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}
$module=if($Editor){Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}else{$exe}
$saved=if($Editor){Join-Path $projectRoot 'Saved'}else{Join-Path $working 'CreatureFrontier\Saved'}
if(!(Test-Path -LiteralPath $exe)){throw '找不到菜单预览程序'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$evidence=Join-Path $projectRoot ('Saved\ChineseMenus\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $evidence | Out-Null
Write-Host "中文菜单证据：$evidence"
$cases=@(
    @{Name='Backpack';Label='背包';Flag='CFBackpackTest';Preview='CFBackpackPreview';Marker='CF_BACKPACK PASS';Files=@('BackpackPreview/backpack.png')},
    @{Name='WorkshopAccess';Label='设施制作通路';Flag='CFBackpackTest';Preview='CFWorkshopAccessPreview';Marker='CF_WORKSHOP_ACCESS_PREVIEW PASS';Files=@('WorkshopAccessPreview/Blocked.png','WorkshopAccessPreview/Ready.png')},
    @{Name='Storage';Label='仓储';Flag='CFStorageTest';Preview='CFStoragePreview';Marker='CF_STORAGE PASS';Files=@('StoragePreview/Storage.png')},
    # 旧图鉴预览在完整断言前提前返回，只检查预览；完整逻辑由本地回归另验。
    @{Name='Journal';Label='图鉴';Flag='CFJournalTest';Preview='CFJournalPreview';Marker='';Files=@('JournalPreview/entry1.png','JournalPreview/entry2.png')},
    @{Name='Session';Label='暂停与帮助';Flag='CFSessionTest';Preview='CFSessionPreview';Marker='CF_SESSION PASS';Files=@('SessionPreview/paused.png','SessionPreview/controls.png','SessionPreview/compact.png')},
    @{Name='Name';Label='伙伴命名';Flag='CFNameTest';Preview='CFNamePreview';Marker='CF_NAME_PREVIEW PASS';Files=@('NamePreview/Dialog.png','NamePreview/Roster.png')},
    @{Name='Memory';Label='归家回忆';Flag='CFMemoryTest';Preview='CFMemoryPreview';Marker='CF_MEMORY_PREVIEW PASS';Files=@('MemoryPreview/Dialog.png','MemoryPreview/Roster.png')}
)
if(!$MenuNames.Count -or @($MenuNames | Select-Object -Unique).Count -ne $MenuNames.Count){throw '菜单列表不能为空或重复'}
$cases=@($cases | Where-Object {$_.Name -in $MenuNames})
$results=@()
$protected=@()
# 仅保护这些场景明确使用的测试槽，常规玩家存档不在写入范围。
$slots=@('FrontierBackpackAutomation','StorageGood','StorageInvalid','StorageLegacy20','StorageCross',
    'FrontierJournalEmptyAutomation','FrontierJournalAutomation','FrontierJournalHistory','FrontierJournalInvalid','FrontierJournalLegacy',
    'FrontierSessionAutomation','NameCross','NameBad','NameDowned','NameLegacy24',
    'MemoryDoneCross','MemoryPendingCross','MemoryBad','MemoryDowned','MemoryLegacy26')
$mutex=New-Object System.Threading.Mutex($false,'Local\CreatureFrontierChineseMenusQA')
$locked=$false
try{
    try{$locked=$mutex.WaitOne(0)}catch [System.Threading.AbandonedMutexException]{$locked=$true}
    if(!$locked){throw '另一个中文菜单测试正在运行'}
    foreach($slot in $slots){
        $path=Join-Path $saved ('SaveGames\'+$slot+'.sav')
        $backup=Join-Path $evidence ($slot+'-before.sav')
        $before=if(Test-Path -LiteralPath $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup}
        $protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
foreach($resolution in $Resolutions){
foreach($case in $cases){
    $resolution -match '^(\d+)x(\d+)$' | Out-Null
    $width=[int]$Matches[1];$height=[int]$Matches[2]
    $started=[DateTime]::UtcNow
    $log=Join-Path $evidence ($resolution+'-'+$case.Name+'.log')
    $previewArguments=@(('-'+$case.Flag),('-'+$case.Preview),'-RenderOffscreen','-ForceRes',('-ResX='+$width),('-ResY='+$height),'-nosound','-unattended','-nop4','-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.DefaultFeature.MotionBlur 0,r.AntiAliasingMethod 1"',('-abslog="'+$log+'"'))
    if($Editor){$previewArguments=@(('"'+(Join-Path $projectRoot 'CreatureFrontier.uproject')+'"'),'-game')+$previewArguments}
    # 只给本次子进程指定文件缓存，不改全局配置，也不迁移或清理原缓存。
    if($LocalCachePath){$previewArguments+=@('-ddc=InstalledNoZenLocalFallback',('-LocalDataCachePath="'+$LocalCachePath+'"'))}
    $process=Start-Process -FilePath $exe -WorkingDirectory $working -ArgumentList $previewArguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw "菜单预览超时：$log"}
    if($process.ExitCode -ne 0 -or ($case.Marker -and !(Select-String -LiteralPath $log -SimpleMatch $case.Marker -Quiet)) -or (Select-String -LiteralPath $log -Pattern 'CF_\w+ FAIL|Fatal error|Assertion failed' -Quiet)){throw "菜单预览未通过：$log"}
    foreach($file in $case.Files){
        $source=Join-Path $saved $file
        if(!(Test-Path -LiteralPath $source)){throw "缺少截图：$source"}
        $info=Get-Item -LiteralPath $source
        if($info.Length -lt 1000 -or $info.LastWriteTimeUtc -lt $started){throw "截图为空或不是本轮生成：$source"}
        $header=New-Object byte[] 24
        $stream=[IO.File]::OpenRead($source)
        try{$read=$stream.Read($header,0,24)}finally{$stream.Dispose()}
        $pngWidth=[int]$header[16]*16777216+[int]$header[17]*65536+[int]$header[18]*256+[int]$header[19]
        $pngHeight=[int]$header[20]*16777216+[int]$header[21]*65536+[int]$header[22]*256+[int]$header[23]
        if($read -ne 24 -or $header[0] -ne 137 -or $header[1] -ne 80 -or $pngWidth -ne $width -or $pngHeight -ne $height){throw "菜单截图格式或尺寸不符：$source"}
        Copy-Item -LiteralPath $source -Destination (Join-Path $evidence ($resolution+'-'+$case.Name+'-'+(Split-Path $file -Leaf)))
    }
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '测试期间程序发生改变'}
    $results += [pscustomobject]@{Case=$case.Name;Resolution=$resolution;Editor=[bool]$Editor;Pass=$true;VisualReviewRequired=$true;BehaviorMarker=$case.Marker;Screens=$case.Files.Count;BuildSHA256=$hash;LocalCachePath=$LocalCachePath;Log=$log}
    $results | Export-Csv -LiteralPath (Join-Path $evidence 'Results.csv') -NoTypeInformation
    Write-Host ($resolution+' '+$case.Label+'：预览及新截图检查通过，仍需逐图查看')
}
}
}finally{
    try{
        foreach($entry in $protected){
            if($entry.Hash){
                Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path
                if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '中文菜单原测试槽恢复校验失败'}
            }elseif(Test-Path -LiteralPath $entry.Path){
                # 只删除本轮新生成的明确测试槽；原有槽已在上一分支恢复。
                Remove-Item -LiteralPath $entry.Path
            }
        }
        Write-Host '中文菜单测试槽已恢复；本轮新生成且原来不存在的测试槽已清理，原始槽备份保留在证据目录'
    }finally{if($locked){$mutex.ReleaseMutex()};$mutex.Dispose()}
}
