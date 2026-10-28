param([Parameter(Mandatory=$true)][string]$BuildRoot,[string[]]$Resolutions=@('960x540','1280x720','1920x1080'))
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
if(!(Test-Path -LiteralPath $exe)){throw '找不到打包程序'}
$hash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$evidence=Join-Path $projectRoot ('Saved\HollowRenderQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $evidence | Out-Null
Write-Host "洞窟图形证据：$evidence"
$results=@()
foreach($resolution in $Resolutions){
    if($resolution -notmatch '^(\d{3,4})x(\d{3,4})$'){throw '尺寸须为宽x高'}
    $width=[int]$Matches[1];$height=[int]$Matches[2]
    if($width -lt 640 -or $width -gt 4096 -or $height -lt 360 -or $height -gt 2160){throw '尺寸超出检查范围'}
    $log=Join-Path $evidence ($resolution+'.log')
    $previewArguments=@('-CFHollowPreview','-RenderOffscreen','-ForceRes',('-ResX='+$width),('-ResY='+$height),'-nosound','-unattended','-nop4','-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"',('-abslog="'+$log+'"'))
    $process=Start-Process -FilePath $exe -WorkingDirectory $BuildRoot -ArgumentList $previewArguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw "预览超时：$log"}
    $contents=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $contents -notmatch 'CF_HOLLOW_PREVIEW PASS scenes=4' -or $contents -match 'CF_HOLLOW.*FAIL|Fatal error|Assertion failed'){throw "预览失败：$log"}
    $rootMatch=[regex]::Match($contents,'CF_HOLLOW_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})')
    if(!$rootMatch.Success){throw '没有记录新截图目录'}
    $shots=Join-Path $BuildRoot ('CreatureFrontier\Saved\HollowPreview\'+$rootMatch.Groups[1].Value)
    foreach($scene in 'Entrance','Switchback','Chamber','Cache'){
        if($contents -notmatch ('CF_HOLLOW_FRAME PASS scene='+$scene+' viewport='+$resolution)){throw '实际视口或场景不符'}
        $source=Join-Path $shots ($scene+'.png')
        if(!(Test-Path -LiteralPath $source) -or (Get-Item -LiteralPath $source).Length -lt 1000){throw "缺少截图：$source"}
        $header=New-Object byte[] 24;$stream=[IO.File]::OpenRead($source)
        try {$read=$stream.Read($header,0,24)} finally {$stream.Dispose()}
        if($read -ne 24 -or $header[0] -ne 137 -or $header[1] -ne 80){throw 'PNG文件头无效'}
        $pngWidth=[int]$header[16]*16777216+[int]$header[17]*65536+[int]$header[18]*256+[int]$header[19]
        $pngHeight=[int]$header[20]*16777216+[int]$header[21]*65536+[int]$header[22]*256+[int]$header[23]
        if($pngWidth -ne $width -or $pngHeight -ne $height){throw 'PNG尺寸不符'}
        Copy-Item -LiteralPath $source -Destination (Join-Path $evidence ($resolution+'-'+$scene+'.png'))
    }
    $metrics=[regex]::Match($contents,'CF_HOLLOW_METRICS seconds=([\d.]+) frames=(\d+) fps=([\d.]+) peakSampledMB=([\d.]+)')
    if(!$metrics.Success){throw '没有本轮帧数与内存采样'}
    if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $hash){throw '测试期间程序改变'}
    $results += [pscustomobject]@{Resolution=$resolution;Pass=$true;FPS=$metrics.Groups[3].Value;SampledMemoryMB=$metrics.Groups[4].Value;BuildSHA256=$hash;Log=$log}
    $results | Export-Csv -LiteralPath (Join-Path $evidence 'Results.csv') -NoTypeInformation
    Write-Host "$resolution：四视图渲染及尺寸检查通过，仍需逐图查看；该限帧离屏采样不是默认性能基准"
}
