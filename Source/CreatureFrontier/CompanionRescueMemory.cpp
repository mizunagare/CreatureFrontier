#include "CompanionMemoryComponent.h"
#include "CreatureCharacter.h"
#include "CFPlayerCharacter.h"
#include "WildEncounter.h"
#include "BaseBuildingComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

namespace
{
// 来源只在此解析，治疗、界面及存档不各自维护洞窟坐标。
AWildEncounter* RescueSourceFor(UWorld* World,FName Id)
{
    if(!World || Id.IsNone())return nullptr;
    AWildEncounter* Found=nullptr;
    for(TActorIterator<AWildEncounter> It(World);It;++It)
        if(It->EncounterId==Id)
        {
            if(Found || !It->bRecordsRescueJourney || It->IsActorBeingDestroyed())return nullptr;
            Found=*It;
        }
    return Found;
}
}
UCompanionMemoryComponent::UCompanionMemoryComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickInterval=.25f;
}
FString UCompanionMemoryComponent::GetRescueSummary() const
{
    switch(RescueMemory)
    {
    case ERescueJourneyMemory::Downed:return TEXT("脱险之旅 | 等待救助");
    case ERescueJourneyMemory::Rescued:return TEXT("脱险之旅 | 一起平安回家吧");
    case ERescueJourneyMemory::Pending:return Memory==ECrystalJourneyMemory::Pending?TEXT("脱险归来 | F7先分享水晶，再聊这段经历"):TEXT("脱险归来 | 安全归家后按F7聊聊");
    case ERescueJourneyMemory::QuietThanks:return TEXT("脱险归来 | 记住了归家后的安静陪伴");
    case ERescueJourneyMemory::Celebration:return TEXT("脱险归来 | 记住了一起庆祝平安归来");
    default:return FString();
    }
}
bool UCompanionMemoryComponent::ValidateRescueSnapshot(UWorld* World,uint8 State,FName Source,bool OwnedTamed)
{
    if(State==0)return Source.IsNone();
    return State<=static_cast<uint8>(ERescueJourneyMemory::Celebration) && OwnedTamed && RescueSourceFor(World,Source);
}
void UCompanionMemoryComponent::RestoreRescueSnapshot(ERescueJourneyMemory State,FName Source)
{if(GetOwner()->HasAuthority()){RescueMemory=State;RescueSource=Source;}}
void UCompanionMemoryComponent::RecordDowned(const ACreatureCharacter* Attacker)
{
    auto* Pet=Cast<ACreatureCharacter>(GetOwner());
    auto* Hero=Pet && Pet->Tamer?Cast<ACFPlayerCharacter>(Pet->Tamer->GetPawn()):nullptr;
    if(RescueMemory!=ERescueJourneyMemory::None || !Pet || !Pet->HasAuthority() || Pet->Health>0 || Pet->Disposition!=ECreatureDisposition::Tamed ||
        !IsValid(Hero) || Hero->Health<=0 || UGameplayStatics::IsGamePaused(this) || !IsValid(Attacker) || Attacker->Health<=0 ||
        Attacker->GetWorld()!=GetWorld() || Attacker->Disposition==ECreatureDisposition::Tamed || Hero->GetDistanceTo(Pet)>800 || Hero->GetDistanceTo(Attacker)>800 ||
        FVector::Dist(Hero->GetActorLocation(),UBaseBuildingComponent::BaseOrigin())<=800 || FVector::Dist(Pet->GetActorLocation(),UBaseBuildingComponent::BaseOrigin())<=800)return;
    auto* Source=RescueSourceFor(GetWorld(),Attacker->EncounterId);
    if(!Source || Source->GetResident()!=Attacker)return;
    FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(RescueWitness),false,Hero);
    if(GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetPawnViewLocation(),Pet->GetActorLocation(),ECC_Visibility,Params) && Hit.GetActor()!=Pet)return;
    RescueSource=Source->EncounterId;RescueMemory=ERescueJourneyMemory::Downed;
}
void UCompanionMemoryComponent::RecordRescued(ACFPlayerCharacter* Hero)
{
    auto* Pet=Cast<ACreatureCharacter>(GetOwner());
    if(RescueMemory==ERescueJourneyMemory::Downed && Pet && Pet->HasAuthority() && Pet->Health>0 && IsValid(Hero) &&
        Hero->Health>0 && Pet->Tamer==Hero->GetController() && Pet->Disposition==ECreatureDisposition::Tamed && !UGameplayStatics::IsGamePaused(this))
        RescueMemory=ERescueJourneyMemory::Rescued;
}
void UCompanionMemoryComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    if(RescueMemory!=ERescueJourneyMemory::Rescued || !GetOwner()->HasAuthority() || UGameplayStatics::IsGamePaused(this))return;
    auto* Pet=Cast<ACreatureCharacter>(GetOwner());
    auto* Hero=Pet && Pet->Tamer?Cast<ACFPlayerCharacter>(Pet->Tamer->GetPawn()):nullptr;
    if(CanShareAtHome(Hero))RescueMemory=ERescueJourneyMemory::Pending;
}
