#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CompanionCargoComponent.h"
#include "CompanionPersonalityComponent.h"
#include "FarmCropComponent.h"
#include "CreatureAffinity.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "WorldLoot.h"
#include "WorkStation.h"
#include "EngineUtils.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ACreatureCharacter::ACreatureCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    CargoInventory=CreateDefaultSubobject<UInventoryComponent>(TEXT("CargoInventory"));
    Cargo=CreateDefaultSubobject<UCompanionCargoComponent>(TEXT("Cargo"));
    Personality=CreateDefaultSubobject<UCompanionPersonalityComponent>(TEXT("Personality"));
    bReplicates = true;
    SetReplicateMovement(true);
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    AIControllerClass = AAIController::StaticClass();
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    GetCapsuleComponent()->SetCanEverAffectNavigation(false);
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->AvoidanceConsiderationRadius=400;
    GetCharacterMovement()->MaxWalkSpeed = 320.0f;
    bUseControllerRotationYaw = false;

    VisualPivot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualPivot"));
    VisualPivot->SetupAttachment(GetRootComponent());
    CreatureVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CreatureVisual"));
    CreatureVisual->SetupAttachment(VisualPivot);
    CreatureVisual->SetRelativeLocation(FVector(0, 0, -28));
    CreatureVisual->SetRelativeScale3D(FVector(0.9f, 0.72f, 0.95f));
    CreatureVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CreatureVisual->SetCanEverAffectNavigation(false);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
        TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (SphereMesh.Succeeded())
    {
        SphereShape = SphereMesh.Object;
        CreatureVisual->SetStaticMesh(SphereMesh.Object);
    }
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
    CubeShape = CubeMesh.Object;
    auto Part = [this](const TCHAR* Name, UStaticMesh* ShapeMesh, FVector Location, FVector Scale, FRotator Rotation=FRotator::ZeroRotator)
    {
        auto* Piece = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Piece->SetupAttachment(VisualPivot);
        Piece->SetStaticMesh(ShapeMesh);
        Piece->SetRelativeLocation(Location);
        Piece->SetRelativeScale3D(Scale);
        Piece->SetRelativeRotation(Rotation);
        Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Piece->SetCanEverAffectNavigation(false);
        return Piece;
    };
    AccentParts.Add(Part(TEXT("LeafLeft"), ConeMesh.Object, FVector(-5,-22,42), FVector(.24,.14,.7), FRotator(0,0,-18)));
    AccentParts.Add(Part(TEXT("LeafRight"), ConeMesh.Object, FVector(-5,22,42), FVector(.24,.14,.7), FRotator(0,0,18)));
    AccentParts.Add(Part(TEXT("EmberTail"), ConeMesh.Object, FVector(-68,0,-28), FVector(.35,.35,1.0), FRotator(-90,0,0)));
    AccentParts.Add(Part(TEXT("EmberSail"), ConeMesh.Object, FVector(-12,0,29), FVector(.38,.18,.55)));
    AccentParts.Add(Part(TEXT("StoneLeft"), ConeMesh.Object, FVector(10,-25,24), FVector(.28,.28,.55)));
    AccentParts.Add(Part(TEXT("StoneCenter"), ConeMesh.Object, FVector(-8,0,35), FVector(.35,.35,.7)));
    AccentParts.Add(Part(TEXT("StoneRight"), ConeMesh.Object, FVector(10,25,24), FVector(.28,.28,.55)));
    EyeParts.Add(Part(TEXT("EyeLeft"), SphereShape, FVector(38,-19,-4), FVector(.22,.2,.25)));
    EyeParts.Add(Part(TEXT("EyeRight"), SphereShape, FVector(38,19,-4), FVector(.22,.2,.25)));
    Pupils.Add(Part(TEXT("PupilLeft"), SphereShape, FVector(47,-19,-3), FVector(.09,.11,.15)));
    Pupils.Add(Part(TEXT("PupilRight"), SphereShape, FVector(47,19,-3), FVector(.09,.11,.15)));
    CargoPack=Part(TEXT("FoodCargo"),CubeMesh.Object,FVector(-10,45,-20),FVector(.3,.25,.3));
    CargoPack->SetVisibility(false); CargoPack->SetHiddenInGame(true);
    Feet.Add(Part(TEXT("FootFL"), SphereShape, FVector(24,-22,-75), FVector(.32,.25,.2)));
    Feet.Add(Part(TEXT("FootFR"), SphereShape, FVector(24,22,-75), FVector(.32,.25,.2)));
    Feet.Add(Part(TEXT("FootBL"), SphereShape, FVector(-24,-22,-75), FVector(.32,.25,.2)));
    Feet.Add(Part(TEXT("FootBR"), SphereShape, FVector(-24,22,-75), FVector(.32,.25,.2)));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    SkillIndicator = Part(TEXT("SkillIndicator"), CylinderMesh.Object, FVector(0,0,-84), FVector(1.6,1.6,.015));
    SkillIndicator->SetVisibility(false);
    SkillIndicator->SetHiddenInGame(true);
    AlphaWarning = Part(TEXT("AlphaWarning"), CylinderMesh.Object, FVector(0,0,-84), FVector(7.6,7.6,.02));
    AlphaWarning->SetVisibility(false);
    AlphaWarning->SetHiddenInGame(true);
    // Ground telegraphs must not follow breathing, crouching or strike poses.
    SkillIndicator->SetupAttachment(GetRootComponent());
    AlphaWarning->SetupAttachment(GetRootComponent());
    for(const auto& Piece:Feet) NeutralFeet.Add(Piece->GetRelativeTransform());
    for(const auto& Piece:AccentParts) NeutralAccents.Add(Piece->GetRelativeTransform());
    for(const auto& Piece:EyeParts) NeutralEyes.Add(Piece->GetRelativeTransform());
    for(const auto& Piece:Pupils) NeutralPupils.Add(Piece->GetRelativeTransform());
}

