#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CFHUD.generated.h"
class ACFPlayerCharacter;

UCLASS()
class CREATUREFRONTIER_API ACFHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
private:
    void DrawStorage(ACFPlayerCharacter* Player);
    void DrawWrappedText(const FString& Text,FLinearColor Color,float X,float Y,float Width,int32 MaxLines);
    void DrawPauseMenu(ACFPlayerCharacter* Player);
    void DrawControlHelp(ACFPlayerCharacter* Player);
    void DrawBackpack(ACFPlayerCharacter* Player);
};
