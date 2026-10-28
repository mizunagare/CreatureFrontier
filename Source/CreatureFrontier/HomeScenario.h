#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HomeScenario.generated.h"
class UWorld;
class ACFPlayerCharacter;

// 打包物理测试使用持续移动输入，不依赖帧率。
UCLASS()
class UHomeMovementProbe : public UActorComponent
{
    GENERATED_BODY()
public:
    UHomeMovementProbe();
    FVector Direction=FVector::ZeroVector;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Tick) override;
};
void RunHomeScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode=0);
void RunHomeWindowScenario(UWorld* World,ACFPlayerCharacter* Hero);