void ACreatureCharacter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshAppearance();
}

void ACreatureCharacter::RefreshAppearance()
{
    const ECreatureForm Form = Definition ? Definition->Form : ECreatureForm::Leaf;
    CreatureVisual->SetStaticMesh(Form == ECreatureForm::Stone ? CubeShape : SphereShape);
    CreatureVisual->SetRelativeScale3D(Form == ECreatureForm::Stone ? FVector(.9,.8,.75) : FVector(.9,.72,.95));
    for (int32 Index=0; Index<AccentParts.Num(); ++Index)
    {
        const bool bShow = Form == ECreatureForm::Leaf ? Index < 2 : Form == ECreatureForm::Ember ? Index >= 2 && Index < 4 : Index >= 4;
        AccentParts[Index]->SetVisibility(bShow);
        AccentParts[Index]->SetHiddenInGame(!bShow);
    }
    if (!Definition || !Definition->PaletteMaterial) return;
    auto MakeColor = [this](FLinearColor Color)
    {
        auto* Material = UMaterialInstanceDynamic::Create(Definition->PaletteMaterial, this);
        Material->SetVectorParameterValue(TEXT("Tint"), Color);
        return Material;
    };
    auto* Body = MakeColor(Definition->BodyColor);
    auto* Accent = MakeColor(Definition->AccentColor);
    auto* White = MakeColor(FLinearColor(.95f,.96f,.9f));
    auto* Black = MakeColor(FLinearColor(.008f,.015f,.025f));
    CreatureVisual->SetMaterial(0, Body);
    for (const auto& Piece : AccentParts) Piece->SetMaterial(0, Accent);
    for (const auto& Piece : Feet) Piece->SetMaterial(0, Accent);
    for (const auto& Piece : EyeParts) Piece->SetMaterial(0, White);
    for (const auto& Piece : Pupils) Piece->SetMaterial(0, Black);
    SkillIndicator->SetMaterial(0, Accent);
    AlphaWarning->SetMaterial(0, MakeColor(FLinearColor(1,.12f,.02f)));
    CargoPack->SetMaterial(0,MakeColor(FLinearColor(.85f,.4f,.05f)));
}

