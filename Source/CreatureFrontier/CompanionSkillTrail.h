#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CompanionSkillTrail.generated.h"

// 命中后的短时空间反馈，不承担弹道、伤害或持久化。
UCLASS(Transient,NotBlueprintable)
class CREATUREFRONTIER_API ACompanionSkillTrail : public AActor
{
    GENERATED_BODY()
public:
    ACompanionSkillTrail();
    void Initialize(const FVector& From,const FVector& To,const class UCreatureDefinition* Definition);
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> Marks;
    UPROPERTY() TObjectPtr<class UStaticMesh> Sphere;
    UPROPERTY() TObjectPtr<class UStaticMesh> Cube;
    UPROPERTY() TObjectPtr<class UStaticMesh> Cone;
    FVector Origin,Destination;
    float Age=0;
};
