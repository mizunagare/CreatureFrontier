#include "CFPlayerCharacter.h"
#include "FrontierChinese.h"
#include "Camera/CameraComponent.h"
#include "CaptureComponent.h"
#include "Components/CapsuleComponent.h"
#include "CreatureCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "EngineUtils.h"
#include "WorkStation.h"
#include "BaseBuildingComponent.h"
#include "FrontierSaveGame.h"
#include "CreatureDefinition.h"
#include "InventoryComponent.h"
#include "WildEncounter.h"
#include "WorldLoot.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "PlayerEquipmentComponent.h"
#include "ResourceNode.h"
#include "ResourcePersistence.h"
#include "HomeBuildingComponent.h"
#include "HomeLayout.h"
#include "HomePersistence.h"
#include "StorageInteractionComponent.h"
#include "CompanionCargoComponent.h"
#include "FarmCropComponent.h"
#include "CompanionNamingComponent.h"
#include "CompanionPersonalityComponent.h"
#include "CompanionMemoryComponent.h"
#include "HomeModule.h"
#include "ResearchComponent.h"

void ACFPlayerCharacter::CraftSpear() { Equipment->CraftSpear(); }
void ACFPlayerCharacter::InteractGarden() {if(auto* Site=TraceFacility()) Site->Crop->Interact(this,0);}
void ACFPlayerCharacter::GardenOrSpecial()
{
    if(auto* Site=TraceFacility();Site && Site->RequiredSpecialty==EWorkSpecialty::Farming) {Site->Crop->Interact(this,0);return;}
    CommandSpecial();
}
void ACFPlayerCharacter::DepositGardenSeed() {if(auto* Site=TraceFacility()) Site->Crop->Interact(this,1);}
void ACFPlayerCharacter::WithdrawGardenSeed() {if(auto* Site=TraceFacility()) Site->Crop->Interact(this,2);}
void ACFPlayerCharacter::CraftVest() { Equipment->CraftVest(); }
void ACFPlayerCharacter::ToggleSpear() { Equipment->ToggleSpear(); }
void ACFPlayerCharacter::ToggleVest() { Equipment->ToggleVest(); }
float ACFPlayerCharacter::GetPlayerAttackDamage() const { return AttackDamage+Equipment->GetAttackBonus(); }

void ACFPlayerCharacter::ToggleBuilding() { Building->Toggle(); }
void ACFPlayerCharacter::CycleBuilding()
{
    if(Building->IsActive()) { Building->Cycle(); return; }
    if(!HasAuthority() || Health<=0 || UGameplayStatics::IsGamePaused(this)) return;
    if(auto* Crate=Storage->TraceCrate())
    {
        DeliverySelection=Crate; bDeliverySelection=true; int32 Applied=0;
        for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It) if(IsCommandTarget(*It) && It->Cargo->Assign(Crate,this)) ++Applied;
        Feedback=FString::Printf(TEXT("已选运输目标；E 指派的资源将运到这里 | 已关联%d项工作 | 对空按V清除后续目标"),Applied);
    }
    else { DeliverySelection.Reset(); bDeliverySelection=false; Feedback=TEXT("后续采集直接放入背包；F 取消已有运输工作"); }
}
void ACFPlayerCharacter::DismantleBuilding() { Building->Dismantle(); }

void ACFPlayerCharacter::QuitGame()
{
    UKismetSystemLibrary::QuitGame(this, Cast<APlayerController>(GetController()), EQuitPreference::Quit, false);
}

bool ACFPlayerCharacter::IsNearWorkStation() const
{
    for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It)
        if (It->RequiredSpecialty != EWorkSpecialty::Feeding && GetDistanceTo(*It) <= 800.0f) return true;
    return false;
}

void ACFPlayerCharacter::CraftCaptureDevice()
{
    CraftRecipe(TEXT("Capture"));
}

void ACFPlayerCharacter::CraftMedicine()
{
    CraftRecipe(TEXT("Medicine"));
}

void ACFPlayerCharacter::UseMedicine()
{
    if (Health <= 0 || Health >= 100 || !HasAuthority()) { Feedback = TEXT("仅受伤且存活时可用药品"); return; }
    if (!Inventory->Consume(EFrontierItem::Medicine, 1)) { Feedback = TEXT("没有药品，请在工作台制作"); return; }
    Health = FMath::Min(100.0f, Health + 40.0f);
    Feedback = TEXT("已用药品，最多恢复40生命");
}