FString ACreatureCharacter::GetCreatureName() const
{
    return Disposition==ECreatureDisposition::Tamed && !Nickname.IsEmpty()?Nickname:GetSpeciesName();
}
FString ACreatureCharacter::GetSpeciesName() const
{
    return Definition && !Definition->DisplayName.IsEmpty() ? Definition->DisplayName.ToString() : TEXT("Wildling");
}

float ACreatureCharacter::GetWorkEfficiency(const AWorkStation* Site) const
{
    if (!Definition || !Site) return 1.0f;
    return Definition->WorkSpecialties.Contains(Site->RequiredSpecialty) ? Definition->WorkSpeed : 0.5f;
}

void ACreatureCharacter::StopNavigation()
{
    bHasFollowPathDetour=false;
    if (auto* AI = Cast<AAIController>(GetController())) AI->StopMovement();
}

void ACreatureCharacter::NavigateTo(const FVector& Destination, float Acceptance)
{
    if (GetWorld()->GetTimeSeconds() < NextNavigationUpdate) return;
    NextNavigationUpdate = GetWorld()->GetTimeSeconds() + 0.4;
    auto* AI = Cast<AAIController>(GetController());
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!AI || !Nav) { StopNavigation(); return; }
    const double Now=GetWorld()->GetTimeSeconds();
    if(Activity!=ECreatureActivity::Follow || FVector::Dist2D(GetActorLocation(),FollowPathDetour)<30 ||
        FVector::Dist2D(Destination,FollowPathDetourGoal)>100 || Now>FollowPathDetourExpires) bHasFollowPathDetour=false;
    if(Activity==ECreatureActivity::Follow && !bHasFollowPathDetour && Now>=NextFollowDetourSearch && AI->GetMoveStatus()==EPathFollowingStatus::Moving)
        if(const auto* Blocker=FindFollowPathBlocker(AI->GetImmediateMoveDestination()))
        {
            NextFollowDetourSearch=Now+1;
            if(FindFollowPathDetour(Destination,Blocker,FollowPathDetour))
            { bHasFollowPathDetour=true; FollowPathDetourGoal=Destination; FollowPathDetourExpires=Now+4; }
        }
    const FVector NavigationGoal=bHasFollowPathDetour?FollowPathDetour:Destination;
    FNavLocation Projected;
    if (!Nav->ProjectPointToNavigation(NavigationGoal, Projected, FVector(250,250,300)))
    {
        const APawn* FollowOwner=Activity==ECreatureActivity::Follow && Tamer?Tamer->GetPawn():nullptr;
        if(!FollowOwner || !Nav->ProjectPointToNavigation(FollowOwner->GetActorLocation(),Projected,FVector(250,250,300)))
        { StopNavigation(); return; }
        Acceptance=160.f; // Preserve personal space when falling back to the owner's location.
    }
    const auto Result=AI->MoveToLocation(Projected.Location, Acceptance, false, true, false, false, nullptr, false);
    if(Result==EPathFollowingRequestResult::Failed && Activity==ECreatureActivity::Follow && Tamer && Tamer->GetPawn())
        AI->MoveToActor(Tamer->GetPawn(),160,false,true,false,nullptr,false);
}

bool ACreatureCharacter::CanHit(const AActor* Target) const
{
    if (!IsValid(Target) || GetDistanceTo(Target) > AttackRange) return false;
    return HasClearSight(Target);
}

