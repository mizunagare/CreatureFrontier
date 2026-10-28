param([switch]$Editor,[string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if($Editor){
    $exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    $module=Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'
    $saveRoot=Join-Path $projectRoot 'Saved\SaveGames'
}else{
    if(!$BuildRoot){throw '请指定 Editor 或 BuildRoot'}
    $exe=Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
    $module=$exe
    $saveRoot=Join-Path $BuildRoot 'CreatureFrontier\Saved\SaveGames'
}
$hash=(Get-FileHash -LiteralPath $module).Hash
$run=Join-Path $projectRoot ('Saved\StorageRenderQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "储物箱图形证据：$run"
$backups=@()
$results=@()
try{
    foreach($slot in @('StorageGood','StorageInvalid','StorageLegacy20','StorageCross')){
        $path=Join-Path $saveRoot ($slot+'.sav')
        if(Test-Path -LiteralPath $path){
            $backup=Join-Path $run ($slot+'-before.sav')
            Copy-Item -LiteralPath $path -Destination $backup
            $backups+=@{Path=$path;Backup=$backup;Hash=(Get-FileHash -LiteralPath $path).Hash}
        }
    }
    foreach($size in @(@(960,540),@(1280,720),@(1920,1080))){
        $label="$($size[0])x$($size[1])"
        $log=Join-Path $run ($label+'.log')
        $started=[DateTime]::UtcNow
        $arguments=@('-game','-CFStorageTest','-CFStoragePreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-nosound','-unattended','-nop4','-norhithread','-forcerhibypass',('-abslog="'+$log+'"'))
        if($Editor){$arguments=@((Join-Path $projectRoot 'CreatureFrontier.uproject'))+$arguments}
        $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw "储物箱预览超时：$log"}
        if($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch 'CF_STORAGE PASS' -Quiet) -or !(Select-String -LiteralPath $log -SimpleMatch 'CF_STORAGE_PREVIEW PASS' -Quiet) -or (Select-String -LiteralPath $log -Pattern 'CF_\w+ FAIL|Fatal error|Assertion failed' -Quiet)){throw "储物箱专项失败：$log"}
        $source=Join-Path (Split-Path $saveRoot -Parent) 'StoragePreview\Storage.png'
        $info=Get-Item -LiteralPath $source
        if($info.Length -lt 1000 -or $info.LastWriteTimeUtc -lt $started){throw '储物箱截图不是本轮有效新图'}
        $header=New-Object byte[] 24
        $stream=[IO.File]::OpenRead($source)
        try{$read=$stream.Read($header,0,24)}finally{$stream.Dispose()}
        $width=[int]$header[16]*16777216+[int]$header[17]*65536+[int]$header[18]*256+[int]$header[19]
        $height=[int]$header[20]*16777216+[int]$header[21]*65536+[int]$header[22]*256+[int]$header[23]
        if($read -ne 24 -or $header[0] -ne 137 -or $header[1] -ne 80 -or $width -ne $size[0] -or $height -ne $size[1]){throw '储物箱 PNG 格式或尺寸不符'}
        Copy-Item -LiteralPath $source -Destination (Join-Path $run ($label+'.png'))
        if((Get-FileHash -LiteralPath $module).Hash -ne $hash){throw '测试期间程序模块发生变化'}
        $results+=[pscustomobject]@{Resolution=$label;Pass=$true;BuildSHA256=$hash;Log=$log}
        $results | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
        Write-Host "$label 行为与新截图尺寸检查通过，仍需逐图查看"
    }
}finally{
    foreach($entry in $backups){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.Hash){throw '原储物箱测试槽恢复校验失败'}}
}
