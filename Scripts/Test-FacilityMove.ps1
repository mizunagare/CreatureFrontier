param([Parameter(Mandatory=$true)][string]$ReadInputPath,[string]$BuildRoot='',[switch]$Resume,[switch]$Preview,[ValidateSet('960x540','1280x720')][string]$Resolution='1280x720')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$ReadInputPath=(Resolve-Path -LiteralPath $ReadInputPath).Path
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Binaries/Win64/CreatureFrontier.exe'}else{Join-Path $root 'Binaries/Win64/UnrealEditor-CreatureFrontier.dll'}
$saved=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier/Saved'}else{Join-Path $root 'Saved'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$inputHash=Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath
$run=Join-Path $root ('Saved/FacilityMoveQA/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run|Out-Null
$slots=Join-Path $saved 'SaveGames';New-Item -ItemType Directory -Path $slots -Force|Out-Null
$protected=@();$started=$null
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierFacilityMoveQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '设施搬迁验证正在运行'}
try{
    foreach($name in @('FacilityMoveInput','FacilityMoveMid','FacilityMoveDone','FacilityMoveBlocked')){
        $path=Join-Path $slots ($name+'.sav');if($path -eq $ReadInputPath){throw '来源必须使用独立副本'}
        $backup=Join-Path $run ($name+'-before.sav');$before=if(Test-Path $path){Get-FrontierBuildFingerprint -LiteralPath $path}else{''}
        if($before){Copy-Item -LiteralPath $path -Destination $backup};$protected+=@{Path=$path;Backup=$backup;Hash=$before}
    }
    Copy-Item -LiteralPath $ReadInputPath -Destination (Join-Path $slots 'FacilityMoveInput.sav')
    $log=Join-Path $run 'Move.log';$flag=if($Resume){'-CFFacilityMoveRead'}else{'-CFFacilityMoveTest'}
    $arguments=@($flag,'-nullrhi','-nosound','-unattended','-nop4',('-abslog="'+$log+'"'))
    if($Preview){$size=$Resolution.Split('x');$arguments=@($arguments|Where-Object {$_ -ne '-nullrhi'})+@('-CFFacilityMovePreview','-RenderOffscreen','-ForceRes',('-ResX='+$size[0]),('-ResY='+$size[1]),'-norhithread','-forcerhibypass','-ExecCmds="t.MaxFPS 60,r.MotionBlurQuality 0,r.AntiAliasingMethod 1"')}
    $exe=if($BuildRoot){$module}elseif($Preview){'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe'}else{'D:/Epic/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'}
    if(!$BuildRoot){$arguments=@(('"'+(Join-Path $root 'CreatureFrontier.uproject')+'"'),'-game')+$arguments}
    [pscustomobject]@{BuildSHA256=$hash;Input=$ReadInputPath;InputSHA256=$inputHash;Resume=[bool]$Resume;Preview=[bool]$Preview;Resolution=$Resolution}|Export-Csv (Join-Path $run 'Run.csv') -NoTypeInformation
    $started=[DateTime]::UtcNow;Write-Host ('真实设施搬迁证据：'+$run)
    $p=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$p.WaitForExit(240000)){$p.Kill();$p.WaitForExit();throw '设施搬迁验证超时'}
    $raw=Get-Content -LiteralPath $log -Raw
    if($p.ExitCode -ne 0 -or $raw -match '\bFAIL\b|Fatal error|Assertion failed' -or !$raw.Contains('CF_FACILITY_MOVE_LIFE PASS materialPaid=5 medicineAdded=1 oldXP=40 newXP=18 identity=1 moved=1 saved=1') -or !$raw.Contains('LogExit: Exiting.')){throw ('设施搬迁整链失败：'+$log)}
    if(!$Resume){foreach($marker in @('CF_FACILITY_MOVE_CANCEL PASS','CF_FACILITY_MOVE_COMMIT PASS','CF_FACILITY_MOVE_MID PASS')){if(!$raw.Contains($marker)){throw ('缺少验收标记：'+$marker)}}}
    if($Preview){
        $id=[regex]::Match($raw,'CF_FACILITY_MOVE_PREVIEW_ROOT [^\r\n]*[/\\]([A-F0-9]{32})');if(!$id.Success){throw '缺少截图目录'}
        $scenes=if($Resume){@('Completed')}else{@('Preview','Arrived','Completed')}
        foreach($name in $scenes){
            $path=Join-Path $saved ('FacilityMovePreview/'+$id.Groups[1].Value+'/'+$name+'.png')
            if(!(Test-Path $path) -or (Get-Item $path).LastWriteTimeUtc -lt $started){throw '缺少本轮新截图'}
            $b=[IO.File]::ReadAllBytes($path);if($b.Length -lt 1000 -or $b[0] -ne 137){throw '截图格式无效'}
            $w=[int]$b[16]*16777216+[int]$b[17]*65536+[int]$b[18]*256+[int]$b[19];$h=[int]$b[20]*16777216+[int]$b[21]*65536+[int]$b[22]*256+[int]$b[23]
            if(($w.ToString()+'x'+$h) -ne $Resolution){throw '截图尺寸不符'}
            Copy-Item -LiteralPath $path -Destination (Join-Path $run ($name+'.png'))
        }
    }
    if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash -or (Get-FrontierBuildFingerprint -LiteralPath $ReadInputPath) -ne $inputHash){throw '程序或来源发生变化'}
    [pscustomobject]@{Pass=$true;BuildSHA256=$hash;Log=$log;VisualReviewRequired=[bool]$Preview;Scope='实际行为与存读；有图形时必须另行目视'}|Export-Csv (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host '真实设施搬迁行为与存读通过；图形和其他平台范围仍需独立验证'
}finally{
    try{foreach($entry in $protected){
        if($started -and (Test-Path $entry.Path) -and (Get-Item $entry.Path).LastWriteTimeUtc -ge $started){Copy-Item -LiteralPath $entry.Path -Destination (Join-Path $run ((Split-Path $entry.Path -LeafBase)+'-generated.sav'))}
        if($entry.Hash){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FrontierBuildFingerprint -LiteralPath $entry.Path) -ne $entry.Hash){throw '原测试槽恢复失败'}}elseif(Test-Path $entry.Path){Remove-Item -LiteralPath $entry.Path}
    }}finally{$mutex.ReleaseMutex();$mutex.Dispose()}
}
