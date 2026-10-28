param([switch]$Editor,[string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8',[switch]$Preview,[string]$Resolution='1280x720',[switch]$Interactions)
$ErrorActionPreference='Stop'
if(($Editor -and $BuildRoot) -or (!$Editor -and !$BuildRoot)){throw '请只指定Editor或BuildRoot之一'}
if($Resolution -notmatch '^(960x540|1280x720|1920x1080)$'){throw '窗墙预览仅支持960x540、1280x720或1920x1080'}
$projectRoot=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$root=if($Editor){$projectRoot}else{(Resolve-Path -LiteralPath $BuildRoot).Path}
$exe=if($Editor){Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'}else{Join-Path $root 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}
$module=if($Editor){Join-Path $root 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}else{$exe}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$saveRoot=if($Editor){Join-Path $root 'Saved\SaveGames'}else{Join-Path $root 'CreatureFrontier\Saved\SaveGames'}
$run=Join-Path $projectRoot ('Saved\WindowPersistenceQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "窗墙跨进程证据：$run"
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierWindowQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '窗墙测试槽正在使用'}
$backups=@();$results=@()
try {
    foreach($name in @('WindowGood','WindowLegacy31')){
        $path=Join-Path $saveRoot ($name+'.sav');$exists=Test-Path -LiteralPath $path
        $backup=Join-Path $run ($name+'-before.sav')
        $before=if($exists){(Get-FileHash -LiteralPath $path).Hash}else{''}
        if($exists){Copy-Item -LiteralPath $path -Destination $backup}
        $backups+=@{Path=$path;Existed=$exists;Backup=$backup;Hash=$before;Name=$name}
    }
    $inputHash=''
    $cases=@(@{Name='Write';Flag='CFWindowTest';Marker='CF_WINDOW PASS built=1 hole=1 frame=1 pawn=1 support=1 legacy=1 save=1'},
                       @{Name='Read1';Flag='CFWindowRead';Marker='CF_WINDOW_READ PASS windows=2 loads=2'},
                       @{Name='Read2';Flag='CFWindowRead';Marker='CF_WINDOW_READ PASS windows=2 loads=2'})
    if($Interactions){$cases+=@(
        @{Name='Input';Flag='CFWindowInputTest';Marker='CF_WINDOW_INPUT PASS phase=2 windows=2 materials=72'},
        @{Name='Navigation';Flag='CFWindowNavigationTest';Marker='CF_WINDOW_NAV PASS phase=2 closed=1 continuous=1 windowBlocked=1'},
        @{Name='Melee';Flag='CFWindowMeleeTest';Marker='CF_WINDOW_MELEE PASS sill=1 opening=2 upper=1 side=1'})}
    foreach($case in $cases){
        $log=Join-Path $run ($case.Name+'.log')
        $arguments=@('-game','-nullrhi','-nosound','-unattended','-nop4','-CFHomeTest',('-'+$case.Flag),('-abslog="'+$log+'"'))
        if($Preview -and $case.Name -eq 'Write'){
            $dimensions=$Resolution.Split('x')
            $arguments=@($arguments | Where-Object {$_ -ne '-nullrhi'})+@('-CFWindowPreview','-RenderOffscreen','-ForceRes',('-ResX='+$dimensions[0]),('-ResY='+$dimensions[1]),'-norhithread','-forcerhibypass')
        }
        if($Editor){$arguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'))+$arguments}
        $process=Start-Process -FilePath $exe -WorkingDirectory $root -WindowStyle Hidden -PassThru -ArgumentList $arguments
        if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw '窗墙测试超时'}
        if($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch $case.Marker -Quiet) -or
           (Select-String -LiteralPath $log -Pattern '\bFAIL\b|Fatal error|Assertion failed' -Quiet)){throw "窗墙测试失败：$log"}
        if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '窗墙测试期间程序指纹变化'}
        if($Preview -and $case.Name -eq 'Write'){
            if(!(Select-String -LiteralPath $log -SimpleMatch 'CF_WINDOW_PREVIEW PASS scenes=3' -Quiet)){throw '窗墙预览未完成'}
            $rootLine=(Select-String -LiteralPath $log -Pattern 'CF_WINDOW_PREVIEW_ROOT .*[/\\]([A-Fa-f0-9]{32})\s*$' | Select-Object -Last 1)
            if(!$rootLine){throw '窗墙预览缺少独立截图目录'}
            $id=$rootLine.Matches[0].Groups[1].Value
            $shots=Join-Path (Split-Path $saveRoot -Parent) ('WindowPreview\'+$id)
            Add-Type -AssemblyName System.Drawing
            foreach($scene in @('Outside','Corner','Inside')){
                $png=Join-Path $shots ($scene+'.png')
                $bitmap=[Drawing.Image]::FromFile($png)
                try {if(($bitmap.Width.ToString()+'x'+$bitmap.Height) -ne $Resolution){throw '窗墙截图实际尺寸不符'}}finally{$bitmap.Dispose()}
                Copy-Item -LiteralPath $png -Destination (Join-Path $run ($scene+'.png'))
            }
        }
        $current=(Get-FileHash -LiteralPath (Join-Path $saveRoot 'WindowGood.sav')).Hash
        if($case.Name -eq 'Write'){$inputHash=$current}elseif($current -ne $inputHash){throw '窗墙读取改写了输入文件'}
        $results += [pscustomobject]@{Case=$case.Name;Pass=$true;Preview=[bool]$Preview;Interactions=[bool]$Interactions;Resolution=$Resolution;BuildSHA256=$hash;InputSHA256=$inputHash;Log=$log}
        $results | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
        Write-Host "$($case.Name) 通过"
    }
} finally {
    try {
        foreach($entry in $backups){
            if(Test-Path -LiteralPath $entry.Path){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ($entry.Name+'-generated.sav'))}
            if($entry.Existed){
                Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path
                if((Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.Hash){throw '原窗墙测试槽恢复校验失败'}
            } elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}
        }
    } finally {$mutex.ReleaseMutex();$mutex.Dispose()}
}
Write-Host '窗墙所选专项通过，输入未改写，原专用槽已还原；截图需目视，未选专项不计通过'
