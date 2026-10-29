#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HomeModuleDefinition.h"
#include "HomeModule.generated.h"

struct FHomePart
{
    FVector Center, Extent;
};

UCLASS()
class CREATUREFRONTIER_API AHomeModule : public AActor
{
    GENERATED_BODY()
public:
    AHomeModule();
    virtual void OnConstruction(const FTransform& Transform) override;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<UHomeModuleDefinition> Definition;
    UPROPERTY(BlueprintReadOnly) FGuid PersistentId;
    UPROPERTY(BlueprintReadOnly) FGuid FloorId;
    UPROPERTY(BlueprintReadOnly) int32 PaidMaterials=0;
    UPROPERTY(BlueprintReadOnly) TMap<EFrontierItem,int32> PaidExtraItems;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<class UPointLightComponent> LanternLight;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<class UInventoryComponent> StorageInventory;
    UPROPERTY() bool bPreview=false;
    UPROPERTY() bool bStaged=false;
    // 0为非门模块，1为关闭，2为开启；保存原始字节以校验非法状态。
    UPROPERTY() uint8 DoorState=0;
    UPROPERTY() bool bUnderConstruction=false;
    UPROPERTY() FGuid ConstructionCrateId;
    UPROPERTY() float ConstructionWork=0;
    // 原位改建保留原墙定义和历史支付成本；完工或取消前不撤销旧墙承重。
    UPROPERTY() TObjectPtr<UHomeModuleDefinition> ConstructionOriginalDefinition;
    static AHomeModule* TraceConstruction(class ACFPlayerCharacter* Hero);
    FString ConstructionStatus() const;
    bool ClearsCompletedPawn(const class ACharacter* Pawn,const FVector& Center) const;
    EHomeModuleKind GetKind() const { return Definition?Definition->Kind:EHomeModuleKind::Floor; }
    static TArray<FHomePart> Parts(EHomeModuleKind Kind);
    static TArray<FHomePart> PlacementParts(EHomeModuleKind Kind);
    static FBox DoorReservation(const FTransform& Transform);
    static AHomeModule* TraceDoor(class ACFPlayerCharacter* Hero);
    bool ToggleDoor(class ACFPlayerCharacter* Hero);
    void SetPreviewTint(bool Valid);
    void ActivateInWorld();
private:
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> Meshes;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> Palette;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> DoorLeafPalette;
};