bool ACFPlayerCharacter::SaveProgress(const FString& Slot)
{
    if (!HasAuthority() || !GetController()) return false;
    auto* Save = Cast<UFrontierSaveGame>(UGameplayStatics::CreateSaveGameObject(UFrontierSaveGame::StaticClass()));
    Save->FormatVersion=28;
    Save->MapName = UGameplayStatics::GetCurrentLevelName(this, true);
    Save->PlayerTransform = GetActorTransform();
    Save->ViewRotation = GetController()->GetControlRotation();
    Save->Health = Health;
    Save->Resources = Inventory->Count(EFrontierItem::Material);
    Save->InventoryStacks = Inventory->GetStacks();
    Save->Journal = Journal;
    Save->Objectives = Objectives;
    Save->Equipment=Equipment->GetState();
    Save->HomeModules=HomeLayout::Collect(GetWorld());
    Save->Research=Research->GetProgress();
    for(TActorIterator<AResourceNode> It(GetWorld());It;++It)
    {
        FResourceSaveRecord Record;
        Record.Id=It->PersistentId; Record.Definition=It->Definition.Get(); Record.Transform=It->GetActorTransform();
        Record.Remaining=It->Remaining; Record.HarvestCooldown=It->HarvestCooldown; Record.RegrowRemaining=It->RegrowRemaining;
        Save->ResourceNodes.Add(Record);
    }
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
    {
        FCreatureSaveRecord Record;
        Record.Id=It->PersistentId;
        Record.Nickname=It->Nickname;
        Record.CreatureClass = It->GetClass();
        Record.Definition = It->Definition.Get();
        Record.Transform = It->GetActorTransform();
        Record.Health = It->Health;
        Record.Level = It->Level;
        Record.Experience = It->Experience;
        Record.Stamina = It->Stamina;
        Record.Nutrition = It->Nutrition;
        Record.AutoFeedCooldown = It->AutoFeedCooldown;
        Record.bResumeWorkAfterRest = It->bResumeWorkAfterRest;
        Record.SpecialCooldown = It->SpecialCooldownRemaining;
        Record.EncounterId = It->EncounterId;
        Record.bAlpha = It->bAlpha;
        Record.AlphaCooldown = It->AlphaCooldown;
        Record.bHasHabitat = It->bHasHabitat; Record.HabitatHome = It->HabitatHome;
        Record.RoamRadius = It->RoamRadius; Record.LeashRadius = It->LeashRadius;
        Record.bOwned = It->Tamer == GetController();
        Record.CarriedFood=It->CarriedFood;
        Record.BondPoints=It->BondPoints; Record.PettingCooldown=It->PettingCooldown;
        It->Personality->EnsurePreference();
        Record.Preference=It->Personality->GetPreference();Record.bPreferenceDiscovered=It->Personality->IsDiscovered();
        Record.CrystalJourney=static_cast<uint8>(It->Memory->GetMemory());
        Record.Disposition = It->Disposition;
        Record.Activity = It->Activity;
        Record.WorkSiteName = IsValid(It->WorkSite) ? It->WorkSite->GetFName() : NAME_None;
        Record.WorkSiteId = IsValid(It->WorkSite) && It->WorkSite->bPlayerBuilt ? It->WorkSite->PersistentId : FGuid();
        Record.FieldResourceId=It->GetFieldResource()?It->GetFieldResource()->PersistentId:FGuid();
        It->Cargo->WriteSnapshot(Record);
        Save->Creatures.Add(Record);
    }
    for (TActorIterator<AWildEncounter> It(GetWorld()); It; ++It)
        Save->EncounterTimers.Add(It->EncounterId, It->GetSavedRemaining());
    for (TActorIterator<AWorldLoot> It(GetWorld()); It; ++It)
    {
        FLootSaveRecord Record;
        Record.Transform = It->GetActorTransform();
        Record.Materials = It->Materials;
        Record.Medicine = It->Medicine;
        Save->Loot.Add(Record);
    }
    for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It)
        if (It->bPlayerBuilt)
        {
            FFacilitySaveRecord Record;
            Record.Transform = It->GetActorTransform(); Record.Id = It->PersistentId; Record.Specialty = It->RequiredSpecialty;
            Record.StoredFood = It->StoredFood;
            Record.Seeds=It->Crop->Seeds; Record.bPlanted=It->Crop->bPlanted; Record.GrowthWork=It->Crop->GrowthWork;
            Save->Facilities.Add(Record);
        }
    const bool bSaved = UGameplayStatics::SaveGameToSlot(Save, Slot, 0);
    Feedback = bSaved ? TEXT("保存成功，F9 读档") : TEXT("保存失败");
    return bSaved;
}

