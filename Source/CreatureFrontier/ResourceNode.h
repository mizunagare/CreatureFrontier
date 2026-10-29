#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ResourceNode.generated.h"
class UResourceDefinition;
class ACFPlayerCharacter;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS()
class CREATUREFRONTIER_API AResourceNode : public AActor
{
    GENERATED_BODY()
public:
    AResourceNode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<UResourceDefinition> Definition;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FGuid PersistentId;
    UPROPERTY(BlueprintReadOnly) int32 Remaining=0;
    UPROPERTY(BlueprintReadOnly) float HarvestCooldown=0;
    UPROPERTY(BlueprintReadOnly) float RegrowRemaining=0;
    bool CanReach(const AActor* Harvester,float Range) const;
    bool TryHarvest(ACFPlayerCharacter* Recipient,AActor* Harvester);
    FString GetStatus() const;
    void Refresh();
private:
    FString GetWorldLabel() const;
    // 对外和存档继续使用原浮点字段；内部双精度避免极短帧反复扣减造成累计偏差。
    double PreciseRegrowRemaining=0;
    float PublishedRegrowRemaining=-1;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY() TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> ResourceMaterial;
};

// 仅用于新世界，或迁移尚未记录资源历史的旧快照。
void SeedFrontierResources(UWorld* World);
bool SpawnStarterResources(UWorld* World,TArray<AResourceNode*>& OutNodes);
bool MakeStarterResourceRecords(TArray<struct FResourceSaveRecord>& OutRecords);
// 避开初始地图中封住旧矿床中心的水晶基座；新世界和旧档修复共用。
FVector StarterStoneMineLocation();
