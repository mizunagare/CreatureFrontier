#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldLoot.generated.h"
class ACFPlayerCharacter;

UCLASS()
class CREATUREFRONTIER_API AWorldLoot : public AActor
{
    GENERATED_BODY()
public:
    AWorldLoot();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY() int32 Materials = 2;
    UPROPERTY() int32 Medicine = 0;
    bool Collect(ACFPlayerCharacter* Hero);
private:
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Marker;
    UPROPERTY() TObjectPtr<class UTextRenderComponent> Label;
};