bool ACFPlayerCharacter::LoadProgress(const FString& Slot)
{
    if (!HasAuthority() || !GetController()) return false;
    auto* Save = Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
    if (!Save || Save->FormatVersion < 1 || Save->FormatVersion > 28 ||
        Save->MapName != UGameplayStatics::GetCurrentLevelName(this, true) ||
        !FMath::IsFinite(Save->Health) || Save->Health < 0 || Save->Health > 100 ||
        Save->Resources < 0 || Save->PlayerTransform.ContainsNaN() || Save->ViewRotation.ContainsNaN())
    {
        Feedback = TEXT("没有适用于当前地图的存档");
        return false;
    }
    if (Save->FormatVersion == 1)
    {
        Save->InventoryStacks = {{EFrontierItem::Material, Save->Resources}, {EFrontierItem::CaptureDevice, 5}, {EFrontierItem::Medicine, 1}};
    }
    if(Save->FormatVersion<14) Save->Equipment=FPlayerEquipmentState();
    if(!UPlayerEquipmentComponent::Validate(Save->Equipment)) { Feedback=TEXT("存档中的装备数据无效"); return false; }
    if (Save->FormatVersion < 9) Save->InventoryStacks.FindOrAdd(EFrontierItem::Food) = 3;
    if(Save->FormatVersion<19)
    {
        Save->Research=FResearchProgress();
        for(const auto& Entry:Save->InventoryStacks) if(Entry.Key>=EFrontierItem::Crystal && Entry.Value>0) { Feedback=TEXT("旧存档含未知物品"); return false; }
    }
    if(!UResearchComponent::Validate(Save->Research)) { Feedback=TEXT("存档中的研究前置条件无效"); return false; }
    if(Save->FormatVersion<23)
        for(const auto& Entry:Save->InventoryStacks) if(Entry.Key>=EFrontierItem::Fiber && Entry.Value>0)
        { Feedback=TEXT("旧存档含未知分类材料"); return false; }
    if(Save->FormatVersion<24 && Save->InventoryStacks.FindRef(EFrontierItem::Seeds)>0)
    {Feedback=TEXT("旧存档背包含未知种子");return false;}
    if (!UInventoryComponent::Validate(Save->InventoryStacks))
    {
        Feedback = TEXT("存档中的背包数据无效");
        return false;
    }
    TArray<TObjectPtr<UCreatureDefinition>> LoadedJournalAssets;
    TSet<FSoftObjectPath> JournalPaths;
    if (Save->FormatVersion < 12) Save->Journal.Reset();
    if (Save->Journal.Num()>128) { Feedback=TEXT("存档中的图鉴数量无效"); return false; }
    for (const auto& Entry : Save->Journal)
    {
        auto* Definition=Entry.Definition.LoadSynchronous();
        const auto Path=Entry.Definition.ToSoftObjectPath();
        if (!Definition || JournalPaths.Contains(Path)) { Feedback=TEXT("存档中的图鉴条目无效"); return false; }
        JournalPaths.Add(Path); LoadedJournalAssets.Add(Definition);
    }
    if(Save->FormatVersion<13) Save->Objectives=FFrontierObjectiveProgress();
    const auto& ObjectiveSave=Save->Objectives;
    bool SeenProof=!Save->Journal.IsEmpty(), CaptureProof=false;
    for(const auto& Entry:Save->Journal) CaptureProof |= Entry.bCaptured;
    for(const auto& Record:Save->Creatures) if(Record.bOwned && !Record.Definition.IsNull()) { SeenProof=true; CaptureProof=true; }
    if(ObjectiveSave.Claimed<0 || ObjectiveSave.Claimed>5 || ObjectiveSave.WorkMaterials<0 || ObjectiveSave.WorkMaterials>3 || ObjectiveSave.FarmFood<0 || ObjectiveSave.FarmFood>2 ||
        (ObjectiveSave.Claimed>=1 && !SeenProof) || (ObjectiveSave.Claimed>=2 && !CaptureProof) ||
        (ObjectiveSave.Claimed>=3 && ObjectiveSave.WorkMaterials<3) || (ObjectiveSave.Claimed>=4 && ObjectiveSave.FarmFood<2) || (ObjectiveSave.Claimed>=5 && !ObjectiveSave.bAlphaResolved))
    { Feedback=TEXT("存档中的旅程进度无效"); return false; }
    for (const auto& Entry : Save->EncounterTimers)
        if (Entry.Key.IsNone() || !FMath::IsFinite(Entry.Value) || Entry.Value < 0 || Entry.Value > 600)
        { Feedback = TEXT("存档中的遭遇计时无效"); return false; }
    for (const auto& Record : Save->Loot)
        if (Record.Transform.ContainsNaN() || Record.Materials < 0 || Record.Materials > 999 || Record.Medicine < 0 || Record.Medicine > 999)
        { Feedback = TEXT("存档中的掉落物无效"); return false; }
    TSet<FGuid> FacilityIds;
    if (Save->Facilities.Num() > 20) { Feedback = TEXT("存档中的设施数量无效"); return false; }
    for (auto& Record : Save->Facilities)
    {
        if(Save->FormatVersion<24) {Record.Seeds=0;Record.bPlanted=false;Record.GrowthWork=0;}
        if (!UFarmCropComponent::Validate(Record) || !Record.Id.IsValid() || FacilityIds.Contains(Record.Id) || UBaseBuildingComponent::GetCost(Record.Specialty) <= 0 ||
            !UBaseBuildingComponent::IsBuildTransformValid(Record.Transform) || Record.StoredFood < 0 || Record.StoredFood > 30 ||
            (Record.Specialty != EWorkSpecialty::Feeding && Record.Specialty != EWorkSpecialty::Farming && Record.StoredFood != 0) ||
            (Save->FormatVersion<15 && ((Record.Specialty==EWorkSpecialty::Farming && Record.StoredFood!=0) || Record.Specialty==EWorkSpecialty::Transport)))
        { Feedback = TEXT("存档中的设施无效"); return false; }
        FacilityIds.Add(Record.Id);
    }
    FResourceRestoreTransaction ResourceRestore(GetWorld());
    if(!ResourceRestore.Stage(Save)) { Feedback=TEXT("资源快照无效或不可用，世界未改变"); return false; }
    TSet<FGuid> CreatureIds;
    for(auto& Record:Save->Creatures)
    {
        if(Save->FormatVersion<25) Record.Nickname.Empty();
        if(!UCompanionNamingComponent::IsValidName(Record.Nickname) || (!Record.Nickname.IsEmpty() && (!Record.bOwned || Record.Disposition!=ECreatureDisposition::Tamed)))
        {Feedback=TEXT("伙伴昵称无效，世界未改变");return false;}
        if(Save->FormatVersion<22) { Record.Id=FGuid::NewGuid(); Record.CargoItems.Reset(); Record.DeliveryCrateId.Invalidate(); Record.bReturningCargo=false; }
        if(!Record.Id.IsValid() || CreatureIds.Contains(Record.Id)) { Feedback=TEXT("生物身份无效或重复，世界未改变"); return false; }
        const bool OwnedTamed=Record.bOwned && Record.Disposition==ECreatureDisposition::Tamed;
        if(Save->FormatVersion<26)
        {Record.Preference=OwnedTamed?UCompanionPersonalityComponent::PreferenceForId(Record.Id):ECompanionPreference::None;Record.bPreferenceDiscovered=false;}
        if(!UCompanionPersonalityComponent::ValidateSnapshot(Record.Preference,Record.bPreferenceDiscovered,OwnedTamed))
        {Feedback=TEXT("伙伴偏好无效，世界未改变");return false;}
        if(Save->FormatVersion<27)Record.CrystalJourney=0;
        if(!UCompanionMemoryComponent::ValidateSnapshot(static_cast<ECrystalJourneyMemory>(Record.CrystalJourney),OwnedTamed,Save->Research.bCrystalSample))
        {Feedback=TEXT("伙伴回忆无效，世界未改变");return false;}
        CreatureIds.Add(Record.Id);
        if(Save->FormatVersion<20) Record.FieldResourceId.Invalidate();
        if(Record.FieldResourceId.IsValid() && (!ResourceRestore.Find(Record.FieldResourceId) || !Record.bOwned || Record.Disposition!=ECreatureDisposition::Tamed || Record.Health<=0 ||
            Record.WorkSiteId.IsValid() || !Record.WorkSiteName.IsNone() ||
            (Record.Activity!=ECreatureActivity::Work && !(Record.Activity==ECreatureActivity::Rest && Record.bResumeWorkAfterRest))))
        { Feedback=TEXT("野外工作目标无效，世界未改变"); return false; }
    }
    FHomeRestoreTransaction HomeRestore(GetWorld());
    if(!HomeRestore.Stage(Save)) { Feedback=TEXT("房屋结构或室内设施支撑无效，世界未改变"); return false; }
    for(const auto& Record:Save->Creatures)
        if(!UCompanionCargoComponent::ValidateSnapshot(Record,HomeRestore.Find(Record.DeliveryCrateId)))
        { Feedback=TEXT("携带资源或运输意图无效，世界未改变"); return false; }
    TArray<ACreatureCharacter*> OldCreatures;
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It) OldCreatures.Add(*It);
    TArray<ACreatureCharacter*> Restored;
    // Stage every actor before replacing the live world; a missing asset leaves it intact.
    for (const auto& Record : Save->Creatures)
    {
        UClass* CreatureClass = Record.CreatureClass.LoadSynchronous();
        UCreatureDefinition* LoadedDefinition = Record.Definition.LoadSynchronous();
        if (!CreatureClass || CreatureClass->HasAnyClassFlags(CLASS_Abstract) ||
            (Save->FormatVersion>=15 && (Record.CarriedFood<0 || Record.CarriedFood>3 || (Record.CarriedFood>0 && (!Record.bOwned || Record.Disposition!=ECreatureDisposition::Tamed)))) ||
            (Save->FormatVersion>=16 && (Record.BondPoints<0 || Record.BondPoints>100 || !FMath::IsFinite(Record.PettingCooldown) || Record.PettingCooldown<0 || Record.PettingCooldown>30 ||
                ((Record.BondPoints>0 || Record.PettingCooldown>0) && (!Record.bOwned || Record.Disposition!=ECreatureDisposition::Tamed)))) ||
            (!Record.Definition.IsNull() && !LoadedDefinition) || Record.Transform.ContainsNaN() ||
            !FMath::IsFinite(Record.Health) || Record.Health < 0 ||
            Record.Level < 1 || Record.Level > 20 || Record.Experience < 0 ||
            (Record.Level < 20 ? Record.Experience >= Record.Level * 100 : Record.Experience != 0) ||
            !FMath::IsFinite(Record.Stamina) || Record.Stamina < 0 || Record.Stamina > 100 ||
            !FMath::IsFinite(Record.Nutrition) || Record.Nutrition < 0 || Record.Nutrition > 100 ||
            !FMath::IsFinite(Record.AutoFeedCooldown) || Record.AutoFeedCooldown < 0 || Record.AutoFeedCooldown > 10 ||
            !FMath::IsFinite(Record.SpecialCooldown) || Record.SpecialCooldown < 0 || Record.SpecialCooldown > 60 ||
            !FMath::IsFinite(Record.AlphaCooldown) || Record.AlphaCooldown < 0 || Record.AlphaCooldown > 10 ||
            Record.HabitatHome.ContainsNaN() || !FMath::IsFinite(Record.RoamRadius) || Record.RoamRadius < 0 || Record.RoamRadius > 600 ||
            !FMath::IsFinite(Record.LeashRadius) || Record.LeashRadius < 600 || Record.LeashRadius > 1500)
        {
            for (auto* Pet : Restored) Pet->Destroy();
            Feedback = TEXT("存档引用的生物数据无效或不可用");
            return false;
        }
        auto* Pet = GetWorld()->SpawnActorDeferred<ACreatureCharacter>(CreatureClass, Record.Transform,
            nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Pet)
        {
            for (auto* Staged : Restored) Staged->Destroy();
            Feedback = TEXT("无法恢复生物");
            return false;
        }
        Pet->PersistentId=Record.Id;
        Pet->Nickname=Record.Nickname;
        Pet->Personality->RestoreSnapshot(Record.Preference,Record.bPreferenceDiscovered);
        Pet->Memory->RestoreSnapshot(static_cast<ECrystalJourneyMemory>(Record.CrystalJourney));
        Pet->Definition = LoadedDefinition;
        Pet->EncounterId = Record.EncounterId;
        Pet->Level = Record.Level;
        Pet->bAlpha = Record.bAlpha;
        if (Save->FormatVersion < 6 && !Record.EncounterId.IsNone())
            for (TActorIterator<AWildEncounter> It(GetWorld()); It; ++It)
                if (It->EncounterId == Record.EncounterId) { Pet->bAlpha = It->bBoss; break; }
        Pet->FinishSpawning(Record.Transform);
        Pet->Health = FMath::Min(Pet->GetMaxHealth(), Record.Health);
        Pet->Experience = Record.Experience;
        Pet->Stamina = Record.Stamina;
        Pet->Nutrition = Record.Nutrition;
        Pet->AutoFeedCooldown = Record.AutoFeedCooldown;
        Pet->SpecialCooldownRemaining = Record.SpecialCooldown;
        Pet->AlphaCooldown = Record.AlphaCooldown;
        Pet->bHasHabitat = Record.bHasHabitat && !Record.bOwned;
        Pet->HabitatHome = Record.HabitatHome; Pet->RoamRadius = Record.RoamRadius; Pet->LeashRadius = Record.LeashRadius;
        if (Save->FormatVersion < 8 && !Record.bOwned && !Record.EncounterId.IsNone())
            for (TActorIterator<AWildEncounter> It(GetWorld()); It; ++It)
                if (It->EncounterId == Record.EncounterId) { Pet->bHasHabitat = true; Pet->HabitatHome = It->GetActorLocation()+FVector(0,0,90); break; }
        Pet->Disposition = Record.Disposition;
        Pet->Tamer = Record.bOwned ? GetController() : nullptr;
        Pet->CarriedFood=Save->FormatVersion>=15?Record.CarriedFood:0;
        Pet->BondPoints=Save->FormatVersion>=16?Record.BondPoints:0;
        Pet->PettingCooldown=Save->FormatVersion>=16?Record.PettingCooldown:0;
        Pet->Activity = Record.Activity;
        for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It)
            if (!Record.WorkSiteId.IsValid() && !It->bPlayerBuilt && It->GetFName() == Record.WorkSiteName) { Pet->WorkSite = *It; break; }
        Restored.Add(Pet);
    }
    TArray<AWorldLoot*> OldLoot, RestoredLoot;
    for (TActorIterator<AWorldLoot> It(GetWorld()); It; ++It) OldLoot.Add(*It);
    for (const auto& Record : Save->Loot)
    {
        auto* Drop = GetWorld()->SpawnActor<AWorldLoot>(AWorldLoot::StaticClass(), Record.Transform);
        if (!Drop)
        {
            for (auto* Staged : Restored) Staged->Destroy();
            for (auto* Staged : RestoredLoot) Staged->Destroy();
            Feedback = TEXT("无法恢复掉落物");
            return false;
        }
        Drop->Materials = Record.Materials;
        Drop->Medicine = Record.Medicine;
        RestoredLoot.Add(Drop);
    }
    TArray<AWorkStation*> OldBuilt, RestoredBuilt;
    for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It) if (It->bPlayerBuilt) OldBuilt.Add(*It);
    for (const auto& Record : Save->Facilities)
    {
        auto* Site = GetWorld()->SpawnActorDeferred<AWorkStation>(AWorkStation::StaticClass(),Record.Transform,nullptr,nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn,ESpawnActorScaleMethod::OverrideRootScale);
        if (!Site)
        {
            for (auto* Staged : Restored) Staged->Destroy();
            for (auto* Staged : RestoredLoot) Staged->Destroy();
            for (auto* Staged : RestoredBuilt) Staged->Destroy();
            Feedback = TEXT("无法恢复设施"); return false;
        }
        Site->bPlayerBuilt = true; Site->PersistentId = Record.Id; Site->RequiredSpecialty = Record.Specialty;
        Site->StoredFood = Record.StoredFood;
        Site->Crop->Seeds=Record.Seeds; Site->Crop->bPlanted=Record.bPlanted; Site->Crop->GrowthWork=Record.GrowthWork;
        Site->FinishSpawning(Record.Transform);
        RestoredBuilt.Add(Site);
    }
    for (int32 Index=0; Index<Restored.Num(); ++Index)
    {
        auto* Pet = Restored[Index];
        const auto& Record = Save->Creatures[Index];
        if(Record.FieldResourceId.IsValid())
        {
            Pet->AssignFieldResource(ResourceRestore.Find(Record.FieldResourceId));
            Pet->Activity=Record.Activity;
        }
        if (Record.WorkSiteId.IsValid())
            for (auto* Site : RestoredBuilt) if (Site->PersistentId == Record.WorkSiteId) { Pet->WorkSite = Site; break; }
        if (Pet->Activity == ECreatureActivity::Work && !IsValid(Pet->WorkSite) && !Pet->GetFieldResource())
            Pet->Activity = Record.bOwned ? ECreatureActivity::Follow : ECreatureActivity::Idle;
        Pet->bResumeWorkAfterRest = Record.bResumeWorkAfterRest && Record.bOwned && Pet->Health > 0 && Pet->Activity == ECreatureActivity::Rest &&
            (Pet->GetFieldResource() || (IsValid(Pet->WorkSite) && Pet->WorkSite->RequiredSpecialty != EWorkSpecialty::Feeding));
        Pet->Cargo->RestoreSnapshot(Record,HomeRestore.Find(Record.DeliveryCrateId));
    }
    UCompanionMemoryComponent::CloseAll(GetWorld());Naming->Close(); CancelMeleeAttack(); Building->Cancel();
    ResourceRestore.Commit();
    HomeRestore.Commit();
    for (auto* Site : OldBuilt) Site->Destroy();
    for (auto* Drop : OldLoot) Drop->Destroy();
    for (auto* Pet : OldCreatures) Pet->Destroy();
    for (TActorIterator<AWildEncounter> It(GetWorld()); It; ++It)
        It->RestorePopulation(Save->EncounterTimers.FindRef(It->EncounterId));
    SetActorTransform(Save->PlayerTransform, false, nullptr, ETeleportType::TeleportPhysics);
    GetController()->SetControlRotation(Save->ViewRotation);
    GetCharacterMovement()->StopMovementImmediately();
    Health = Save->Health;
    RespawnProtectionUntil=0;
    Equipment->Restore(Save->Equipment);
    Inventory->Restore(Save->InventoryStacks);
    Research->Restore(Save->Research); CaptureComponent->ResetSelection();
    GetCharacterMovement()->SetMovementMode(Health > 0 ? MOVE_Walking : MOVE_None);
    SelectedCompanion.Reset();
    RosterHighlight.Reset(); bBackpackOpen=false; BackpackRecipe=0; Storage->Close(); DeliverySelection.Reset(); bDeliverySelection=false;
    Journal=Save->Journal; JournalAssets=LoadedJournalAssets; JournalPage=0;
    Objectives=Save->Objectives;
    // Owned survivors in old snapshots prove capture; unseen wild snapshots do not prove discovery.
    for (auto* Pet : Restored)
        if (Pet->Tamer==GetController()) { RecordJournal(Pet->Definition,true); RecordAlphaResolution(Pet); }
    Feedback = bSingleCompanionMode ? TEXT("读档成功，请按L重选伙伴或按K指挥全队；此前不执行命令") : TEXT("读档成功，当前指挥全队；L 选择单个伙伴");
    return true;
}

