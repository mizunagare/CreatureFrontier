#include "HomePersistence.h"
#include "HomeModule.h"
#include "HomeLayout.h"
#include "FrontierSaveGame.h"
#include "EngineUtils.h"

FHomeRestoreTransaction::~FHomeRestoreTransaction()
{
    if(!bCommitted) for(auto* Module:Staged) if(IsValid(Module)) Module->Destroy();
}
bool FHomeRestoreTransaction::Stage(UFrontierSaveGame* Save)
{
    if(Save->FormatVersion<18) Save->HomeModules.Reset();
    if(Save->HomeModules.Num()>128) return false;
    for(auto& Record:Save->HomeModules) if(!Record.Definition.LoadSynchronous()) return false;
    FString Reason;
    if(!HomeLayout::Validate(Save->HomeModules,Reason)) return false;
    for(const auto& Record:Save->HomeModules) if(!HomeLayout::ValidateTerrain(World,Record,Reason)) return false;
    for(const auto& Facility:Save->Facilities)
        if(!HomeLayout::SupportsFacility(Facility.Transform,Save->HomeModules)) return false;
    for(const auto& Record:Save->HomeModules)
    {
        auto* Module=World->SpawnActorDeferred<AHomeModule>(AHomeModule::StaticClass(),Record.Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Module) return false;
        Module->Definition=Record.Definition.Get(); Module->PersistentId=Record.Id; Module->FloorId=Record.FloorId;
        Module->PaidMaterials=Record.PaidMaterials;
        Module->bStaged=true; Module->SetActorEnableCollision(false); Module->FinishSpawning(Record.Transform); Staged.Add(Module);
    }
    return true;
}
void FHomeRestoreTransaction::Commit()
{
    for(TActorIterator<AHomeModule> It(World);It;++It) if(!It->bPreview && !Staged.Contains(*It)) It->Destroy();
    for(auto* Module:Staged) Module->ActivateInWorld();
    bCommitted=true;
}
