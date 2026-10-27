#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CFHUD.generated.h"

UCLASS()
class CREATUREFRONTIER_API ACFHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};