void ACreatureCharacter::BeginPlay()
{
    Super::BeginPlay();
    if(HasAuthority() && !PersistentId.IsValid()) PersistentId=FGuid::NewGuid();
    RefreshAppearance();
    if (HasAuthority() && Definition)
    {
        Health = GetMaxHealth();
        GetCharacterMovement()->MaxWalkSpeed = Definition->MoveSpeed;
        AttackInterval = Definition->AttackSeconds;
    }
}

void ACreatureCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (VisualPivot)
    {
        const bool Carrying=CarriedFood>0 || Cargo->Count()>0;
        CargoPack->SetVisibility(Carrying); CargoPack->SetHiddenInGame(!Carrying);
        UpdateCreaturePresentation(DeltaSeconds);
        const bool bEffect = Health > 0 && (SpecialWindupRemaining > 0 || SkillPulseRemaining > 0 || GuardRemaining > 0);
        SkillIndicator->SetVisibility(bEffect);
        SkillIndicator->SetHiddenInGame(!bEffect);
        const float Size = 1.6f + FMath::Sin(GetWorld()->GetTimeSeconds()*12.0f)*0.12f;
        SkillIndicator->SetRelativeScale3D(FVector(Size,Size,.015));
        const bool bWarning = bAlpha && Health > 0 && Disposition != ECreatureDisposition::Tamed && AlphaWindup > 0;
        AlphaWarning->SetVisibility(bWarning);
        AlphaWarning->SetHiddenInGame(!bWarning);
    }
    if (!HasAuthority()) return;
    const bool AvoidFollowers=Health>0 && Disposition==ECreatureDisposition::Tamed && (Activity==ECreatureActivity::Follow || Activity==ECreatureActivity::Idle);
    if(GetCharacterMovement()->bUseRVOAvoidance!=AvoidFollowers) GetCharacterMovement()->SetAvoidanceEnabled(AvoidFollowers);
    SpecialCooldownRemaining = FMath::Max(0.0f, SpecialCooldownRemaining - DeltaSeconds);
    SlowRemaining = FMath::Max(0.0f, SlowRemaining - DeltaSeconds);
    GuardRemaining = FMath::Max(0.0f, GuardRemaining - DeltaSeconds);
    SkillPulseRemaining = FMath::Max(0.0f, SkillPulseRemaining - DeltaSeconds);
    RetaliationRemaining = FMath::Max(0.0f, RetaliationRemaining - DeltaSeconds);
    AlphaCooldown = FMath::Max(0.0f, AlphaCooldown - DeltaSeconds);
    AutoFeedCooldown = FMath::Max(0.0f,AutoFeedCooldown-DeltaSeconds);
    PettingCooldown=FMath::Max(0.f,PettingCooldown-DeltaSeconds);
    GetCharacterMovement()->MaxWalkSpeed = (Definition ? Definition->MoveSpeed : 320.0f) * (SlowRemaining > 0 ? 0.5f : 1.0f) *
        (Disposition != ECreatureDisposition::Tamed && Activity == ECreatureActivity::Wander ? .45f : 1.0f);
    if (Health <= 0.0f) { bResumeWorkAfterRest = false; CancelFieldResource(); FeedingSite.Reset(); AlphaWindup = 0; ClearCombatOrders(); StopNavigation(); return; }

    AttackCooldown = FMath::Max(0.0f, AttackCooldown - DeltaSeconds);
    if (Disposition == ECreatureDisposition::Tamed)
    {
        ACFPlayerCharacter* OwnerPlayer = Tamer ? Cast<ACFPlayerCharacter>(Tamer->GetPawn()) : nullptr;
        if (!OwnerPlayer || OwnerPlayer->Health <= 0.0f) { bGatheringActive=false; ClearCombatOrders(); StopNavigation(); return; }
        if(!Cargo->ValidateOrder()) return;
        Nutrition = FMath::Max(0.0f,Nutrition - DeltaSeconds*(Activity == ECreatureActivity::Work ? .35f : Activity == ECreatureActivity::Rest ? .04f : .08f));
        if (TickAutoFeeding()) return;
        if (Nutrition <= 0 && Activity == ECreatureActivity::Work)
        {
            bResumeWorkAfterRest = GetFieldResource() || (IsValid(WorkSite) && WorkSite->RequiredSpecialty != EWorkSpecialty::Feeding);
            SetActivity(ECreatureActivity::Rest); WorkProgress = 0;
            OwnerPlayer->Feedback = TEXT("A companion is hungry. J: feed aimed companion | 3: craft food at base");
        }
        if (Activity == ECreatureActivity::Rest)
        {
            if (bResumeWorkAfterRest && !GetFieldResource() && (!IsValid(WorkSite) || WorkSite->RequiredSpecialty == EWorkSpecialty::Feeding))
            { bResumeWorkAfterRest = false; WorkSite = nullptr; StopNavigation(); SetActivity(ECreatureActivity::Follow); OwnerPlayer->Feedback=TEXT("Assigned work target no longer available; companion following"); return; }
            if (IsValid(WorkSite) && GetDistanceTo(WorkSite) > 220.0f)
            {
                NavigateTo(WorkSite->GetActorLocation(), 35.0f);
                return;
            }
            StopNavigation();
            Stamina = FMath::Min(100.0f, Stamina + DeltaSeconds * (IsValid(WorkSite) ? 12.0f : 4.0f));
            if (IsValid(WorkSite)) Health = FMath::Min(GetMaxHealth(), Health + DeltaSeconds * 6.0f);
            if (bResumeWorkAfterRest && Stamina >= 80 && Nutrition >= 20 && Health >= GetMaxHealth()*.8f)
            {
                SetActivity(ECreatureActivity::Work);
                OwnerPlayer->Feedback = TEXT("Recovered companion resumed its assigned work");
            }
            return;
        }
        if (Activity == ECreatureActivity::Work && GetFieldResource()) { if(!Cargo->TickDelivery(DeltaSeconds,OwnerPlayer)) TickGathering(DeltaSeconds,OwnerPlayer); return; }
        if(Activity==ECreatureActivity::Work && !IsValid(WorkSite)) { StopNavigation(); SetActivity(ECreatureActivity::Follow); OwnerPlayer->Feedback=TEXT("Assigned work target no longer available; companion following"); return; }
        if (Activity == ECreatureActivity::Work && IsValid(WorkSite))
        {
            if (WorkSite->RequiredSpecialty == EWorkSpecialty::Feeding) { SetActivity(ECreatureActivity::Rest); return; }
            if(WorkSite->RequiredSpecialty==EWorkSpecialty::Transport) { TickHauling(DeltaSeconds); return; }
            if(WorkSite->RequiredSpecialty!=EWorkSpecialty::Farming) { TickGathering(DeltaSeconds,OwnerPlayer); return; }
            if (ProgressSite.Get() != WorkSite.Get()) { WorkProgress = 0; ProgressSite = WorkSite; }
            const FVector Offset = WorkSite->GetActorLocation() - GetActorLocation();
            if (Offset.Size() > 220.0f) { NavigateTo(WorkSite->GetActorLocation(), 35.0f); WorkProgress = 0.0f; }
            else
            {
                StopNavigation();
                Stamina = FMath::Max(0.0f, Stamina - DeltaSeconds * 1.5f);
                if (Stamina <= 0.0f)
                {
                    bResumeWorkAfterRest = true;
                    SetActivity(ECreatureActivity::Rest);
                    WorkProgress = 0.0f;
                    OwnerPlayer->Feedback = TEXT("Companion resting; work resumes at 80 stamina, 20 satiety and 80% health");
                    return;
                }
                if(WorkSite->Crop->Work(this,OwnerPlayer,DeltaSeconds*GetWorkEfficiency(WorkSite)*(Nutrition<20?.5f:1.f)))
                { OwnerPlayer->RecordCompanionProduction(this,EFrontierItem::Food); AddExperience(1); }
            }
            return;
        }
        WorkProgress = 0.0f;
        if (!IsValid(FocusTarget) || FocusTarget->Health <= 0 || FocusTarget->Disposition == ECreatureDisposition::Tamed ||
            FocusTarget->Activity == ECreatureActivity::ReturnHome || OwnerPlayer->GetDistanceTo(FocusTarget) > 1500.0f)
        {
            FocusTarget = nullptr;
            bSpecialRequested = false;
        }
        if (TickSpecial(DeltaSeconds)) return;
        ACreatureCharacter* Enemy = FocusTarget;
        float Best = 1000.0f;
        if (!Enemy) for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
        {
            if (It->Disposition == ECreatureDisposition::Tamed || It->Health <= 0.0f || It->Activity == ECreatureActivity::ReturnHome) continue;
            const float Distance = OwnerPlayer->GetDistanceTo(*It);
            if (Distance < Best) { Best = Distance; Enemy = *It; }
        }
        SetActivity(Enemy ? ECreatureActivity::Combat : ECreatureActivity::Follow);
        if(GetCharacterMovement()->bUseRVOAvoidance!=(Enemy==nullptr)) GetCharacterMovement()->SetAvoidanceEnabled(Enemy==nullptr);
        const FVector Destination = Enemy ? Enemy->GetActorLocation() : GetFollowDestination();
        // A nav path may bend back through a companion even when the final-goal ray is clear.
        // Bypass self-steering only if BOTH desired and actual path directions have a clear local corridor.
        const auto* FollowAI=Cast<AAIController>(GetController());
        const bool ClearSegment=!FollowAI || FollowAI->GetMoveStatus()!=EPathFollowingStatus::Moving ||
            HasClearFollowApproach(FollowAI->GetImmediateMoveDestination());
        GetCharacterMovement()->SetRVOAvoidanceWeight(!Enemy && HasClearFollowApproach(Destination) && ClearSegment?1.f:.5f);
        const FVector Offset = Destination - GetActorLocation();
        if (Enemy ? !CanHit(Enemy) : Offset.Size2D() > 25.0f) NavigateTo(Destination, Enemy ? 65.0f : 15.0f);
        else
        {
            StopNavigation();
            if (Enemy && AttackCooldown <= 0.0f)
            {
                if (Stamina < 3.0f) { WorkSite = nullptr; SetActivity(ECreatureActivity::Rest); return; }
                Stamina -= 3.0f;
                BeginStrikePose(Enemy);
                UGameplayStatics::ApplyDamage(Enemy, GetAttackDamage(), Tamer, this, UDamageType::StaticClass());
                AttackCooldown = AttackInterval;
            }
        }
        return;
    }
    if (TickHabitatReturn()) return;
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!IsValid(Player)) { AlphaWindup = 0; StopNavigation(); return; }
    if (const auto* Hero = Cast<ACFPlayerCharacter>(Player); Hero && Hero->Health <= 0.0f) { AlphaWindup = 0; StopNavigation(); return; }

    APawn* Target = Player;
    float Closest = GetDistanceTo(Player);
    for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
    {
        if (It->Disposition != ECreatureDisposition::Tamed || It->Health <= 0) continue;
        const float Distance = GetDistanceTo(*It);
        if (Distance < Closest) { Closest = Distance; Target = *It; }
    }
    bool bProvoked = false;
    if (auto* Aggressor = RetaliationTarget.Get(); Aggressor && RetaliationRemaining > 0 && GetDistanceTo(Aggressor) <= 2000.0f)
    {
        const auto* Pet = Cast<ACreatureCharacter>(Aggressor);
        if (!Pet || Pet->Health > 0) { Target = Aggressor; bProvoked = true; }
    }
    const FVector Offset = Target->GetActorLocation() - GetActorLocation();
    const float Distance2D = Offset.Size2D();
    if ((AlphaWindup > 0 || Distance2D <= AggroRange || bProvoked) && TickAlphaAttack(DeltaSeconds, Target)) return;
    if (Distance2D <= AggroRange || bProvoked)
    {
        Disposition = ECreatureDisposition::Hostile;
        SetActivity(ECreatureActivity::Combat);
        if (!CanHit(Target))
        {
            NavigateTo(Target->GetActorLocation(), 65.0f);
        }
        else
        {
            StopNavigation();
            if (AttackCooldown <= 0.0f)
            {
                BeginStrikePose(Target);
                UGameplayStatics::ApplyDamage(Target, GetAttackDamage(), GetController(), this, UDamageType::StaticClass());
                AttackCooldown = AttackInterval;
            }
        }
    }
    else TickRoaming(DeltaSeconds);
}

