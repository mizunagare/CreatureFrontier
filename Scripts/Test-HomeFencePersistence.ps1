param([string]$BuildRoot='',[string]$EngineRoot='D:\Epic\UE_5.8')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$projectRoot=Split-Path $PSScriptRoot -Parent
$saveRoot=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Saved\SaveGames'}else{Join-Path $projectRoot 'Saved\SaveGames'}
$module=if($BuildRoot){Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'}else{Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}
$hash=Get-FrontierBuildFingerprint -LiteralPath $module
$run=Join-Path $projectRoot ('Saved\FencePersistenceQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
Write-Host "栅栏跨进程存读：$run"
$mutex=[Threading.Mutex]::new($false,'Local\CreatureFrontierFencePersistenceQA')
if(!$mutex.WaitOne(0)){$mutex.Dispose();throw '栅栏存读测试槽正在使用'}
$protected=@()
try {
    foreach($name in @('HomeFenceCross','HomeFenceBad32')){
        $path=Join-Path $saveRoot ($name+'.sav');$existed=Test-Path -LiteralPath $path
        $backup=Join-Path $run ($name+'-before.sav');$beforeHash=''
        if($existed){$beforeHash=(Get-FileHash -LiteralPath $path).Hash;Copy-Item -LiteralPath $path -Destination $backup}
        $protected+=[pscustomobject]@{Path=$path;Existed=$existed;Backup=$backup;Hash=$beforeHash}
    }
    & (Join-Path $PSScriptRoot 'Test-HomeInput.ps1') -BuildRoot $BuildRoot -EngineRoot $EngineRoot -Fence -Mode Write
    $slot=Join-Path $saveRoot 'HomeFenceCross.sav'
    $inputHash=(Get-FileHash -LiteralPath $slot).Hash
    Copy-Item -LiteralPath $slot -Destination (Join-Path $run 'HomeFenceCross-input.sav')
    foreach($attempt in 1..2){
        & (Join-Path $PSScriptRoot 'Test-HomeInput.ps1') -BuildRoot $BuildRoot -EngineRoot $EngineRoot -Fence -Mode Read
        if((Get-FileHash -LiteralPath $slot).Hash -ne $inputHash){throw '栅栏读取改写源输入'}
        if((Get-FrontierBuildFingerprint -LiteralPath $module) -ne $hash){throw '栅栏链期间模块变化'}
        [pscustomobject]@{Attempt=$attempt;Pass=$true;InputSHA256=$inputHash;BuildSHA256=$hash} | Export-Csv -LiteralPath (Join-Path $run ('Read-'+$attempt+'.csv')) -NoTypeInformation
    }
    Write-Host '栅栏写入、两独立读取和伪旧版本拒绝通过；不是实际旧32档兼容或伙伴导航验收'
} finally {
    try {foreach($entry in $protected){
        if($entry.Existed){Copy-Item -LiteralPath $entry.Backup -Destination $entry.Path;if((Get-FileHash -LiteralPath $entry.Path).Hash -ne $entry.Hash){throw '栅栏测试原槽恢复失败'}}
        elseif(Test-Path -LiteralPath $entry.Path){Remove-Item -LiteralPath $entry.Path}
    }} finally {$mutex.ReleaseMutex();$mutex.Dispose()}
}