void ACFPlayerCharacter::RestartLevel()
{
    UGameplayStatics::OpenLevel(this, FName(*GetWorld()->GetName()));
}

void ACFPlayerCharacter::BeginPlay()
{
    Super::BeginPlay();
    // Starter supplies belong to a new player, never to a generic container.
    Inventory->Restore({{EFrontierItem::Material,6},{EFrontierItem::CaptureDevice,5},{EFrontierItem::Medicine,1},{EFrontierItem::Food,3}});
    InitializeRespawnAnchor();
    CaptureComponent->OnCaptureResolved.AddDynamic(this, &ACFPlayerCharacter::CaptureResolved);
    GetWorldTimerManager().SetTimer(ObservationTimer,this,&ACFPlayerCharacter::ObserveAimedCreature,.4f,true);
}

float ACFPlayerCharacter::TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
    if (!HasAuthority() || Health <= 0.0f || !FMath::IsFinite(Amount) || Amount<=0) return 0.0f;
    if(GetRespawnProtectionRemaining()>0) { Feedback=TEXT("基地复活保护中，主动攻击会结束保护"); return 0; }
    const float Applied = FMath::Min(Health, Amount*(1-Equipment->GetDamageReduction()));
    Health -= Applied;
    Feedback = Health <= 0.0f ? TEXT("已倒下，R 回基地复活并保留世界 | F9 读档") : TEXT("受到伤害！");
    if (Health <= 0.0f)
    {
        UCompanionMemoryComponent::CloseAll(GetWorld());
        Naming->Close();
        Storage->Close();
        DeliverySelection.Reset(); bDeliverySelection=false;
        GetCharacterMovement()->StopMovementImmediately(); GetCharacterMovement()->DisableMovement(); StopJumping();
        CancelMeleeAttack();
        Building->Cancel(); bRosterOpen=false; bJournalOpen=false; bBackpackOpen=false;
        Equipment->RefreshVisual();
        for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It) It->HandlePlayerDowned(this);
    }
    return Applied;
}

