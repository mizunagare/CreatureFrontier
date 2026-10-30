#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CFGameMode.generated.h"

UCLASS()
class CREATUREFRONTIER_API ACFGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ACFGameMode();
    virtual void BeginPlay() override;
    // 仅使用本关卡实际加载的放置对象；同物种多锚点的旧档不能猜测归属。
    bool ResolvePlacedCreatureHome(const class UCreatureDefinition* Definition,FVector& Home) const;
private:
    TMap<FSoftObjectPath,TArray<FVector>> PlacedCreatureHomes;
};

