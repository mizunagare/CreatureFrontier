#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "InventoryComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "ResearchComponent.h"
#include "CraftingCatalog.h"
#include "FrontierSaveGame.h"
#include "CompanionCargoComponent.h"
#include "CompanionMemoryComponent.h"

AResourceNode::AResourceNode()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Resource")); Visual->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Palette(TEXT("/Game/Materials/M_CreaturePalette.M_CreaturePalette"));
    Visual->SetStaticMesh(Mesh.Object); Visual->SetMaterial(0,Palette.Object);
    Visual->SetRelativeScale3D(FVector(.9f,.9f,.65f));
    Visual->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Visual->SetCollisionResponseToAllChannels(ECR_Ignore);
    Visual->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block); Visual->SetCanEverAffectNavigation(false);
    Label=CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label")); Label->SetupAttachment(RootComponent);
    Label->SetRelativeLocation(FVector(0,0,65)); Label->SetWorldSize(12); Label->SetCullDistance(600);Label->SetHorizontalAlignment(EHTA_Center);
}
void AResourceNode::BeginPlay()
{
    Super::BeginPlay();
    Label->SetHiddenInGame(true);
    if(!PersistentId.IsValid()) PersistentId=FGuid::NewGuid();
    Remaining=Definition && Definition->IsValidDefinition()?Definition->Capacity:0;
    Refresh();
}
void AResourceNode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!HasAuthority() || !Definition || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<0) return;
    HarvestCooldown=FMath::Max(0.f,HarvestCooldown-DeltaSeconds);
    if(Remaining==0 && RegrowRemaining>0)
    {
        const float PreviousSeconds=FMath::CeilToFloat(RegrowRemaining);
        RegrowRemaining=FMath::Max(0.f,RegrowRemaining-DeltaSeconds);
        if(RegrowRemaining==0) { Remaining=Definition->Capacity; Refresh(); }
        else if(PreviousSeconds!=FMath::CeilToFloat(RegrowRemaining)) Label->SetText(FText::FromString(GetWorldLabel()));
    }
    if(auto* Hero=UGameplayStatics::GetPlayerPawn(this,0))
        Label->SetWorldRotation((Hero->GetActorLocation()-Label->GetComponentLocation()).Rotation());
}
bool AResourceNode::CanReach(const AActor* Harvester,float Range) const
{
    if(!IsValid(Harvester) || Harvester->GetWorld()!=GetWorld() || Harvester->GetDistanceTo(this)>Range) return false;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(ResourceSight),false,Harvester);
    return !GetWorld()->LineTraceSingleByChannel(Hit,Harvester->GetActorLocation(),GetActorLocation(),ECC_Visibility,Params) || Hit.GetActor()==this;
}
bool AResourceNode::TryHarvest(ACFPlayerCharacter* Recipient,AActor* Harvester)
{
    if(!HasAuthority() || !IsValid(Recipient) || Recipient->Health<=0 || UGameplayStatics::IsGamePaused(this) ||
        !Definition || !Definition->IsValidDefinition() || Remaining<=0 || HarvestCooldown>0) return false;
    if(Harvester!=Recipient)
    {
        const auto* Pet=Cast<ACreatureCharacter>(Harvester);
        if(!Pet || Pet->Health<=0 || Pet->Tamer!=Recipient->GetController() || Pet->Disposition!=ECreatureDisposition::Tamed ||
            Pet->Activity!=ECreatureActivity::Work || !CanReach(Pet,220)) return false;
        if(Pet->GetFieldResource()!=this && (Pet->GetFieldResource() || !IsValid(Pet->WorkSite) ||
            Pet->WorkSite->RequiredSpecialty!=Definition->Specialty || GetDistanceTo(Pet->WorkSite)>800)) return false;
    }
    else if(!CanReach(Recipient,300)) return false;
    // 唯一的共用事务：容量、距离或归属检查失败时不消耗任何资源。
    const auto* Pet=Cast<ACreatureCharacter>(Harvester);
    const bool Added=Pet && Pet->Cargo->HasOrder()?Pet->Cargo->Receive(Definition->OutputItem):Recipient->Inventory->AddItem(Definition->OutputItem,1);
    if(!Added) return false;
    --Remaining; HarvestCooldown=Definition->HarvestInterval;
    if(Remaining==0) RegrowRemaining=Definition->RegrowSeconds;
    Recipient->Research->RecordHarvest(this);
    UCompanionMemoryComponent::RecordHarvest(this,Recipient,Harvester);
    Refresh(); return true;
}
FString AResourceNode::GetStatus() const
{
    if(!Definition) return TEXT("资源不可用");
    if(Remaining==0) return FString::Printf(TEXT("%s | 已采空 | %s"),*Definition->DisplayName.ToString(),
        Definition->RegrowSeconds>0?*FString::Printf(TEXT("%.0f秒后再生"),FMath::CeilToFloat(RegrowRemaining)):TEXT("不会再生"));
    return FString::Printf(TEXT("%s | %d / %d | %s | 按住M采集（3米）"),*Definition->DisplayName.ToString(),Remaining,Definition->Capacity,*UCraftingCatalog::ItemName(Definition->OutputItem));
}
void AResourceNode::Refresh()
{
    Label->SetText(FText::FromString(GetWorldLabel()));
    Visual->SetRelativeScale3D(Remaining>0?FVector(.9f,.9f,.65f):FVector(.5f,.5f,.15f));
    if(!ResourceMaterial) ResourceMaterial=Visual->CreateDynamicMaterialInstance(0);
    if(Definition && ResourceMaterial) ResourceMaterial->SetVectorParameterValue(TEXT("Tint"),Remaining>0?Definition->Color:FLinearColor(.15f,.12f,.1f));
}
FString AResourceNode::GetWorldLabel() const
{return Definition?Definition->DisplayName.ToString()+FString::Printf(TEXT(" (%d)"),Remaining):TEXT("资源不可用");}
void SeedFrontierResources(UWorld* World)
{
    if(!World) return;
    for(TActorIterator<AResourceNode> It(World);It;++It) return;
    TArray<AResourceNode*> Nodes;
    if(!SpawnStarterResources(World,Nodes)) for(auto* Node:Nodes) Node->Destroy();
}
bool SpawnStarterResources(UWorld* World,TArray<AResourceNode*>& OutNodes)
{
    TArray<FResourceSaveRecord> Records;
    if(!MakeStarterResourceRecords(Records)) return false;
    for(const auto& Record:Records)
    {
        auto* Node=World->SpawnActorDeferred<AResourceNode>(AResourceNode::StaticClass(),Record.Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Node) return false;
        Node->Definition=Record.Definition.Get(); Node->PersistentId=Record.Id; Node->FinishSpawning(Record.Transform); OutNodes.Add(Node);
    }
    return true;
}
bool MakeStarterResourceRecords(TArray<FResourceSaveRecord>& OutRecords)
{
    struct FSeed { const TCHAR* Asset; FVector Position; uint32 Id; };
    const FSeed Seeds[]={
        {TEXT("DA_Brush"),FVector(-450,290,0),1}, {TEXT("DA_Stone"),FVector(-650,-60,0),2},
        {TEXT("DA_Salvage"),FVector(-650,640,0),3}, {TEXT("DA_Brush"),FVector(-1400,900,0),4},
        {TEXT("DA_Stone"),FVector(1250,1000,0),5}, {TEXT("DA_Salvage"),FVector(800,-1000,0),6},
        {TEXT("DA_Crystal"),FVector(1400,1600,0),7},
        {TEXT("DA_Fiber"),FVector(-1500,1400,0),8},
        {TEXT("DA_Ore"),FVector(1800,-1100,0),9},
        {TEXT("DA_Scrap"),FVector(-1800,-800,0),10},
        {TEXT("DA_HollowScrap"),FVector(-550,-4100,0),11}};
    for(const auto& Seed:Seeds)
    {
        auto* Definition=LoadObject<UResourceDefinition>(nullptr,*FString::Printf(TEXT("/Game/Resources/%s.%s"),Seed.Asset,Seed.Asset));
        if(!Definition || !Definition->IsValidDefinition()) { UE_LOG(LogTemp,Error,TEXT("Missing resource definition %s"),Seed.Asset); return false; }
        FResourceSaveRecord Record; Record.Id=FGuid(0xCF170000,0,0,Seed.Id); Record.Definition=Definition;
        Record.Transform=FTransform(Seed.Position); Record.Remaining=Definition->Capacity; OutRecords.Add(Record);
    }
    return true;
}
