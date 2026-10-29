#include "PlayerGatheringComponent.h"
#include "CFPlayerCharacter.h"
#include "ResourceNode.h"
#include "BaseBuildingComponent.h"
#include "StorageInteractionComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

UPlayerGatheringComponent::UPlayerGatheringComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.bTickEvenWhenPaused=true;PrimaryComponentTick.TickGroup=TG_PrePhysics;
}
void UPlayerGatheringComponent::BeginPlay()
{
    Super::BeginPlay();
    if(FSlateApplication::IsInitialized())ActivationHandle=FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this,&UPlayerGatheringComponent::HandleActivationChanged);
}
void UPlayerGatheringComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Stop();
    if(ActivationHandle.IsValid() && FSlateApplication::IsInitialized())FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle);
    Super::EndPlay(Reason);
}
bool UPlayerGatheringComponent::CanGather(const ACFPlayerCharacter* Hero) const
{
    const auto* PC=Hero?Cast<APlayerController>(Hero->GetController()):nullptr;
    return Hero && PC && Hero->HasAuthority() && Hero->IsLocallyControlled() && PC->GetPawn()==Hero &&
        Hero->Health>0 && !PC->IsMoveInputIgnored() && !UGameplayStatics::IsGamePaused(this) &&
        !Hero->IsBackpackOpen() && !Hero->IsHelpVisible() && !Hero->IsJournalOpen() && !Hero->IsRosterOpen() &&
        !Hero->IsPauseMenuOpen() && !Hero->Storage->IsOpen() && !Hero->Building->IsActive();
}
void UPlayerGatheringComponent::Start()
{
    if(bHeld)return;
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());
    if(!CanGather(Hero))return;
    auto* Node=Hero->TraceResource();
    if(!Node || Node->Remaining<=0 || !Node->CanReach(Hero,300)){Hero->GatherAimedResource();return;}
    Target=Node;bHeld=true;SetComponentTickEnabled(true);
    const int32 Before=Node->Remaining;Hero->GatherAimedResource();
    if(Node->Remaining==Before && Node->HarvestCooldown<=0)Stop();
}
void UPlayerGatheringComponent::Stop()
{bHeld=false;Target.Reset();SetComponentTickEnabled(false);}
void UPlayerGatheringComponent::HandleActivationChanged(bool Active)
{if(!Active)Stop();}
void UPlayerGatheringComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(DeltaTime,TickType,Tick);
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());auto* Node=Target.Get();
    if(!bHeld || !CanGather(Hero) || !IsValid(Node) || Node->IsActorBeingDestroyed() || Node->Remaining<=0 ||
        Hero->TraceResource()!=Node || !Node->CanReach(Hero,300))
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("CFGatherHoldTest")))UE_LOG(LogTemp,Display,TEXT("CF_GATHER_STOP allowed=%d held=%d target=%s trace=%s reach=%d"),CanGather(Hero),bHeld,*GetNameSafe(Node),Hero?*GetNameSafe(Hero->TraceResource()):TEXT("无"),IsValid(Node) && Node->CanReach(Hero,300));
        Stop();return;
    }
    // 不自建产出计时或补算漏帧；原资源节点的共享冷却仍是唯一采集节奏。
    if(Node->HarvestCooldown>0)return;
    const int32 Before=Node->Remaining;Hero->GatherAimedResource();
    if(Node->Remaining==Before)Stop();
}
