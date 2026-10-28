param([Parameter(Mandatory=$true)][string]$BuildRoot)
$ErrorActionPreference='Stop'
$exe=Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
if(!(Test-Path -LiteralPath $exe)){throw '缺少打包程序'}
$hash=(Get-FileHash -LiteralPath $exe).Hash
$run=Join-Path (Split-Path $PSScriptRoot -Parent) ('Saved\SkillTrailQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "技能空间反馈图形证据：$run"
$results=@()
foreach($resolution in '960x540','1280x720','1920x1080'){
    $width,$height=$resolution.Split('x') | ForEach-Object {[int]$_}
    foreach($covered in $false,$true){
        $label=$resolution+'-'+$(if($covered){'Covered'}else{'Visible'})
        $log=Join-Path $run ($label+'.log')
        $shot=Join-Path $BuildRoot 'CreatureFrontier\Saved\SkillTrailPreview.png'
        $arguments=@('-CFCombatTest','-CFSkillTrailPreview','-RenderOffscreen','-ForceRes',('-ResX='+$width),('-ResY='+$height),'-nosound','-unattended','-nop4','-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"',('-abslog="'+$log+'"'))
        if($covered){$arguments+='-CFSkillTrailCovered'}
        $started=[DateTime]::UtcNow
        $process=Start-Process -FilePath $exe -WorkingDirectory $BuildRoot -ArgumentList $arguments -WindowStyle Hidden -PassThru
        if(!$process.WaitForExit(60000)){$process.Kill();$process.WaitForExit();throw "技能取景超时：$label"}
        $contents=Get-Content -LiteralPath $log -Raw
        $occluded=if($covered){21}else{0};$mode=if($covered){1}else{0}
        $marker="CF_SKILL_TRAIL_SHOT PASS active=3 projected=21 occluded=$occluded covered=$mode viewport=${width}x$height frame="
        if($process.ExitCode -ne 0 -or !$contents.Contains($marker) -or $contents -notmatch 'Using Default RHI: D3D12' -or $contents -notmatch 'CF_COMBAT PASS ' -or $contents -notmatch 'CF_SKILL_TRAIL PASS geometry=1 cleanup=1' -or $contents -match 'CF_SKILL_TRAIL.*FAIL|Fatal error|Assertion failed'){throw "技能图形行为未通过：$log"}
        if(!(Test-Path -LiteralPath $shot) -or (Get-Item -LiteralPath $shot).LastWriteTimeUtc -lt $started){throw '缺少本次截图'}
        $header=New-Object byte[] 24;$stream=[IO.File]::OpenRead($shot)
        try{$read=$stream.Read($header,0,24)}finally{$stream.Dispose()}
        $actualWidth=[int]$header[16]*16777216+[int]$header[17]*65536+[int]$header[18]*256+[int]$header[19]
        $actualHeight=[int]$header[20]*16777216+[int]$header[21]*65536+[int]$header[22]*256+[int]$header[23]
        if($read -ne 24 -or $header[0] -ne 137 -or $header[1] -ne 80 -or $actualWidth -ne $width -or $actualHeight -ne $height){throw '截图格式或实际尺寸不符'}
        Copy-Item -LiteralPath $shot -Destination (Join-Path $run ($label+'.png'))
        $results+=[pscustomobject]@{Scene=$label;Pass=$true;BuildSHA256=$hash;ScreenshotSHA256=(Get-FileHash -LiteralPath $shot).Hash}
        $results | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
        Write-Host "${label}：行为、投影及PNG尺寸通过，仍需逐图查看"
    }
}
if((Get-FileHash -LiteralPath $exe).Hash -ne $hash){throw '测试期间程序改变'}
Write-Host "六组技能取景检查通过：$run"
