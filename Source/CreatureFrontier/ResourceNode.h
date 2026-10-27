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
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY() TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> ResourceMaterial;
};

// Used only for a new world or migrating snapshots that predate resource history.
void SeedFrontierResources(UWorld* World);
bool SpawnStarterResources(UWorld* World,TArray<AResourceNode*>& OutNodes);
bool MakeStarterResourceRecords(TArray<struct FResourceSaveRecord>& OutRecords);
