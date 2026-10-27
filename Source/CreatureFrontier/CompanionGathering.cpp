#include "CreatureCharacter.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "WorkStation.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "EngineUtils.h"

void ACreatureCharacter::TickGathering(float DeltaSeconds,ACFPlayerCharacter* Recipient)
{
    bGatheringActive=false;
    auto Park=[this]()
    {
        const FVector Waiting=WorkSite->GetActorLocation()+FVector(-180,0,90);
        if(FVector::Dist2D(Waiting,GetActorLocation())>70) NavigateTo(Waiting,35); else StopNavigation();
    };
    Stamina=FMath::Max(0.f,Stamina-DeltaSeconds*1.5f);
    if(Stamina<=0) { bResumeWorkAfterRest=true; SetActivity(ECreatureActivity::Rest); StopNavigation(); return; }
    if(ProgressSite.Get()!=WorkSite.Get()) { ResetGathering(); ProgressSite=WorkSite; }
    GatherRetry=FMath::Max(0.f,GatherRetry-DeltaSeconds);
    if(GatherRetry>0) { Park(); return; }
    auto* Node=GatherTarget.Get();
    if(Node && (Node->Remaining<=0 || !Node->Definition || Node->Definition->Specialty!=WorkSite->RequiredSpecialty ||
        Node->GetDistanceTo(WorkSite)>800 || GetWorld()->GetTimeSeconds()>GatherExpires))
    {
        FailedResource=Node; FailedResourceUntil=GetWorld()->GetTimeSeconds()+5;
        GatherTarget.Reset(); WorkProgress=0; GatherRetry=2; Park(); return;
    }
    if(!Node)
    {
        float Best=MAX_flt;
        for(TActorIterator<AResourceNode> It(GetWorld());It;++It)
            if(It->Definition && It->Remaining>0 && It->Definition->Specialty==WorkSite->RequiredSpecialty &&
                It->GetDistanceTo(WorkSite)<=800 && (FailedResource.Get()!=*It || GetWorld()->GetTimeSeconds()>=FailedResourceUntil) && GetDistanceTo(*It)<Best)
            { Node=*It; Best=GetDistanceTo(*It); }
        if(!Node) { WorkProgress=0; GatherRetry=2; Park(); return; }
        GatherTarget=Node; GatherExpires=GetWorld()->GetTimeSeconds()+8;
    }
    if(!Node->CanReach(this,220))
    {
        WorkProgress=0;
        FVector Direction=GetActorLocation()-Node->GetActorLocation(); Direction.Z=0;
        if(!Direction.Normalize()) Direction=FVector::ForwardVector;
        FVector Approach=Node->GetActorLocation()+Direction*150; Approach.Z=GetActorLocation().Z;
        NavigateTo(Approach,35); return;
    }
    StopNavigation(); GatherExpires=GetWorld()->GetTimeSeconds()+8;
    bGatheringActive=Recipient->Inventory->Count(Node->Definition->OutputItem)<999;
    WorkProgress=FMath::Min(Node->Definition->WorkSeconds,WorkProgress+DeltaSeconds*GetWorkEfficiency(WorkSite)*(Nutrition<20?.5f:1.f));
    if(WorkProgress>=Node->Definition->WorkSeconds && Node->TryHarvest(Recipient,this))
    { Recipient->RecordCompanionProduction(this,Node->Definition->OutputItem); AddExperience(1); WorkProgress=0; }
}
void ACreatureCharacter::ResetGathering()
{
    GatherTarget.Reset(); FailedResource.Reset(); GatherRetry=0; GatherExpires=0; FailedResourceUntil=0; WorkProgress=0; bGatheringActive=false;
}
FString ACreatureCharacter::GetGatheringStatus() const
{
    if(Activity!=ECreatureActivity::Work || !WorkSite || WorkSite->RequiredSpecialty==EWorkSpecialty::Farming ||
        WorkSite->RequiredSpecialty==EWorkSpecialty::Transport || WorkSite->RequiredSpecialty==EWorkSpecialty::Feeding) return FString();
    if(auto* Node=GatherTarget.Get())
    {
        const auto* Hero=Tamer?Cast<ACFPlayerCharacter>(Tamer->GetPawn()):nullptr;
        if(Hero && Node->Definition && Hero->Inventory->Count(Node->Definition->OutputItem)>=999) return TEXT("RESOURCE JOB: inventory full; stock and ready work retained");
        return TEXT("RESOURCE JOB: ")+Node->GetStatus();
    }
    return TEXT("RESOURCE JOB: seeking stock within 8m of post; no stock = no output");
}
