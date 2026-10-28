#include "PlayerPaceComponent.h"
#include "CFPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"

UPlayerPaceComponent::UPlayerPaceComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.bTickEvenWhenPaused=true;PrimaryComponentTick.TickGroup=TG_PrePhysics;
}
void UPlayerPaceComponent::BeginPlay()
{
    Super::BeginPlay();
    if(FSlateApplication::IsInitialized())ActivationHandle=FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this,&UPlayerPaceComponent::HandleActivationChanged);
}
void UPlayerPaceComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    StopSlowWalk();
    if(ActivationHandle.IsValid() && FSlateApplication::IsInitialized())FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle);
    Super::EndPlay(Reason);
}
void UPlayerPaceComponent::StartSlowWalk()
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());auto* PC=Hero?Cast<APlayerController>(Hero->GetController()):nullptr;
    if(bSlowWalking || !Hero || !PC || !Hero->IsLocallyControlled() || Hero->Health<=0 || UGameplayStatics::IsGamePaused(this) || PC->IsMoveInputIgnored())return;
    auto* Movement=Hero->GetCharacterMovement();
    if(Movement->MovementMode==MOVE_None || !FMath::IsFinite(SlowWalkSpeed) || SlowWalkSpeed<=0 || !FMath::IsFinite(Movement->MaxWalkSpeed) || Movement->MaxWalkSpeed<=0)return;
    PreviousSpeed=Movement->MaxWalkSpeed;bSlowWalking=true;Movement->MaxWalkSpeed=FMath::Min(PreviousSpeed,SlowWalkSpeed);SetComponentTickEnabled(true);
}
void UPlayerPaceComponent::StopSlowWalk()
{
    if(bSlowWalking)if(auto* Hero=Cast<ACFPlayerCharacter>(GetOwner()))Hero->GetCharacterMovement()->MaxWalkSpeed=PreviousSpeed;
    bSlowWalking=false;SetComponentTickEnabled(false);
}
void UPlayerPaceComponent::HandleActivationChanged(bool Active)
{if(!Active)StopSlowWalk();}
void UPlayerPaceComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(DeltaTime,TickType,Tick);
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());auto* PC=Hero?Cast<APlayerController>(Hero->GetController()):nullptr;
    if(!Hero || !PC || PC->GetPawn()!=Hero || Hero->Health<=0 || UGameplayStatics::IsGamePaused(this) || PC->IsMoveInputIgnored() || Hero->GetCharacterMovement()->MovementMode==MOVE_None)StopSlowWalk();
}
