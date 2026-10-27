param(
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [int[]]$FrameCaps=@(30,60,120),
    [int[]]$LeaderForms=@(0,1,2),
    [int[]]$SideOrders=@(0,1),
    [ValidateRange(1,10)][int]$Repeats=2,
    [switch]$Trace
)

$ErrorActionPreference='Stop'
if(!$FrameCaps.Count -or !$LeaderForms.Count -or !$SideOrders.Count) { throw 'The matrix must not be empty' }
foreach($cap in $FrameCaps) { if($cap -ne 0 -and ($cap -lt 15 -or $cap -gt 240)) { throw 'FrameCaps must be 0 (uncapped) or in 15..240' } }
foreach($leader in $LeaderForms) { if($leader -lt 0 -or $leader -gt 2) { throw 'LeaderForms: 0 Leaf, 1 Ember, 2 Stone' } }
foreach($order in $SideOrders) { if($order -ne 0 -and $order -ne 1) { throw 'SideOrders: 0 cyclic, 1 reversed' } }
if(@($FrameCaps | Select-Object -Unique).Count -ne $FrameCaps.Count -or
    @($LeaderForms | Select-Object -Unique).Count -ne $LeaderForms.Count -or
    @($SideOrders | Select-Object -Unique).Count -ne $SideOrders.Count) { throw 'Matrix axes must not contain duplicates' }
$resolvedRoot=(Resolve-Path -LiteralPath $BuildRoot).Path
$exe=Join-Path $resolvedRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
if(!(Test-Path -LiteralPath $exe)) { throw "Missing packaged executable: $exe" }
$binaryHash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$projectRoot=Split-Path $PSScriptRoot -Parent
$runName=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$logRoot=Join-Path $projectRoot "Saved\FormationMatrix\$runName"
New-Item -ItemType Directory -Path $logRoot | Out-Null
Write-Host "Formation matrix: $logRoot"
Write-Host "EXE SHA256: $binaryHash"
$results=@()
foreach($leader in $LeaderForms) {
  foreach($order in $SideOrders) {
    foreach($cap in $FrameCaps) {
        foreach($attempt in 1..$Repeats) {
            $label="Lead-$leader-Reverse-$order-FPS-$cap-Run-$attempt"
            $log=Join-Path $logRoot "$label.log"
            $arguments=@('-nullrhi','-nosound','-unattended','-nop4','-FixedSeed','-CFFormationTest',
                "-CFFormationLead=$leader","-ExecCmds=`"t.MaxFPS $cap`"","-abslog=`"$log`"")
            if($Trace) { $arguments+='-CFFormationTrace' }
            if($order) { $arguments+='-CFFormationReverse' }
            $process=Start-Process -FilePath $exe -WorkingDirectory $resolvedRoot -ArgumentList $arguments -WindowStyle Hidden -PassThru
            $finished=$process.WaitForExit(60000)
            if(!$finished) { $process.Kill(); $process.WaitForExit(5000) | Out-Null }
            $fixture=$false; $marker=$false
            if(Test-Path -LiteralPath $log) {
                $fixture=Select-String -LiteralPath $log -SimpleMatch "CF_FORMATION_FIXTURE lead=$leader center=$leader named=1 reverse=$order" -Quiet
                $marker=Select-String -LiteralPath $log -SimpleMatch 'CF_FORMATION PASS' -Quiet
            }
            $passed=$finished -and $process.ExitCode -eq 0 -and $fixture -and $marker
            $results += [pscustomobject]@{Case=$label;Leader=$leader;Reverse=$order;FrameCap=$cap;Finished=$finished;ExitCode=$process.ExitCode;Fixture=$fixture;PassMarker=$marker;Passed=$passed;BuildHash=$binaryHash;Log=$log}
            Write-Host "${label}: Passed=$passed Finished=$finished Fixture=$fixture Marker=$marker Exit=$($process.ExitCode)"
        }
    }
  }
}
$results | Export-Csv -LiteralPath (Join-Path $logRoot 'Results.csv') -NoTypeInformation -Encoding utf8
$failures=@($results | Where-Object { !$_.Passed })
if($failures.Count) { throw "$($failures.Count)/$($results.Count) formation matrix cases failed; inspect $logRoot" }
Write-Host "FORMATION_MATRIX PASS $($results.Count)/$($results.Count)"
