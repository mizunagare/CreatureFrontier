param([string]$BuildRoot='', [switch]$Editor, [switch]$ReuseInput,
      [string]$EngineRoot='D:\Epic\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
if($Editor){
    $exe=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    $working=$projectRoot
    $prefix=@((Join-Path $projectRoot 'CreatureFrontier.uproject'),'/Game/Maps/L_Prototype','-game')
    $saveRoot=Join-Path $projectRoot 'Saved\SaveGames'
}else{
    if(!$BuildRoot){throw '打包模式必须指定BuildRoot'}
    $exe=Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
    $working=$BuildRoot;$prefix=@();$saveRoot=Join-Path $BuildRoot 'CreatureFrontier\Saved\SaveGames'
}
if(!(Test-Path -LiteralPath $exe)){throw '测试程序不存在'}
$run=Join-Path $projectRoot ('Saved\RescueAdventureQA\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run | Out-Null
$buildHash=(Get-FileHash -LiteralPath $exe).Hash
$module=if($Editor){Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-CreatureFrontier.dll'}else{''}
$moduleHash=if($module){(Get-FileHash -LiteralPath $module).Hash}else{''}
$cases=@()
if(!$ReuseInput){$cases+=@{Flag='CFRescueNaturalWrite';Marker='CF_RESCUE_NATURAL_WRITE PASS';Timeout=115}}
$cases+=@{Flag='CFRescueNaturalResume';Marker='CF_RESCUE_NATURAL_RESUME PASS';Timeout=135}
$cases+=@{Flag='CFRescueNaturalRead';Marker='CF_RESCUE_NATURAL_READ PASS';Timeout=60}
$source=Join-Path $saveRoot 'RescueNaturalDowned.sav'
if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination (Join-Path $run 'Input-before.sav')}
$inputHash=if($ReuseInput){(Get-FileHash -LiteralPath $source).Hash}else{''}
$results=@()
Write-Host "救助证据目录：$run"
foreach($case in $cases){
    $log=Join-Path $run ($case.Flag+'.log')
    $arguments=$prefix+@('-nullrhi','-nosound','-unattended','-nop4',('-'+$case.Flag),('-abslog="'+$log+'"'))
    $process=Start-Process -FilePath $exe -WorkingDirectory $working -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit($case.Timeout*1000)){$process.Kill();$process.WaitForExit();throw "救助测试超时：$log"}
    if($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch $case.Marker -Quiet) -or (Select-String -LiteralPath $log -Pattern 'CF_RESCUE.*FAIL|Fatal error|Assertion failed' -Quiet)){throw "救助测试失败：$log"}
    if(!$inputHash){$inputHash=(Get-FileHash -LiteralPath $source).Hash}
    if((Get-FileHash -LiteralPath $source).Hash -ne $inputHash){throw '倒地输入存档被改写'}
    $inputCopy=Join-Path $run 'Input.sav'
    if(!(Test-Path -LiteralPath $inputCopy)){Copy-Item -LiteralPath $source -Destination $inputCopy}
    if((Get-FileHash -LiteralPath $inputCopy).Hash -ne $inputHash){throw '留存输入副本哈希不符'}
    if($case.Flag -eq 'CFRescueNaturalRead'){Copy-Item -LiteralPath (Join-Path $saveRoot 'RescueNaturalFinal.sav') -Destination (Join-Path $run 'Final.sav')}
    if((Get-FileHash -LiteralPath $exe).Hash -ne $buildHash){throw '测试程序发生变化'}
    if($module -and (Get-FileHash -LiteralPath $module).Hash -ne $moduleHash){throw 'Editor游戏模块发生变化'}
    $results+=[pscustomobject]@{Flag=$case.Flag;Pass=$true;Editor=[bool]$Editor;InputSHA256=$inputHash;BuildSHA256=$buildHash;ModuleSHA256=$moduleHash;Log=$log}
    $results | Export-Csv -LiteralPath (Join-Path $run 'Results.csv') -NoTypeInformation
    Write-Host "$($case.Flag)：通过，输入文件未改写"
}