void ACFPlayerCharacter::CaptureResolved(ACreatureCharacter* Creature, bool bSucceeded)
{
    if (HasAuthority() && IsValid(Creature) && Health>0)
    {
        RespawnProtectionUntil=0;
        RecordJournal(Creature->Definition,bSucceeded && Creature->Disposition==ECreatureDisposition::Tamed && Creature->Tamer==GetController());
        if(bSucceeded) RecordAlphaResolution(Creature);
    }
    Feedback = bSucceeded ? TEXT("捕捉成功！F 跟随 | E 指派工作") : TEXT("捕捉失败，削弱目标后再试");
}

void ACFPlayerCharacter::CommandFollow()
{
    if (Health <= 0.0f) return;
    int32 Count = 0;
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (IsCommandTarget(*It)) { It->ClearCombatOrders(); It->WorkSite = nullptr; It->SetActivity(ECreatureActivity::Follow); ++Count; }
    Feedback = FString::Printf(TEXT("%d 位受指挥伙伴正在跟随 | %s"),Count,*GetCommandScopeText());
}

void ACFPlayerCharacter::CommandFocus()
{
    if (!HasAuthority() || Health <= 0) return;
    RespawnProtectionUntil=0;
    auto* Target = TraceCreature();
    if (!Target || Target->Health <= 0 || Target->Disposition == ECreatureDisposition::Tamed)
    { Feedback = TEXT("瞄准存活的野生生物以指挥集火"); return; }
    int32 Count = 0;
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (IsCommandTarget(*It) && It->GetDistanceTo(this) <= 1800 && It->OrderAttack(Target)) ++Count;
    Feedback = FString::Printf(TEXT("集火%s：%d位可行动伙伴"), *Target->GetCreatureName(), Count);
}

