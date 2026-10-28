param([string]$BuildRoot = '', [int]$TimeoutSeconds = 60)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BuildFingerprint.ps1')
$projectRoot = Split-Path $PSScriptRoot -Parent
if (-not $BuildRoot) {
    $candidates = @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Builds') -Directory | ForEach-Object {
        $binary = Join-Path $_.FullName 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
        if (Test-Path -LiteralPath $binary) {
            [pscustomobject]@{ Root=$_.FullName; Built=(Get-Item -LiteralPath $binary).LastWriteTimeUtc }
        }
    } | Sort-Object Built -Descending)
    if ($candidates.Count -eq 0) { throw '找不到已打包的 Development 构建' }
    $BuildRoot = $candidates[0].Root
}
if ($TimeoutSeconds -lt 10 -or $TimeoutSeconds -gt 60) { throw '超时时间必须为 10 至 60 秒' }
Write-Host "正在测试构建：$BuildRoot"
$exePath = Join-Path $BuildRoot 'CreatureFrontier\Binaries\Win64\CreatureFrontier.exe'
if (-not (Test-Path -LiteralPath $exePath)) { throw "找不到 Development 游戏程序：$exePath" }
$buildHash=Get-FrontierBuildFingerprint -LiteralPath $exePath
$runName = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$testLogRoot = Join-Path $projectRoot (Join-Path 'Saved\PackagedTests' $runName)
New-Item -ItemType Directory -Path $testLogRoot -Force | Out-Null
Write-Host "本次测试日志：$testLogRoot"
$cases = @(
    @{Flag='CFDoorFarmTest'; Marker='CF_DOOR_FARM PASS'; RequiredMarkers=@('CF_DOOR_FARM_CLOSED PASS','CF_DOOR_FARM_FIXTURE roof=1 removed=0')},
    @{Flag='CFDoorFarmTest'; Label='CFStanceFarm'; Arguments=@('-CFStanceFarmTest'); Marker='CF_STANCE_FARM PASS'; RequiredMarkers=@('CF_STANCE_FARM_SWITCH PASS','CF_DOOR_FARM_CLOSED PASS','CF_DOOR_FARM PASS')},
    @{Flag='CFDoorFarmTest'; Label='CFDoorFarmNoRoof'; Arguments=@('-CFDoorFarmNoRoof'); Marker='CF_DOOR_FARM PASS'; RequiredMarkers=@('CF_DOOR_FARM_CLOSED PASS','CF_DOOR_FARM_FIXTURE roof=0 removed=1')},
    @{Flag='CFHomePaletteTest'; Marker='CF_HOME_PALETTE PASS built=1 opened=1 closed=1'; RequiredMarkers=@('CF_HOME_PALETTE_PREVIEW PASS','CF_HOME_PALETTE_INDEPENDENT stage=settled pass=1')},
    @{Flag='CFWindowTest'; Arguments=@('-CFHomeTest'); Marker='CF_WINDOW PASS built=1 hole=1 frame=1 pawn=1 support=1 legacy=1 save=1'},
    @{Flag='CFWindowInputTest'; Arguments=@('-CFHomeTest'); Marker='CF_WINDOW_INPUT PASS phase=2 windows=2 materials=72'},
    @{Flag='CFWindowNavigationTest'; Marker='CF_WINDOW_NAV PASS phase=2 closed=1 continuous=1 windowBlocked=1'},
    @{Flag='CFWindowMeleeTest'; Arguments=@('-CFHomeTest'); Marker='CF_WINDOW_MELEE PASS sill=1 opening=2 upper=1 side=1'},
    @{Flag='CFWindowSkillTest'; Marker='CF_WINDOW_SKILL PASS phase=3 charged=1'; RequiredMarkers=@('CF_WINDOW_SKILL_BLOCKED PASS','CF_WINDOW_SKILL_SPLASH PASS')},
    @{Flag='CFWindowRead'; Arguments=@('-CFHomeTest'); Marker='CF_WINDOW_READ PASS windows=2 loads=2'; PreserveSlot='WindowGood'},
    @{Flag='CFStanceTest'; Arguments=@('-CFDefenseTest'); Marker='CF_STANCE PASS defaults=1 passive=1 explicit=1 cancel=1 invalid=1 persist=1 legacy=1'; RequiredMarkers=@('CF_STANCE_BOUNDARIES PASS scope=1 guard=1 bounds=1 work=1','CF_STANCE_SKILL PASS explicit=1 cancel=1')},
    @{Flag='CFStanceRead'; Arguments=@('-CFDefenseTest'); Marker='CF_STANCE_READ PASS avoid=2 guard=2'; PreserveSlot='StanceGood'},
    @{Flag='CFStanceRetreatTest'; Arguments=@('-CFDefenseTest'); Marker='CF_STANCE_RETREAT PASS'; RequiredMarkers=@('CF_STANCE_RETREAT_SWITCH PASS')},
    @{Flag='CFStanceRetreatTest'; Label='CFStanceRetreat30'; Arguments=@('-CFDefenseTest','-ExecCmds="t.MaxFPS 30"'); Marker='CF_STANCE_RETREAT PASS'; RequiredMarkers=@('CF_STANCE_RETREAT_SWITCH PASS','t.MaxFPS = "30"')},
    @{Flag='CFDoorTest'; Marker='CF_DOOR PASS'},
    @{Flag='CFDoorOpenRead'; Marker='CF_DOOR_OPEN_READ PASS'},
    @{Flag='CFDoorClosedRead'; Marker='CF_DOOR_CLOSED_READ PASS'},
    @{Flag='CFDoorNavigationTest'; Marker='CF_DOOR_NAV PASS yaw=0'; RequiredMarkers=@('CF_DOOR_NAV_CLOSED PASS')},
    @{Flag='CFDoorNavigationRotatedTest'; Marker='CF_DOOR_NAV PASS yaw=90'; RequiredMarkers=@('CF_DOOR_NAV_CLOSED PASS')},
    @{Flag='CFDoorPlayerTest'; Marker='CF_DOOR_PLAYER PASS yaw=0'},
    @{Flag='CFDoorPlayerRotatedTest'; Marker='CF_DOOR_PLAYER PASS yaw=90'},
    @{Flag='CFRescueMemoryTest'; Marker='CF_RESCUE_MEMORY PASS mode=0'; RequiredMarkers=@('CF_RESCUE_PAUSED_HEAL PASS')},
    @{Flag='CFRescueMemoryWrite'; Marker='CF_RESCUE_MEMORY PASS mode=1'},
    @{Flag='CFRescueMemoryRescueRead'; Marker='CF_RESCUE_MEMORY PASS mode=2'},
    @{Flag='CFRescueMemoryHomeRead'; Marker='CF_RESCUE_MEMORY PASS mode=3'},
    @{Flag='CFRescueMemoryFinalRead'; Marker='CF_RESCUE_MEMORY PASS mode=4'},
    @{Flag='CFRescueRecallTest'; Marker='CF_RESCUE_RECALL PASS'},
    @{Flag='CFRescueRecallRead'; Marker='CF_RESCUE_RECALL_READ PASS'},
    @{Flag='CFRescueRecallLifecycleTest'; Marker='CF_RESCUE_LIFECYCLE PASS'; RequiredMarkers=@('CF_RESCUE_LIFECYCLE_UNPOSSESS PASS','CF_RESCUE_LIFECYCLE_OWNER PASS')},
    @{Flag='CFSmokeTest'; Marker='CF_SMOKE PASS'},
    @{Flag='CFPaceTest'; Marker='CF_PACE PASS'},
    @{Flag='CFWildSightTest'; Marker='CF_WILD_SIGHT PASS'},
    @{Flag='CFDefenseTest'; Marker='CF_DEFENSE PASS'},
    @{Flag='CFDefenseWrite'; Marker='CF_DEFENSE_WRITE PASS'},
    @{Flag='CFDefenseRead'; Marker='CF_DEFENSE_READ PASS'},
    @{Flag='CFHUDLayoutTest'; Marker='CF_HUD_LAYOUT PASS'},
    @{Flag='CFHollowTest'; Marker='CF_HOLLOW PASS'},
    @{Flag='CFHollowAdventureTest'; Marker='CF_HOLLOW_ADVENTURE PASS'; Timeout=90},
    @{Flag='CFHollowAdventureWrite'; Marker='CF_HOLLOW_ADVENTURE_WRITE PASS'; Timeout=90},
    @{Flag='CFHollowAdventureRead'; Marker='CF_HOLLOW_ADVENTURE_READ PASS'},
    @{Flag='CFHollowRecoveryTest'; Marker='CF_HOLLOW_RECOVERY PASS'; Timeout=195; RequiredMarkers=@('CF_HOLLOW_RECOVERY_PREPARED PASS','CF_HOLLOW_RECOVERY_AVOID PASS','CF_HOLLOW_RECOVERY_DEATH_GATES south=1 near=1 enemyAlive=1 enemyHurt=1 petHurt=1 resident=1 noFocus=1 noWindup=1 noSlow=1')},
    @{Flag='CFHollowRecoveryRead'; Marker='CF_HOLLOW_RECOVERY_READ PASS'},
    @{Flag='CFHollowDownedWrite'; Marker='CF_HOLLOW_DOWNED_WRITE PASS'; Timeout=90; RequiredMarkers=@('CF_HOLLOW_RECOVERY_PREPARED PASS','CF_HOLLOW_RECOVERY_AVOID PASS','CF_HOLLOW_RECOVERY_DEATH_GATES south=1 near=1 enemyAlive=1 enemyHurt=1 petHurt=1 resident=1 noFocus=1 noWindup=1 noSlow=1')},
    @{Flag='CFHollowDownedResume'; Marker='CF_HOLLOW_DOWNED_RESUME PASS'; Timeout=195; PreserveSlot='HollowDownedCross'},
    @{Flag='CFHollowCheckpointRead'; Marker='CF_HOLLOW_CHECKPOINT_READ PASS'},
    @{Flag='CFRescueGatesTest'; Marker='CF_RESCUE_GATES PASS'},
    @{Flag='CFRescueNaturalWrite'; Marker='CF_RESCUE_NATURAL_WRITE PASS'; Timeout=115; Arguments=@('-CFRescueJourney'); RequiredMarkers=@('CF_RESCUE_JOURNEY_SOURCE PASS')},
    @{Flag='CFRescueNaturalResume'; Marker='CF_RESCUE_NATURAL_RESUME PASS'; Timeout=135; PreserveSlot='RescueNaturalDowned'; Arguments=@('-CFRescueJourney'); RequiredMarkers=@('CF_RESCUE_JOURNEY_COMMIT PASS','CF_RESCUE_JOURNEY_RESPONSE PASS')},
    @{Flag='CFRescueNaturalRead'; Marker='CF_RESCUE_NATURAL_READ PASS'; Arguments=@('-CFRescueJourney'); RequiredMarkers=@('CF_RESCUE_JOURNEY_READ PASS')},
    @{Flag='CFHollowRouteTest'; Marker='CF_HOLLOW_ROUTE PASS'},
    @{Flag='CFHollowWrite'; Marker='CF_HOLLOW_WRITE PASS'},
    @{Flag='CFHollowRead'; Marker='CF_HOLLOW_READ PASS'},
    @{Flag='CFBefriendTest'; Marker='CF_BEFRIEND PASS'},
    @{Flag='CFBefriendWrite'; Marker='CF_BEFRIEND_WRITE PASS'},
    @{Flag='CFBefriendRead'; Marker='CF_BEFRIEND_READ PASS'},
    @{Flag='CFInventoryTest'; Marker='CF_INVENTORY PASS'},
    @{Flag='CFStorageTest'; Marker='CF_STORAGE PASS'},
    @{Flag='CFStorageWrite'; Marker='CF_STORAGE_WRITE PASS'},
    @{Flag='CFStorageRead'; Marker='CF_STORAGE_READ PASS'},
    @{Flag='CFCargoTest'; Marker='CF_CARGO PASS'},
    @{Flag='CFCargoTest'; Label='CFStanceCargo'; Arguments=@('-CFStanceCargoTest'); Marker='CF_STANCE_CARGO PASS switched=1'; RequiredMarkers=@('CF_STANCE_CARGO_SWITCH PASS cargo=3 source=1 stock=0 xp=3','CF_CARGO PASS phase=6 loaded=1 paused=1 delivered=1')},
    @{Flag='CFCargoWrite'; Marker='CF_CARGO_WRITE PASS'},
    @{Flag='CFCargoRead'; Marker='CF_CARGO_READ PASS'},
    @{Flag='CFCargoCrystalTest'; Marker='CF_CARGO_CRYSTAL PASS'},
    @{Flag='CFCargoOreTest'; Marker='CF_CARGO_ORE PASS'},
    @{Flag='CFMaterialsTest'; Marker='CF_MATERIALS PASS'},
    @{Flag='CFMaterialsWrite'; Marker='CF_MATERIALS_WRITE PASS'},
    @{Flag='CFMaterialsRead'; Marker='CF_MATERIALS_READ PASS'},
    @{Flag='CFCropTest'; Marker='CF_CROP PASS'},
    @{Flag='CFCropWrite'; Marker='CF_CROP_WRITE PASS'},
    @{Flag='CFCropRead'; Marker='CF_CROP_READ PASS'},
    @{Flag='CFNameTest'; Marker='CF_NAME PASS'},
    @{Flag='CFNameWrite'; Marker='CF_NAME_WRITE PASS'},
    @{Flag='CFNameRead'; Marker='CF_NAME_READ PASS'},
    @{Flag='CFPersonalityTest'; Marker='CF_PERSONALITY PASS'},
    @{Flag='CFPersonalityWrite'; Marker='CF_PERSONALITY_WRITE PASS'},
    @{Flag='CFPersonalityRead'; Marker='CF_PERSONALITY_READ PASS'},
    @{Flag='CFMemoryTest'; Marker='CF_MEMORY PASS'},
    @{Flag='CFMemoryWrite'; Marker='CF_MEMORY_WRITE PASS'},
    @{Flag='CFMemoryRead'; Marker='CF_MEMORY_READ PASS'},
    @{Flag='CFMemoryDoneRead'; Marker='CF_MEMORY_DONE_READ PASS'},
    @{Flag='CFCareTest'; Marker='CF_CARE PASS'},
    @{Flag='CFSpeciesTest'; Marker='CF_SPECIES PASS'},
    @{Flag='CFCombatTest'; Marker='CF_SKILL_TRAIL PASS'},
    @{Flag='CFEncounterTest'; Marker='CF_ENCOUNTER PASS'},
    @{Flag='CFBossTest'; Marker='CF_BOSS PASS'},
    @{Flag='CFBuildingTest'; Marker='CF_BUILD PASS'},
    @{Flag='CFWorldTest'; Marker='CF_WORLD PASS'},
    @{Flag='CFWorldTest'; Label='CFWorldShort1549'; Marker='CF_WORLD PASS'; Arguments=@('-CFRoamSeed=1549');
        SeedMarker='CF_WORLD_SEED 1549'; Pattern='CF_WORLD_ROAM pass=1 travel=(?:[0-2]?\d|3[0-4])\.\d{2} path=\d+\.\d{2} legs=2 continuous=1 within=1'},
    @{Flag='CFWorldTest'; Label='CFWorldShort3028'; Marker='CF_WORLD PASS'; Arguments=@('-CFRoamSeed=3028');
        SeedMarker='CF_WORLD_SEED 3028'; Pattern='CF_WORLD_ROAM pass=1 travel=(?:[0-2]?\d|3[0-4])\.\d{2} path=\d+\.\d{2} legs=2 continuous=1 within=1'},
    @{Flag='CFFarmingTest'; Marker='CF_FARM PASS'},
    @{Flag='CFFeedingTest'; Marker='CF_FEED PASS'},
    @{Flag='CFWorkCycleTest'; Marker='CF_WORK_CYCLE PASS'},
    @{Flag='CFSquadTest'; Marker='CF_SQUAD PASS'},
    @{Flag='CFJournalTest'; Marker='CF_JOURNAL PASS'},
    @{Flag='CFObjectiveTest'; Marker='CF_OBJECTIVE PASS'},
    @{Flag='CFRespawnTest'; Marker='CF_RESPAWN PASS'},
    @{Flag='CFEquipmentTest'; Marker='CF_EQUIPMENT PASS'},
    @{Flag='CFMeleeTest'; Marker='CF_MELEE PASS'},
    @{Flag='CFLogisticsTest'; Marker='CF_LOGISTICS PASS'},
    @{Flag='CFSessionTest'; Marker='CF_SESSION PASS'},
    @{Flag='CFBackpackTest'; Marker='CF_BACKPACK PASS'},
    @{Flag='CFAnimationTest'; Marker='CF_ANIMATION PASS'},
    @{Flag='CFAffinityTest'; Marker='CF_AFFINITY PASS'},
    @{Flag='CFAffectionTest'; Marker='CF_AFFECTION PASS'},
    @{Flag='CFFormationTest'; Marker='CF_FORMATION PASS'; Repeats=3},
    @{Flag='CFResourceTest'; Marker='CF_RESOURCE PASS'},
    @{Flag='CFFieldTest'; Marker='CF_FIELD PASS'},
    @{Flag='CFFieldWrite'; Marker='CF_FIELD_WRITE PASS'},
    @{Flag='CFFieldRead'; Marker='CF_FIELD_READ PASS'},
    @{Flag='CFFieldRecoveryTest'; Marker='CF_FIELD_RECOVERY PASS'},
    @{Flag='CFFieldRecoveryWrite'; Marker='CF_FIELD_RECOVERY_WRITE PASS'},
    @{Flag='CFFieldRecoveryRead'; Marker='CF_FIELD_RECOVERY_READ PASS'},
    @{Flag='CFHomeTest'; Marker='CF_HOME PASS'},
    @{Flag='CFHomeWrite'; Marker='CF_HOME_WRITE PASS'},
    @{Flag='CFHomeRead'; Marker='CF_HOME_READ PASS'},
    @{Flag='CFExpeditionTest'; Marker='CF_EXPEDITION PASS'},
    @{Flag='CFExpeditionWrite'; Marker='CF_EXPEDITION_WRITE PASS'},
    @{Flag='CFExpeditionRead'; Marker='CF_EXPEDITION_READ PASS'},
    @{Flag='CFSaveWrite'; Marker='CF_SAVE_WRITE PASS'},
    @{Flag='CFSaveRead'; Marker='CF_SAVE_READ PASS'},
    @{Flag='CFNavTest'; Marker='CF_NAV_WORK PASS'}
)
foreach ($case in $cases) {
    $repeatCount = if ($case.Repeats) { [int]$case.Repeats } else { 1 }
    for ($attempt = 1; $attempt -le $repeatCount; $attempt++) {
        $caseName = if ($case.Label) { $case.Label } else { $case.Flag }
        $label = if ($repeatCount -gt 1) { "$caseName-$attempt" } else { $caseName }
        $logPath = Join-Path $testLogRoot ($label + '.log')
        $arguments = @('-nullrhi', '-nosound', '-unattended', '-nop4', "-$($case.Flag)", "-abslog=`"$logPath`"")
        if ($case.Arguments) { $arguments += $case.Arguments }
        $preservedPath=if($case.PreserveSlot){Join-Path $BuildRoot ('CreatureFrontier\Saved\SaveGames\'+$case.PreserveSlot+'.sav')}else{''}
        $preservedHash=if($preservedPath){(Get-FileHash -LiteralPath $preservedPath).Hash}else{''}
        $testProcess = Start-Process -FilePath $exePath -WorkingDirectory $BuildRoot `
            -ArgumentList $arguments `
            -WindowStyle Hidden -PassThru
        $caseTimeout=if($case.Timeout){[int]$case.Timeout}else{$TimeoutSeconds}
        if (-not $testProcess.WaitForExit($caseTimeout * 1000)) {
            $testProcess.Kill()
            throw "$label 超过 $caseTimeout 秒，已停止测试进程。详见：$logPath"
        }
        if ($testProcess.ExitCode -ne 0) { throw "$label 测试失败，退出码：$($testProcess.ExitCode)" }
        if ((Get-FrontierBuildFingerprint -LiteralPath $exePath) -ne $buildHash) { throw '完整回归期间测试程序发生变化' }
        if (Select-String -LiteralPath $logPath -Pattern 'CF_\w+ FAIL|Fatal error|Assertion failed' -Quiet) {
            throw "$label 日志包含失败标记。详见：$logPath"
        }
        if (-not (Select-String -LiteralPath $logPath -SimpleMatch $case.Marker -Quiet)) {
            throw "$label 未输出必要成功标记。详见：$logPath"
        }
        if ($case.SeedMarker -and -not (Select-String -LiteralPath $logPath -SimpleMatch $case.SeedMarker -Quiet)) {
            throw "$label 未使用指定的回放种子。详见：$logPath"
        }
        if ($case.Pattern -and -not (Select-String -LiteralPath $logPath -Pattern $case.Pattern -Quiet)) {
            throw "$label 未实际完成两段短程巡游。详见：$logPath"
        }
        foreach($requiredMarker in $case.RequiredMarkers){if(!(Select-String -LiteralPath $logPath -SimpleMatch $requiredMarker -Quiet)){throw "缺少专项必要标记：$requiredMarker；$logPath"}}
        if($preservedPath -and (Get-FileHash -LiteralPath $preservedPath).Hash -ne $preservedHash){throw "倒地输入存档被改写：$preservedPath"}
        Write-Host "${label}：通过"
    }
}
