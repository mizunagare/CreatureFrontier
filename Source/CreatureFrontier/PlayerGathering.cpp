#include "CFPlayerCharacter.h"
#include "ResourceNode.h"
#include "Camera/CameraComponent.h"
#include "BaseBuildingComponent.h"
#include "Kismet/GameplayStatics.h"

AResourceNode* ACFPlayerCharacter::TraceResource() const
{
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(PlayerResource),false,this);
    const FVector Start=FirstPersonCamera->GetComponentLocation();
    GetWorld()->LineTraceSingleByChannel(Hit,Start,Start+FirstPersonCamera->GetForwardVector()*340,ECC_Visibility,Params);
    return Cast<AResourceNode>(Hit.GetActor());
}
void ACFPlayerCharacter::GatherAimedResource()
{
    if(!HasAuthority() || Health<=0 || UGameplayStatics::IsGamePaused(this) || IsBackpackOpen() || IsHelpVisible() || IsJournalOpen() || Building->IsActive()) return;
    if(auto* Node=TraceResource())
        Feedback=Node->TryHarvest(this,this)?TEXT("Gathered 1 material from shared resource stock"):TEXT("Cannot gather: depleted, cooldown, blocked, too far, or inventory full");
    else Feedback=TEXT("Aim at a resource node within 3m, then press M");
}