void ACFPlayerCharacter::CommandSpecial()
{
    if (!HasAuthority() || Health <= 0) return;
    RespawnProtectionUntil=0;
    auto* Aimed = TraceCreature();
    ACreatureCharacter* BestPet = nullptr;
    ACreatureCharacter* BestTarget = nullptr;
    float BestDistance = TNumericLimits<float>::Max();
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
    {
        if (!IsCommandTarget(*It) || !It->IsSpecialReady() || It->GetDistanceTo(this) > 1800) continue;
        auto* Target = IsValid(Aimed) && Aimed->Health > 0 && Aimed->Disposition != ECreatureDisposition::Tamed ? Aimed : It->FocusTarget.Get();
        if (!IsValid(Target) || Target->Health <= 0 || Target->Disposition == ECreatureDisposition::Tamed || Target->GetDistanceTo(this) > 1500) continue;
        const float Distance = It->GetDistanceTo(Target);
        if (Distance < BestDistance) { BestDistance = Distance; BestPet = *It; BestTarget = Target; }
    }
    if (BestPet && BestPet->RequestSpecial(BestTarget)) Feedback = BestPet->GetCreatureName() + TEXT("正前往技能范围内");
    else Feedback = TEXT("需要可行动伙伴、目标、20体力且技能冷却结束");
}

void ACFPlayerCharacter::CommandWork()
{
    if (!HasAuthority() || Health <= 0 || UGameplayStatics::IsGamePaused(this) || Building->IsActive()) return;
    if(Storage->TryOpenAimed()) return;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(FieldOrder),false,this);
    const FVector Start=FirstPersonCamera->GetComponentLocation();
    GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+FirstPersonCamera->GetForwardVector()*800,ECC_Visibility,Params);
    if(auto* Node=Cast<AResourceNode>(Hit.GetActor()))
    {
        auto* Crate=DeliverySelection.Get();
        if(bDeliverySelection && (!IsValid(Crate) || Crate->IsActorBeingDestroyed())) { Feedback=TEXT("所选运输箱已丢失，按V清除或选择其他箱子"); return; }
        int32 Assigned=0;
        for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)
            if(IsCommandTarget(*It) && (!bDeliverySelection || It->CarriedFood==0) && It->AssignFieldResource(Node))
            { if(bDeliverySelection) It->Cargo->Assign(Crate,this); ++Assigned; }
        Feedback=FString::Printf(TEXT("已为%d位伙伴指派野外资源 | %s | F 取消"),Assigned,bDeliverySelection?TEXT("运到所选储物箱"):TEXT("直接放入玩家背包"));
        return;
    }
    AWorkStation* Site = nullptr;
    float Best = 800.0f;
    for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It)
    {
        if (It->RequiredSpecialty == EWorkSpecialty::Feeding) continue;
        const float Distance = GetDistanceTo(*It);
        if (Distance < Best) { Best = Distance; Site = *It; }
    }
    if (!Site) { Feedback = TEXT("请靠近工作设施8米内"); return; }
    int32 Count = 0;
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (IsCommandTarget(*It) && It->Health > 0.0f)
        { It->CancelFieldResource(); It->WorkSite = Site; It->SetActivity(ECreatureActivity::Work); ++Count; }
    Feedback = FString::Printf(TEXT("已指派%d位伙伴：%s。专长匹配时工作更快"),Count,
        *FrontierChinese::Specialty(Site->RequiredSpecialty));
}

