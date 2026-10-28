#include "CreatureCharacter.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "WorkStation.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "EngineUtils.h"
#include "CreatureDefinition.h"
#include "CompanionCargoComponent.h"

AResourceNode* ACreatureCharacter::GetFieldResource() const { return FieldResource.Get(); }

bool ACreatureCharacter::AssignFieldResource(AResourceNode* Node)
{
    if(!HasAuthority() || Health<=0 || Disposition!=ECreatureDisposition::Tamed || !IsValid(Node) ||
        Node->GetWorld()!=GetWorld() || !Node->Definition || !Node->Definition->IsValidDefinition()) return false;
    Cargo->CancelOrder();
    ResetGathering(); StopNavigation(); ClearCombatOrders();
    WorkSite=nullptr; FieldResource=Node; bResumeWorkAfterRest=false;
    SetActivity(ECreatureActivity::Work); return true;
}
void ACreatureCharacter::CancelFieldResource()
{
    FieldResource.Reset(); ResetGathering(); Cargo->CancelOrder();
}

void ACreatureCharacter::TickGathering(float DeltaSeconds,ACFPlayerCharacter* Recipient)
{
    bGatheringActive=false;
    auto* Field=FieldResource.Get();
    auto Park=[this]()
    {
        if(!IsValid(WorkSite)) { StopNavigation(); return; }
        const FVector Waiting=WorkSite->GetActorLocation()+FVector(-180,0,90);
        if(FVector::Dist2D(Waiting,GetActorLocation())>70) NavigateTo(Waiting,35); else StopNavigation();
    };
    Stamina=FMath::Max(0.f,Stamina-DeltaSeconds*1.5f);
    if(Stamina<=0) { bResumeWorkAfterRest=true; SetActivity(ECreatureActivity::Rest); StopNavigation(); return; }
    if(!Field && ProgressSite.Get()!=WorkSite.Get()) { ResetGathering(); ProgressSite=WorkSite; }
    GatherRetry=FMath::Max(0.f,GatherRetry-DeltaSeconds);
    if(GatherRetry>0) { Park(); return; }
    if(Field && (!Field->Definition || Field->Remaining<=0)) { WorkProgress=0; Park(); return; }
    auto* Node=GatherTarget.Get();
    if(Node && (Node->Remaining<=0 || !Node->Definition || (!Field && (Node->Definition->Specialty!=WorkSite->RequiredSpecialty ||
        Node->GetDistanceTo(WorkSite)>800)) || GetWorld()->GetTimeSeconds()>GatherExpires))
    {
        FailedResource=Node; FailedResourceUntil=GetWorld()->GetTimeSeconds()+5;
        GatherTarget.Reset(); WorkProgress=0; GatherRetry=2; Park(); return;
    }
    if(!Node)
    {
        if(Field) { Node=Field; }
        else
        {
        float Best=MAX_flt;
        for(TActorIterator<AResourceNode> It(GetWorld());It;++It)
            if(It->Definition && It->Remaining>0 && It->Definition->Specialty==WorkSite->RequiredSpecialty &&
                It->GetDistanceTo(WorkSite)<=800 && (FailedResource.Get()!=*It || GetWorld()->GetTimeSeconds()>=FailedResourceUntil) && GetDistanceTo(*It)<Best)
            { Node=*It; Best=GetDistanceTo(*It); }
        }
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
    bGatheringActive=Cargo->HasOrder()?Cargo->CanReceive(Node->Definition->OutputItem):Recipient->Inventory->Count(Node->Definition->OutputItem)<999;
    const float Efficiency=Field ? (Definition && Definition->WorkSpecialties.Contains(Node->Definition->Specialty)?Definition->WorkSpeed:.5f) : GetWorkEfficiency(WorkSite);
    WorkProgress=FMath::Min(Node->Definition->WorkSeconds,WorkProgress+DeltaSeconds*Efficiency*(Nutrition<20?.5f:1.f));
    if(WorkProgress>=Node->Definition->WorkSeconds && Node->TryHarvest(Recipient,this))
    { Recipient->RecordCompanionProduction(this,Node->Definition->OutputItem); AddExperience(1); WorkProgress=0; }
}
void ACreatureCharacter::ResetGathering()
{
    GatherTarget.Reset(); FailedResource.Reset(); GatherRetry=0; GatherExpires=0; FailedResourceUntil=0; WorkProgress=0; bGatheringActive=false;
}
FString ACreatureCharacter::GetGatheringStatus() const
{
    const FString CargoStatus=Cargo->Status(); if(!CargoStatus.IsEmpty()) return CargoStatus;
    if(auto* Field=FieldResource.Get())
    {
        const auto* Hero=Tamer?Cast<ACFPlayerCharacter>(Tamer->GetPawn()):nullptr;
        if(Hero && Hero->Health<=0) return TEXT("野外工作：主人倒下，暂停且不结算资源 | F 取消");
        if(Activity==ECreatureActivity::Rest) return TEXT("野外工作：原地休息，不自动回血 | 可喂食/治疗 | F 取消");
        if(Field->Remaining<=0) return TEXT("野外工作：等待资源 | ")+Field->GetStatus()+TEXT(" | F 取消");
        if(GatherRetry>0) return FString::Printf(TEXT("野外工作：路径超时，%.1f秒后重试 | F 取消"),GatherRetry);
        if(Hero && Field->Definition && Hero->Inventory->Count(Field->Definition->OutputItem)>=999)
            return TEXT("野外工作：背包已满，保留资源与工作进度 | F 取消");
        return (bGatheringActive?FString(TEXT("野外工作：采集中 | ")):FString(TEXT("野外工作：前往中/受阻，单次尝试8秒 | ")))+Field->GetStatus()+TEXT(" | F 取消");
    }
    if(Activity!=ECreatureActivity::Work || !WorkSite || WorkSite->RequiredSpecialty==EWorkSpecialty::Farming ||
        WorkSite->RequiredSpecialty==EWorkSpecialty::Transport || WorkSite->RequiredSpecialty==EWorkSpecialty::Feeding) return FString();
    if(auto* Node=GatherTarget.Get())
    {
        const auto* Hero=Tamer?Cast<ACFPlayerCharacter>(Tamer->GetPawn()):nullptr;
        if(Hero && Node->Definition && Hero->Inventory->Count(Node->Definition->OutputItem)>=999) return TEXT("采集工作：背包已满，保留资源与已完成进度");
        return TEXT("采集工作：")+Node->GetStatus();
    }
    return TEXT("采集工作：在设施8米内寻找资源，无储量则不产出");
}
