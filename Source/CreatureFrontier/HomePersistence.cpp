#include "HomePersistence.h"
#include "HomeModule.h"
#include "HomeLayout.h"
#include "FrontierSaveGame.h"
#include "EngineUtils.h"
#include "ResearchComponent.h"
#include "InventoryComponent.h"

FHomeRestoreTransaction::~FHomeRestoreTransaction()
{
    if(!bCommitted) for(auto* Module:Staged) if(IsValid(Module)) Module->Destroy();
}
bool FHomeRestoreTransaction::Stage(UFrontierSaveGame* Save)
{
    if(Save->FormatVersion<18) Save->HomeModules.Reset();
    if(Save->HomeModules.Num()>128) return false;
    for(auto& Record:Save->HomeModules) if(!Record.Definition.LoadSynchronous()) return false;
    for(auto& Record:Save->HomeModules)
    {
        if(Save->FormatVersion<21)
        {
            if(Record.Definition.Get()->Kind==EHomeModuleKind::Storage) return false;
            Record.StoredItems.Reset();
        }
        if(Save->FormatVersion<19)
        {
            if(Record.Definition.Get()->Kind==EHomeModuleKind::Lantern) return false;
            Record.PaidExtraItems.Reset();
        }
        if(!UResearchComponent::Meets(Save->Research,Record.Definition.Get()->RequiredResearch)) return false;
    }
    FString Reason;
    if(!HomeLayout::Validate(Save->HomeModules,Reason)) return false;
    for(const auto& Record:Save->HomeModules) if(!HomeLayout::ValidateTerrain(World,Record,Reason)) return false;
    for(const auto& Facility:Save->Facilities)
        if(!HomeLayout::SupportsFacility(Facility.Transform,Save->HomeModules)) return false;
    // Validate saved containers against the SAVED facilities, not the old live
    // world which this transaction will replace. Floor contact is allowed.
    for(const auto& Record:Save->HomeModules) if(Record.Definition.Get()->Kind==EHomeModuleKind::Storage)
        for(const auto& Facility:Save->Facilities)
        {
            const FBox FacilityBounds=FBox(FVector(-50),FVector(50)).TransformBy(Facility.Transform.ToMatrixWithScale());
            for(const auto& Part:AHomeModule::Parts(EHomeModuleKind::Storage))
            {
                const FVector Center=Record.Transform.GetLocation()+Part.Center;
                if(FBox(Center-Part.Extent+FVector(1),Center+Part.Extent-FVector(1)).Intersect(FacilityBounds)) return false;
            }
        }
    for(const auto& Record:Save->HomeModules)
    {
        auto* Module=World->SpawnActorDeferred<AHomeModule>(AHomeModule::StaticClass(),Record.Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Module) return false;
        Module->Definition=Record.Definition.Get(); Module->PersistentId=Record.Id; Module->FloorId=Record.FloorId;
        Module->PaidMaterials=Record.PaidMaterials;
        Module->PaidExtraItems=Record.PaidExtraItems;
        Module->bStaged=true; Module->SetActorEnableCollision(false); Module->FinishSpawning(Record.Transform); Staged.Add(Module);
        if(!Module->StorageInventory->Restore(Record.StoredItems)) return false;
    }
    return true;
}
void FHomeRestoreTransaction::Commit()
{
    for(TActorIterator<AHomeModule> It(World);It;++It) if(!It->bPreview && !Staged.Contains(*It)) It->Destroy();
    for(auto* Module:Staged) Module->ActivateInWorld();
    bCommitted=true;
}