void ACFPlayerCharacter::CommandRest()
{
    if (!HasAuthority() || Health <= 0) return;
    AWorkStation* Site = nullptr;
    float Best = 800;
    for (TActorIterator<AWorkStation> It(GetWorld()); It; ++It)
        if (GetDistanceTo(*It) < Best) { Best = GetDistanceTo(*It); Site = *It; }
    if (!Site) { Feedback = TEXT("休息命令需要靠近基地工作设施（8米）"); return; }
    int32 Count = 0;
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        if (IsCommandTarget(*It) && It->Health > 0)
        { It->CancelFieldResource(); It->bResumeWorkAfterRest = false; It->WorkSite = Site; It->SetActivity(ECreatureActivity::Rest); ++Count; }
    Feedback = FString::Printf(TEXT("%d位伙伴正回基地恢复生命和体力"), Count);
}

void ACFPlayerCharacter::HealCompanion()
{
    if (!HasAuthority() || Health <= 0) return;
    auto* Pet = TraceCreature();
    if (!Pet || Pet->Tamer != GetController()) { Feedback = TEXT("瞄准自己的伙伴使用药品"); return; }
    if (Pet->Health >= Pet->GetMaxHealth()) { Feedback = TEXT("伙伴生命已满"); return; }
    if (!Inventory->Consume(EFrontierItem::Medicine, 1)) { Feedback = TEXT("没有药品，在基地按2制作"); return; }
    Pet->Health = FMath::Min(Pet->GetMaxHealth(), Pet->Health + 40.0f);
    Feedback = TEXT("已治疗伙伴（恢复40生命）。T 基地休息，F 跟随");
}

ACFPlayerCharacter::ACFPlayerCharacter()
{
    Naming=CreateDefaultSubobject<UCompanionNamingComponent>(TEXT("Naming"));
    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
    FirstPersonCamera->SetRelativeLocation(FVector(-10.0f, 0.0f, 64.0f));
    FirstPersonCamera->bUsePawnControlRotation = true;

    CaptureComponent = CreateDefaultSubobject<UCaptureComponent>(TEXT("CaptureComponent"));
    Inventory = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory"));
    Building = CreateDefaultSubobject<UBaseBuildingComponent>(TEXT("Building"));
    HomeBuilding = CreateDefaultSubobject<UHomeBuildingComponent>(TEXT("HomeBuilding"));
    Research = CreateDefaultSubobject<UResearchComponent>(TEXT("Research"));
    Storage = CreateDefaultSubobject<UStorageInteractionComponent>(TEXT("Storage"));
    Equipment = CreateDefaultSubobject<UPlayerEquipmentComponent>(TEXT("Equipment"));
}

void ACFPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &ACFPlayerCharacter::MoveForward);
    PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &ACFPlayerCharacter::MoveRight);
    PlayerInputComponent->BindAxis(TEXT("Turn"), this, &APawn::AddControllerYawInput);
    PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &APawn::AddControllerPitchInput);
    PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &ACharacter::Jump);
    PlayerInputComponent->BindAction(TEXT("Jump"), IE_Released, this, &ACharacter::StopJumping);
    PlayerInputComponent->BindAction(TEXT("Attack"), IE_Pressed, this, &ACFPlayerCharacter::Attack);
    PlayerInputComponent->BindAction(TEXT("Capture"), IE_Pressed, this, &ACFPlayerCharacter::CaptureTarget);
    PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &ACFPlayerCharacter::CommandFollow);
    PlayerInputComponent->BindKey(EKeys::E, IE_Pressed, this, &ACFPlayerCharacter::CommandWork);
    PlayerInputComponent->BindKey(EKeys::T, IE_Pressed, this, &ACFPlayerCharacter::CommandRest);
    PlayerInputComponent->BindKey(EKeys::P, IE_Pressed, this, &ACFPlayerCharacter::HealCompanion);
    PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed, this, &ACFPlayerCharacter::CommandFocus);
    PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &ACFPlayerCharacter::GardenOrSpecial);
    PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &ACFPlayerCharacter::RequestRespawn);
    PlayerInputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ACFPlayerCharacter::TogglePauseMenu).bExecuteWhenPaused=true;
    PlayerInputComponent->BindKey(EKeys::F5, IE_Pressed, this, &ACFPlayerCharacter::QuickSave).bExecuteWhenPaused=true;
    PlayerInputComponent->BindKey(EKeys::F9, IE_Pressed, this, &ACFPlayerCharacter::QuickLoad).bExecuteWhenPaused=true;
    PlayerInputComponent->BindKey(EKeys::Up, IE_Pressed, this, &ACFPlayerCharacter::PauseMenuPrevious).bExecuteWhenPaused=true;
    PlayerInputComponent->BindKey(EKeys::Down, IE_Pressed, this, &ACFPlayerCharacter::PauseMenuNext).bExecuteWhenPaused=true;
    PlayerInputComponent->BindKey(EKeys::F1, IE_Pressed, this, &ACFPlayerCharacter::ToggleHelp);
    PlayerInputComponent->BindKey(EKeys::C, IE_Pressed, this, &ACFPlayerCharacter::ToggleBackpack);
    PlayerInputComponent->BindKey(EKeys::BackSpace, IE_Pressed, Storage.Get(), &UStorageInteractionComponent::WithdrawOne);
    PlayerInputComponent->BindKey(EKeys::Right, IE_Pressed, Storage.Get(), &UStorageInteractionComponent::DepositStack);
    PlayerInputComponent->BindKey(EKeys::Left, IE_Pressed, Storage.Get(), &UStorageInteractionComponent::WithdrawStack);
    PlayerInputComponent->BindKey(EKeys::Y, IE_Pressed, this, &ACFPlayerCharacter::PetAimedCompanion);
    PlayerInputComponent->BindKey(EKeys::F6, IE_Pressed, this, &ACFPlayerCharacter::PlayAimedCompanion);
    PlayerInputComponent->BindKey(EKeys::F7, IE_Pressed, this, &ACFPlayerCharacter::RecallAimedJourney);
    PlayerInputComponent->BindKey(EKeys::M, IE_Pressed, this, &ACFPlayerCharacter::GatherAimedResource);
    PlayerInputComponent->BindKey(EKeys::F2,IE_Pressed,this,&ACFPlayerCharacter::DepositGardenSeed);
    PlayerInputComponent->BindKey(EKeys::F3,IE_Pressed,this,&ACFPlayerCharacter::WithdrawGardenSeed);
    PlayerInputComponent->BindKey(EKeys::F4,IE_Pressed,Naming.Get(),&UCompanionNamingComponent::OpenAimed);
    PlayerInputComponent->BindKey(EKeys::Zero, IE_Pressed, CaptureComponent.Get(), &UCaptureComponent::ToggleDevice);
    PlayerInputComponent->BindKey(EKeys::One, IE_Pressed, this, &ACFPlayerCharacter::CraftCaptureDevice);
    PlayerInputComponent->BindKey(EKeys::Two, IE_Pressed, this, &ACFPlayerCharacter::CraftMedicine);
    PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ACFPlayerCharacter::CraftFood);
    PlayerInputComponent->BindKey(EKeys::J, IE_Pressed, this, &ACFPlayerCharacter::FeedAimedCompanion);
    PlayerInputComponent->BindKey(EKeys::U, IE_Pressed, this, &ACFPlayerCharacter::DepositTroughFood);
    PlayerInputComponent->BindKey(EKeys::O, IE_Pressed, this, &ACFPlayerCharacter::WithdrawTroughFood);
    PlayerInputComponent->BindKey(EKeys::H, IE_Pressed, this, &ACFPlayerCharacter::UseMedicine);
    PlayerInputComponent->BindKey(EKeys::B, IE_Pressed, this, &ACFPlayerCharacter::ToggleBuilding);
    PlayerInputComponent->BindKey(EKeys::V, IE_Pressed, this, &ACFPlayerCharacter::CycleBuilding);
    PlayerInputComponent->BindKey(EKeys::Eight, IE_Pressed, Building.Get(), &UBaseBuildingComponent::SwitchCategory);
    PlayerInputComponent->BindKey(EKeys::Nine, IE_Pressed, Building.Get(), &UBaseBuildingComponent::RotateHome);
    PlayerInputComponent->BindKey(EKeys::X, IE_Pressed, this, &ACFPlayerCharacter::DismantleBuilding);
    PlayerInputComponent->BindKey(EKeys::L, IE_Pressed, this, &ACFPlayerCharacter::SelectAimedCompanion);
    PlayerInputComponent->BindKey(EKeys::K, IE_Pressed, this, &ACFPlayerCharacter::ClearCompanionSelection);
    PlayerInputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ACFPlayerCharacter::ToggleRoster);
    PlayerInputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &ACFPlayerCharacter::NextRosterCompanion);
    PlayerInputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &ACFPlayerCharacter::PreviousRosterCompanion);
    PlayerInputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ACFPlayerCharacter::ConfirmInterfaceAction).bExecuteWhenPaused=true;
    PlayerInputComponent->BindKey(EKeys::I, IE_Pressed, this, &ACFPlayerCharacter::ToggleJournal);
    PlayerInputComponent->BindKey(EKeys::PageDown, IE_Pressed, this, &ACFPlayerCharacter::NextJournalPage);
    PlayerInputComponent->BindKey(EKeys::PageUp, IE_Pressed, this, &ACFPlayerCharacter::PreviousJournalPage);
    PlayerInputComponent->BindKey(EKeys::N, IE_Pressed, this, &ACFPlayerCharacter::ClaimObjectiveReward);
    PlayerInputComponent->BindKey(EKeys::Four, IE_Pressed, this, &ACFPlayerCharacter::CraftSpear);
    PlayerInputComponent->BindKey(EKeys::Five, IE_Pressed, this, &ACFPlayerCharacter::CraftVest);
    PlayerInputComponent->BindKey(EKeys::Six, IE_Pressed, this, &ACFPlayerCharacter::ToggleSpear);
    PlayerInputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &ACFPlayerCharacter::ToggleVest);
    PlayerInputComponent->BindKey(EKeys::Z, IE_Pressed, this, &ACFPlayerCharacter::WithdrawAimedCargo);
}

void ACFPlayerCharacter::MoveForward(float Value)
{
    if (Controller && !FMath::IsNearlyZero(Value))
    {
        AddMovementInput(FRotationMatrix(FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f))
            .GetUnitAxis(EAxis::X), Value);
    }
}

void ACFPlayerCharacter::MoveRight(float Value)
{
    if (Controller && !FMath::IsNearlyZero(Value))
    {
        AddMovementInput(FRotationMatrix(FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f))
            .GetUnitAxis(EAxis::Y), Value);
    }
}

ACreatureCharacter* ACFPlayerCharacter::TraceCreature() const
{
    if (!FirstPersonCamera || !GetWorld()) return nullptr;

    const FVector Start = FirstPersonCamera->GetComponentLocation();
    const FVector End = Start + FirstPersonCamera->GetForwardVector() * InteractionRange;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CreatureInteraction), false, this);
    GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
    return Cast<ACreatureCharacter>(Hit.GetActor());
}

void ACFPlayerCharacter::CaptureTarget()
{
    if (Building->IsActive()) { Building->Cancel(); return; }
    if (Health <= 0.0f) return;
    CancelMeleeAttack();
    RespawnProtectionUntil=0;
    if (ACreatureCharacter* Creature = TraceCreature())
    {
        CaptureComponent->TryCapture(Creature);
    }
}
