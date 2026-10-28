#include "HomeLayout.h"
#include "HomeModule.h"
#include "BaseBuildingComponent.h"
#include "EngineUtils.h"
#include "WorkStation.h"
#include "ResourceNode.h"
#include "WorldLoot.h"
#include "CreatureCharacter.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"

namespace
{
bool Grid(double Value,double Step) { return FMath::IsNearlyEqual(Value,FMath::GridSnap(Value,Step),.01); }
bool IsWall(EHomeModuleKind Kind) { return Kind==EHomeModuleKind::Wall || Kind==EHomeModuleKind::Doorway; }
bool At(const FHomeModuleRecord& Record,const FVector& Position,double Yaw)
{ return Record.Transform.GetLocation().Equals(Position,.01) && FMath::IsNearlyEqual(Record.Transform.Rotator().Yaw,Yaw,.01); }
bool WallAt(const TArray<FHomeModuleRecord>& Records,const FVector& Position,double Yaw)
{
    for(const auto& Other:Records)
        if(auto* Definition=Other.Definition.Get();Definition && IsWall(Definition->Kind) && At(Other,Position,Yaw)) return true;
    return false;
}
}
bool HomeLayout::IsCanonical(const FHomeModuleRecord& Record)
{
    const auto* Definition=Record.Definition.Get();
    if(!Record.Id.IsValid() || !Definition || !Definition->IsValidDefinition() || Record.PaidMaterials<2 || Record.PaidMaterials>100 || Record.Transform.ContainsNaN() ||
        !Record.Transform.GetRotation().IsNormalized() || !Record.Transform.GetScale3D().Equals(FVector::OneVector,.001)) return false;
    const FVector Position=Record.Transform.GetLocation(); const auto Rotation=Record.Transform.Rotator();
    for(const auto& Cost:Record.PaidExtraItems) if(Cost.Key==EFrontierItem::Material || Cost.Key>=EFrontierItem::Count || Cost.Value<=0 || Cost.Value>999) return false;
    if(!UInventoryComponent::Validate(Record.StoredItems) || (Definition->Kind!=EHomeModuleKind::Storage && !Record.StoredItems.IsEmpty())) return false;
    if(FVector::Dist2D(Position,UBaseBuildingComponent::BaseOrigin())>1200 || !FMath::IsNearlyZero(Rotation.Pitch,.01) || !FMath::IsNearlyZero(Rotation.Roll,.01)) return false;
    if(IsWall(Definition->Kind))
        return FMath::IsNearlyEqual(Position.Z,-30.,.01) &&
            ((FMath::IsNearlyZero(Rotation.Yaw,.01) && Grid(Position.X,400) && Grid(Position.Y-200,400)) ||
             (FMath::IsNearlyEqual(Rotation.Yaw,90.,.01) && Grid(Position.X-200,400) && Grid(Position.Y,400)));
    if(Definition->Kind==EHomeModuleKind::Lantern || Definition->Kind==EHomeModuleKind::Storage) return FMath::IsNearlyZero(Rotation.Yaw,.01) && Grid(Position.X,100) && Grid(Position.Y,100) && FMath::IsNearlyEqual(Position.Z,-30.,.01);
    return FMath::IsNearlyZero(Rotation.Yaw,.01) && Grid(Position.X,400) && Grid(Position.Y,400) &&
        FMath::IsNearlyEqual(Position.Z,Definition->Kind==EHomeModuleKind::Floor?-40.:280.,.01);
}
bool HomeLayout::Validate(const TArray<FHomeModuleRecord>& Records,FString& Reason)
{
    if(Records.Num()>128) { Reason=TEXT("房屋最多128个模块"); return false; }
    TSet<FGuid> Ids;
    for(int32 Index=0;Index<Records.Num();++Index)
    {
        const auto& Record=Records[Index];
        if(!IsCanonical(Record) || Ids.Contains(Record.Id)) { Reason=TEXT("模块变换或定义无效，或身份重复"); return false; }
        Ids.Add(Record.Id);
        for(int32 Other=0;Other<Index;++Other)
            if(Record.Transform.GetLocation().Equals(Records[Other].Transform.GetLocation(),.01) &&
                Record.Transform.GetRotation().Equals(Records[Other].Transform.GetRotation(),.001))
            { Reason=TEXT("模块安装位已被占用"); return false; }
        const auto Kind=Record.Definition.Get()->Kind;
        if(Kind==EHomeModuleKind::Floor)
        { if(Record.FloorId.IsValid()) { Reason=TEXT("地面地板不能依附父地板"); return false; } continue; }
        const FHomeModuleRecord* Floor=Records.FindByPredicate([&Record](const FHomeModuleRecord& Other){return Other.Id==Record.FloorId;});
        if(!Floor || !Floor->Definition.Get() || Floor->Definition.Get()->Kind!=EHomeModuleKind::Floor)
        { Reason=TEXT("模块需要支撑地板"); return false; }
        const FVector Center=Floor->Transform.GetLocation(); const FVector Position=Record.Transform.GetLocation();
        if(Kind==EHomeModuleKind::Roof)
        {
            if(!FVector(Position.X,Position.Y,-40).Equals(Center,.01) ||
                !((WallAt(Records,FVector(Center.X,Center.Y-200,-30),0) && WallAt(Records,FVector(Center.X,Center.Y+200,-30),0)) ||
                  (WallAt(Records,FVector(Center.X-200,Center.Y,-30),90) && WallAt(Records,FVector(Center.X+200,Center.Y,-30),90))))
            { Reason=TEXT("屋顶需要所在楼层两面相对的墙支撑"); return false; }
        }
        else if(Kind==EHomeModuleKind::Lantern || Kind==EHomeModuleKind::Storage)
        {
            const float Inset=Kind==EHomeModuleKind::Storage?145:170;
            if(FMath::Abs(Position.X-Center.X)>Inset || FMath::Abs(Position.Y-Center.Y)>Inset)
            { Reason=TEXT("家具必须完全放在支撑地板内"); return false; }
        }
        else
        {
            const bool AlongX=FMath::IsNearlyZero(Record.Transform.Rotator().Yaw,.01);
            if(!(AlongX?FMath::IsNearlyEqual(Position.X,Center.X,.01) && FMath::IsNearlyEqual(FMath::Abs(Position.Y-Center.Y),200.,.01):
                FMath::IsNearlyEqual(Position.Y,Center.Y,.01) && FMath::IsNearlyEqual(FMath::Abs(Position.X-Center.X),200.,.01)))
            { Reason=TEXT("墙需吸附到支撑地板边缘"); return false; }
        }
    }
    Reason=TEXT("结构支撑有效"); return true;
}
FGuid HomeLayout::FindFloor(const FVector& Point,const TArray<FHomeModuleRecord>& Records)
{
    for(const auto& Record:Records)
        if(Record.Definition.Get() && Record.Definition.Get()->Kind==EHomeModuleKind::Floor &&
            FMath::Abs(Point.X-Record.Transform.GetLocation().X)<=200.01 && FMath::Abs(Point.Y-Record.Transform.GetLocation().Y)<=200.01) return Record.Id;
    return FGuid();
}
bool HomeLayout::SupportsFacility(const FTransform& Facility,const TArray<FHomeModuleRecord>& Records)
{
    if(FMath::Abs(Facility.GetLocation().Z+15)<=2) return true; // Preserve the original facility transform tolerance.
    if(!FMath::IsNearlyEqual(Facility.GetLocation().Z,5.,.01)) return false;
    for(double X:{-95.,95.}) for(double Y:{-45.,45.})
        if(!FindFloor(Facility.GetLocation()+Facility.GetRotation().RotateVector(FVector(X,Y,0)),Records).IsValid()) return false;
    return true;
}
TArray<FHomeModuleRecord> HomeLayout::Collect(UWorld* World)
{
    TArray<FHomeModuleRecord> Result;
    for(TActorIterator<AHomeModule> It(World);It;++It) if(!It->bPreview)
    {
        FHomeModuleRecord Record; Record.Id=It->PersistentId; Record.FloorId=It->FloorId;
        Record.Definition=It->Definition.Get(); Record.Transform=It->GetActorTransform(); Record.PaidMaterials=It->PaidMaterials; Record.PaidExtraItems=It->PaidExtraItems; Result.Add(Record);
        if(It->GetKind()==EHomeModuleKind::Storage) Result.Last().StoredItems=It->StorageInventory->GetStacks();
    }
    return Result;
}
bool HomeLayout::ValidateTerrain(UWorld* World,const FHomeModuleRecord& Record,FString& Reason)
{
    if(!IsCanonical(Record)) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HomeTerrain),false);
    // 快照实体可以在读档时改变位置，预设世界则不能改变。
    for(TActorIterator<AActor> It(World);It;++It)
    {
        const auto* Station=Cast<AWorkStation>(*It);
        if(It->IsA<AHomeModule>() || (Station && Station->bPlayerBuilt) || It->IsA<AResourceNode>() || It->IsA<AWorldLoot>() || It->IsA<ACreatureCharacter>() || It->IsA<ACFPlayerCharacter>()) Params.AddIgnoredActor(*It);
    }
    const auto Objects=FCollisionObjectQueryParams(ECC_WorldStatic);
    for(const auto& Part:AHomeModule::Parts(Record.Definition.Get()->Kind))
        if(World->OverlapAnyTestByObjectType(Record.Transform.GetLocation()+Record.Transform.GetRotation().RotateVector(Part.Center),Record.Transform.GetRotation(),Objects,
            FCollisionShape::MakeBox(Part.Extent-FVector(1)),Params))
        { Reason=TEXT("模块与场景几何体相交"); return false; }
    if(Record.Definition.Get()->Kind==EHomeModuleKind::Floor)
        for(double X:{-190.,190.}) for(double Y:{-190.,190.})
        {
            const FVector Corner=Record.Transform.GetLocation()+FVector(X,Y,0); FHitResult Hit;
            if(!World->LineTraceSingleByChannel(Hit,Corner+FVector(0,0,15),Corner-FVector(0,0,30),ECC_Visibility,Params) || Hit.ImpactNormal.Z<.98 || !FMath::IsNearlyEqual(Hit.ImpactPoint.Z,-50.,2.))
            { Reason=TEXT("地板四角下方均需平坦地面"); return false; }
        }
    return true;
}
