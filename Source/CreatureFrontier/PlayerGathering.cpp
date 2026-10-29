#include "CFPlayerCharacter.h"
#include "ResourceNode.h"
#include "Camera/CameraComponent.h"
#include "BaseBuildingComponent.h"
#include "Kismet/GameplayStatics.h"
#include "CraftingCatalog.h"
#include "ResourceDefinition.h"
#include "ResearchComponent.h"

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
    {
        const bool NewSample=Node->Definition && Node->Definition->Discovery==EFrontierResearch::CrystalSample && !Research->Has(EFrontierResearch::CrystalSample);
        if(Node->TryHarvest(this,this)) Feedback=NewSample?Research->GetHint():TEXT("采集了1份")+UCraftingCatalog::ItemName(Node->Definition->OutputItem)+TEXT("，来自共享储量");
        else Feedback=TEXT("无法采集：已采空、冷却中、受阻、距离过远或背包已满");
    }
    else Feedback=TEXT("瞄准3米内资源，按住M采集，松开停止");
}
