#include "ResourcePersistence.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "FrontierSaveGame.h"
#include "EngineUtils.h"

FResourceRestoreTransaction::~FResourceRestoreTransaction()
{
    if(!bCommitted) for(auto* Node:Staged) if(IsValid(Node)) Node->Destroy();
}
bool FResourceRestoreTransaction::Stage(const UFrontierSaveGame* Save)
{
    if(Save->FormatVersion<17)
    {
        const bool Result=SpawnStarterResources(World,Staged);
        for(auto* Node:Staged) Node->SetActorEnableCollision(false);
        return Result;
    }
    if(Save->ResourceNodes.Num()>256) return false;
    TSet<FGuid> Ids;
    for(const auto& Record:Save->ResourceNodes)
    {
        auto* Definition=Record.Definition.LoadSynchronous();
        if(!Record.Id.IsValid() || Ids.Contains(Record.Id) || !Definition || !Definition->IsValidDefinition() ||
            Record.Transform.ContainsNaN() || !Record.Transform.GetRotation().IsNormalized() || !Record.Transform.GetScale3D().Equals(FVector::OneVector,.001f) ||
            Record.Transform.GetLocation().GetAbsMax()>100000 || Record.Remaining<0 || Record.Remaining>Definition->Capacity ||
            !FMath::IsFinite(Record.HarvestCooldown) || Record.HarvestCooldown<0 || Record.HarvestCooldown>Definition->HarvestInterval ||
            !FMath::IsFinite(Record.RegrowRemaining) || Record.RegrowRemaining<0 || Record.RegrowRemaining>Definition->RegrowSeconds ||
            (Record.Remaining>0 && Record.RegrowRemaining!=0) ||
            (Record.Remaining==0 && Definition->RegrowSeconds>0 && Record.RegrowRemaining<=0)) return false;
        Ids.Add(Record.Id);
        auto* Node=World->SpawnActorDeferred<AResourceNode>(AResourceNode::StaticClass(),Record.Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Node) return false;
        Node->Definition=Definition; Node->PersistentId=Record.Id; Node->SetActorEnableCollision(false);
        Node->FinishSpawning(Record.Transform);
        Node->Remaining=Record.Remaining; Node->HarvestCooldown=Record.HarvestCooldown; Node->RegrowRemaining=Record.RegrowRemaining;
        Node->Refresh(); Staged.Add(Node);
    }
    return true;
}
void FResourceRestoreTransaction::Commit()
{
    for(TActorIterator<AResourceNode> It(World);It;++It) if(!Staged.Contains(*It)) It->Destroy();
    for(auto* Node:Staged) Node->SetActorEnableCollision(true);
    bCommitted=true;
}