float ACreatureCharacter::GetHealthRatio() const
{
    const float MaxHealth = GetMaxHealth();
    return MaxHealth > 0.0f ? FMath::Clamp(Health / MaxHealth, 0.0f, 1.0f) : 0.0f;
}

float ACreatureCharacter::GetMaxHealth() const
{
    return (Definition ? Definition->BaseMaxHealth : 100.0f) + (Level - 1) * 20.0f;
}

float ACreatureCharacter::GetAttackDamage() const
{
    return (Definition ? Definition->BaseAttack : 10.0f) + (Level - 1) * 2.0f;
}

void ACreatureCharacter::AddExperience(int32 Amount)
{
    if (!HasAuthority() || Health <= 0 || Disposition != ECreatureDisposition::Tamed || Amount <= 0 || Level >= 20) return;
    Experience += FMath::Min(Amount, 10000);
    while (Level < 20 && Experience >= Level * 100)
    {
        Experience -= Level * 100;
        ++Level;
        Health = FMath::Min(GetMaxHealth(), Health + 20.0f);
    }
    if (Level == 20) Experience = 0;
}

float ACreatureCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    if (Activity == ECreatureActivity::ReturnHome && Disposition != ECreatureDisposition::Tamed) return 0;
    const auto* Attacker=Cast<ACreatureCharacter>(DamageCauser);
    const float Affinity=CreatureAffinity::Multiplier(Attacker?Attacker->Definition.Get():nullptr,Definition);
    const float Applied = FMath::Min(Health, FMath::Max(0.0f, DamageAmount) * Affinity * (GuardRemaining > 0 ? 0.5f : 1.0f));
    if (HasAuthority() && Applied > 0)
    {
        Health = FMath::Max(0.0f, Health - Applied);
        HitPoseRemaining=.2f;
        AffectionPoseRemaining=0;
        if(Attacker)
        {
            auto* Observer=Attacker->Tamer?Cast<ACFPlayerCharacter>(Attacker->Tamer->GetPawn()):Tamer?Cast<ACFPlayerCharacter>(Tamer->GetPawn()):nullptr;
            if(Observer && Observer->Health>0)
                Observer->Feedback=FString::Printf(TEXT("%s -> %s: %.1f damage | %s x%.2f"),*Attacker->GetCreatureName(),*GetCreatureName(),Applied,Affinity>1?TEXT("STRONG"):Affinity<1?TEXT("RESISTED"):TEXT("NORMAL"),Affinity);
        }
        if (Disposition != ECreatureDisposition::Tamed && Health > 0)
        {
            APawn* Aggressor = Cast<APawn>(DamageCauser);
            if (!Aggressor && EventInstigator) Aggressor = EventInstigator->GetPawn();
            if (IsValid(Aggressor) && Aggressor != this) { RetaliationTarget = Aggressor; RetaliationRemaining = 12.0f; }
        }
        if (Health <= 0.0f)
        {
            CancelFieldResource();
            bResumeWorkAfterRest = false;
            AlphaWindup = 0;
            SetActivity(ECreatureActivity::Rest);
            StopNavigation();
            if (Disposition != ECreatureDisposition::Tamed && IsValid(EventInstigator))
            {
                if (auto* Hero = Cast<ACFPlayerCharacter>(EventInstigator->GetPawn()))
                {
                    Hero->RecordAlphaResolution(this);
                    auto* Loot = GetWorld()->SpawnActor<AWorldLoot>(GetActorLocation() + FVector(0,0,-55), FRotator::ZeroRotator);
                    if (Loot) { Loot->Materials = bAlpha ? 8 : 2; Loot->Medicine = bAlpha ? 1 : 0; }
                    Hero->Feedback = TEXT("Loot dropped - approach the gold cache to collect");
                }
                for (TActorIterator<ACreatureCharacter> It(GetWorld()); It; ++It)
                    if (It->Tamer == EventInstigator && It->GetDistanceTo(this) <= 1500.0f)
                        It->AddExperience((DamageCauser == *It ? 50 : 25) * Level);
            }
        }
        OnRep_Health();
    }
    return Applied;
}

