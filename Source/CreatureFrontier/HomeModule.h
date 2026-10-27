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
    UPROPERTY() bool bPreview=false;
    UPROPERTY() bool bStaged=false;
    EHomeModuleKind GetKind() const { return Definition?Definition->Kind:EHomeModuleKind::Floor; }
    static TArray<FHomePart> Parts(EHomeModuleKind Kind);
    void SetPreviewTint(bool Valid);
    void ActivateInWorld();
private:
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> Meshes;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> Palette;
};
