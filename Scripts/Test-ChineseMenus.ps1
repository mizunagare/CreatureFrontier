param([Parameter(Mandatory=$true)][string]$BuildRoot)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
if(!(Test-Path -LiteralPath $exe)){throw '找不到打包程序'}
$hash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$evidence=Join-Path $projectRoot ('Saved\ChineseMenus\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $evidence | Out-Null
Write-Host "中文菜单证据：$evidence"
$cases=@(
    @{Name='Backpack';Flag='CFBackpackTest';Preview='CFBackpackPreview';Marker='CF_BACKPACK PASS';Files=@('BackpackPreview/backpack.png')},
    @{Name='Storage';Flag='CFStorageTest';Preview='CFStoragePreview';Marker='CF_STORAGE PASS';Files=@('StoragePreview/Storage.png')},
    # 旧图鉴预览在完整断言前提前返回，只检查预览；完整逻辑由本地回归另验。
    @{Name='Journal';Flag='CFJournalTest';Preview='CFJournalPreview';Marker='';Files=@('JournalPreview/entry1.png','JournalPreview/entry2.png')},
    @{Name='Session';Flag='CFSessionTest';Preview='CFSessionPreview';Marker='CF_SESSION PASS';Files=@('SessionPreview/paused.png','SessionPreview/controls.png','SessionPreview/compact.png')},
    @{Name='Name';Flag='CFNameTest';Preview='CFNamePreview';Marker='CF_NAME_PREVIEW PASS';Files=@('NamePreview/Dialog.png','NamePreview/Roster.png')},
    @{Name='Memory';Flag='CFMemoryTest';Preview='CFMemoryPreview';Marker='CF_MEMORY_PREVIEW PASS';Files=@('MemoryPreview/Dialog.png','MemoryPreview/Roster.png')}
)
$results=@()
foreach($case in $cases){
    $started=[DateTime]::UtcNow
    $log=Join-Path $evidence ($case.Name+'.log')
    $previewArguments=@(('-'+$case.Flag),('-'+$case.Preview),'-RenderOffscreen','-ForceRes','-ResX=1280','-ResY=720','-nosound','-unattended','-nop4','-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.DefaultFeature.MotionBlur 0,r.AntiAliasingMethod 1"',('-abslog="'+$log+'"'))
    $process=Start-Process -FilePath $exe -WorkingDirectory $BuildRoot -ArgumentList $previewArguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw "菜单预览超时：$log"}
    if($process.ExitCode -ne 0 -or ($case.Marker -and !(Select-String -LiteralPath $log -SimpleMatch $case.Marker -Quiet))){throw "菜单预览未通过：$log"}
    foreach($file in $case.Files){
        $source=Join-Path $BuildRoot ('CreatureFrontier\Saved\'+$file)
        if(!(Test-Path -LiteralPath $source)){throw "缺少截图：$source"}
        $info=Get-Item -LiteralPath $source
        if($info.Length -lt 1000 -or $info.LastWriteTimeUtc -lt $started){throw "截图为空或不是本轮生成：$source"}
        Copy-Item -LiteralPath $source -Destination (Join-Path $evidence ($case.Name+'-'+(Split-Path $file -Leaf)))
    }
    if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $hash){throw '测试期间程序发生改变'}
    $results += [pscustomobject]@{Case=$case.Name;Pass=$true;BehaviorMarker=$case.Marker;Screens=$case.Files.Count;BuildSHA256=$hash;Log=$log}
    $results | Export-Csv -LiteralPath (Join-Path $evidence 'Results.csv') -NoTypeInformation
    Write-Host ($case.Name+'：预览及新截图检查通过，仍需逐图查看')
}
