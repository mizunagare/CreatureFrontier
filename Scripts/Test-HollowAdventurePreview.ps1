param([Parameter(Mandatory=$true)][string]$BuildRoot)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
if(!(Test-Path -LiteralPath $exe)){throw '缺少打包程序'}
$hash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$evidence=Join-Path $projectRoot ('Saved\HollowAdventureQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $evidence | Out-Null
$log=Join-Path $evidence 'Adventure.log'
Write-Host "连续冒险图形证据：$evidence"
$arguments=@('-CFHollowAdventureTest','-CFHollowAdventurePreview','-RenderOffscreen','-ForceRes','-ResX=1280','-ResY=720','-nosound','-unattended','-nop4','-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.DefaultFeature.MotionBlur 0,r.AntiAliasingMethod 1"',('-abslog="'+$log+'"'))
$process=Start-Process -FilePath $exe -WorkingDirectory $BuildRoot -ArgumentList $arguments -WindowStyle Hidden -PassThru
$watch=[Diagnostics.Stopwatch]::StartNew();$sampledMemory=0L
while(!$process.WaitForExit(200)){
    if($watch.Elapsed.TotalSeconds -gt 90){$process.Kill();$process.WaitForExit();throw '连续冒险图形专项超时，原进程已停止'}
    try {$process.Refresh();$sampledMemory=[Math]::Max($sampledMemory,$process.WorkingSet64)} catch [InvalidOperationException] {}
}
$contents=Get-Content -LiteralPath $log -Raw
if($process.ExitCode -ne 0 -or $contents -notmatch 'Using Default RHI: D3D12' -or $contents -notmatch 'CF_HOLLOW_ADVENTURE PASS ' -or $contents -notmatch 'CF_HOLLOW_ADVENTURE_BATTLE PASS ' -or $contents -match 'CF_HOLLOW_ADVENTURE.*FAIL|Fatal error|Assertion failed'){throw "连续冒险或D3D12未通过：$log"}
$rootMatch=[regex]::Match($contents,'CF_HOLLOW_ADVENTURE_ROOT [^\r\n]*[/\\]([A-F0-9]{32})')
if(!$rootMatch.Success){throw '缺少独立截图目录'}
$shots=Join-Path $BuildRoot ('CreatureFrontier\Saved\HollowAdventurePreview\'+$rootMatch.Groups[1].Value)
foreach($scene in 'Entrance','Skill','Cache','Home'){
    if($contents -notmatch ('CF_HOLLOW_ADVENTURE_SHOT '+$scene+' viewport=1280x720 frame=\d+')){throw "缺少实际取景审计：$scene"}
    $source=Join-Path $shots ($scene+'.png')
    if(!(Test-Path -LiteralPath $source) -or (Get-Item -LiteralPath $source).Length -lt 1000){throw "缺少新截图：$source"}
    $header=New-Object byte[] 24;$stream=[IO.File]::OpenRead($source)
    try {$read=$stream.Read($header,0,24)} finally {$stream.Dispose()}
    if($read -ne 24 -or $header[0] -ne 137 -or $header[1] -ne 80){throw '无效PNG'}
    $width=[int]$header[16]*16777216+[int]$header[17]*65536+[int]$header[18]*256+[int]$header[19]
    $height=[int]$header[20]*16777216+[int]$header[21]*65536+[int]$header[22]*256+[int]$header[23]
    if($width -ne 1280 -or $height -ne 720){throw '截图实际尺寸不符'}
    Copy-Item -LiteralPath $source -Destination (Join-Path $evidence ($scene+'.png'))
}
$frames=[regex]::Matches($contents,'\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\].*CF_HOLLOW_ADVENTURE_SHOT (Entrance|Home) viewport=1280x720 frame=(\d+)')
if($frames.Count -ne 2){throw '缺少取景区间帧采样'}
$firstTime=[DateTime]::ParseExact($frames[0].Groups[1].Value,'yyyy.MM.dd-HH.mm.ss:fff',[Globalization.CultureInfo]::InvariantCulture)
$lastTime=[DateTime]::ParseExact($frames[1].Groups[1].Value,'yyyy.MM.dd-HH.mm.ss:fff',[Globalization.CultureInfo]::InvariantCulture)
$fps=([long]$frames[1].Groups[3].Value-[long]$frames[0].Groups[3].Value)/($lastTime-$firstTime).TotalSeconds
if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $hash){throw '测试期间程序改变'}
[pscustomobject]@{Pass=$true;Resolution='1280x720';ShotIntervalFPS=[Math]::Round($fps,2);SampledWorkingSetMiB=[Math]::Round($sampledMemory/1MB,1);BuildSHA256=$hash;Log=$log} | Export-Csv -LiteralPath (Join-Path $evidence 'Results.csv') -NoTypeInformation
Write-Host "连续冒险与四图尺寸通过，仍需逐图查看：$evidence"
