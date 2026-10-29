#include "HomePersistence.h"
#include "HomeModule.h"
#include "HomeLayout.h"
#include "FrontierSaveGame.h"
#include "EngineUtils.h"
#include "ResearchComponent.h"
#include "InventoryComponent.h"
#include "CFPlayerCharacter.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

namespace
{
bool DoorClearsPawn(const FHomeModuleRecord& Door,const FTransform& Pawn,const ACharacter* Defaults)
{
    if(Pawn.ContainsNaN() || !Defaults)return false;
    const auto* Capsule=Defaults->GetCapsuleComponent();
    const FVector Scale=Pawn.GetScale3D().GetAbs();
    const double Radius=Capsule->GetUnscaledCapsuleRadius()*FMath::Max(Scale.X,Scale.Y);
    const double Half=FMath::Max(Radius,Capsule->GetUnscaledCapsuleHalfHeight()*Scale.Z);
    const FVector Point=Door.Transform.InverseTransformPosition(Pawn.GetLocation());
    const FVector Center=Door.DoorState==2?FVector(-110,110,125):FVector(0,0,125);
    const FVector Extent=Door.DoorState==2?FVector(8,109,124):FVector(109,8,124);
    const double X=FMath::Max(0.,FMath::Abs(Point.X-Center.X)-Extent.X);
    const double Y=FMath::Max(0.,FMath::Abs(Point.Y-Center.Y)-Extent.Y);
    const double Z=FMath::Max(0.,FMath::Abs(Point.Z-Center.Z)-(Half-Radius)-Extent.Z);
    return X*X+Y*Y+Z*Z>=Radius*Radius;
}
}

FHomeRestoreTransaction::~FHomeRestoreTransaction()
{
    if(!bCommitted) for(auto* Module:Staged) if(IsValid(Module)) Module->Destroy();
}
AHomeModule* FHomeRestoreTransaction::Find(FGuid Id) const
{
    for(auto* Module:Staged) if(Module->PersistentId==Id) return Module;
    return nullptr;
}
bool FHomeRestoreTransaction::Stage(UFrontierSaveGame* Save)
{
    if(Save->FormatVersion<18) Save->HomeModules.Reset();
    if(Save->HomeModules.Num()>128) return false;
    for(auto& Record:Save->HomeModules) if(!Record.Definition.LoadSynchronous()) return false;
    for(auto& Record:Save->HomeModules)
    {
        if(Save->FormatVersion<33 && Record.Definition.Get()->Kind==EHomeModuleKind::Fence)return false;
        if(Save->FormatVersion<32 && Record.Definition.Get()->Kind==EHomeModuleKind::Window)return false;
        if(Save->FormatVersion<30)
        {if(Record.Definition.Get()->Kind==EHomeModuleKind::Door)return false;Record.DoorState=0;}
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
    for(const auto& Door:Save->HomeModules)if(Door.Definition.Get()->Kind==EHomeModuleKind::Door)
    {
        if(!DoorClearsPawn(Door,Save->PlayerTransform,GetDefault<ACFPlayerCharacter>()))return false;
        for(const auto& Creature:Save->Creatures)
        {
            auto* Class=Creature.CreatureClass.LoadSynchronous();
            if(!Class || !Class->IsChildOf(ACharacter::StaticClass()) || !DoorClearsPawn(Door,Creature.Transform,Cast<ACharacter>(Class->GetDefaultObject())))return false;
        }
    }
    for(const auto& Record:Save->HomeModules) if(!HomeLayout::ValidateTerrain(World,Record,Reason)) return false;
    for(const auto& Facility:Save->Facilities)
        if(!HomeLayout::SupportsFacility(Facility.Transform,Save->HomeModules)) return false;
    for(const auto& Facility:Save->Facilities)
        if(!HomeLayout::ClearsDoorReservations(FBox(FVector(-50),FVector(50)).TransformBy(Facility.Transform.ToMatrixWithScale()),Save->HomeModules))return false;
    // 使用存档中的设施校验存档容器，而非即将被本事务替换的旧世界。
    // 允许容器与地板接触。
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
        Module->DoorState=Record.DoorState;
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