void ACreatureCharacter::SetActivity(ECreatureActivity NewActivity)
{
    if (HasAuthority() && (NewActivity == ECreatureActivity::Work || NewActivity == ECreatureActivity::Rest)) ClearCombatOrders();
    if (!HasAuthority() || Activity == NewActivity) return;
    if(NewActivity!=ECreatureActivity::Work && !(NewActivity==ECreatureActivity::Rest && bResumeWorkAfterRest)) CancelFieldResource();
    if (NewActivity != ECreatureActivity::Rest) bResumeWorkAfterRest = false;
    if (NewActivity != ECreatureActivity::Work) ResetGathering();
    Activity = NewActivity;
    OnRep_Activity();
}

void ACreatureCharacter::Tame(AController* NewTamer)
{
    if (!HasAuthority() || !IsValid(NewTamer) || Activity == ECreatureActivity::ReturnHome) return;
    bHasHabitat = false;
    Tamer = NewTamer;
    AlphaWindup = 0;
    RetaliationTarget.Reset();
    RetaliationRemaining = 0;
    Disposition = ECreatureDisposition::Tamed;
    Personality->EnsurePreference();
    Activity = ECreatureActivity::Follow;
    OnTamed();
}

void ACreatureCharacter::OnTamed_Implementation() {}
void ACreatureCharacter::OnRep_Health() {}
void ACreatureCharacter::OnRep_Activity() {}

void ACreatureCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACreatureCharacter, Health);
    DOREPLIFETIME(ACreatureCharacter, bAlpha);
    DOREPLIFETIME(ACreatureCharacter, AlphaCooldown);
    DOREPLIFETIME(ACreatureCharacter, AlphaWindup);
    DOREPLIFETIME(ACreatureCharacter, BondPoints);
    DOREPLIFETIME(ACreatureCharacter, PettingCooldown);
    DOREPLIFETIME(ACreatureCharacter, Level);
    DOREPLIFETIME(ACreatureCharacter, Experience);
    DOREPLIFETIME(ACreatureCharacter, Stamina);
    DOREPLIFETIME(ACreatureCharacter, Nutrition);
    DOREPLIFETIME(ACreatureCharacter, AutoFeedCooldown);
    DOREPLIFETIME(ACreatureCharacter, bResumeWorkAfterRest);
    DOREPLIFETIME(ACreatureCharacter, SpecialCooldownRemaining);
    DOREPLIFETIME(ACreatureCharacter, SpecialWindupRemaining);
    DOREPLIFETIME(ACreatureCharacter, SlowRemaining);
    DOREPLIFETIME(ACreatureCharacter, GuardRemaining);
    DOREPLIFETIME(ACreatureCharacter, Disposition);
    DOREPLIFETIME(ACreatureCharacter, Activity);
    DOREPLIFETIME(ACreatureCharacter, Tamer);
}
