#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FrontierHUDLayout.h"
#include "CFHUD.generated.h"
class ACFPlayerCharacter;

UCLASS()
class CREATUREFRONTIER_API ACFHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    const FFrontierHUDAudit& GetLayoutAudit() const {return Audit;}
private:
    void BeginHUDFrame(ACFPlayerCharacter* Player);
    void DrawWorldHUD(ACFPlayerCharacter* Player);
    void DrawWorldLabels(ACFPlayerCharacter* Player);
    void DrawRosterPanel(ACFPlayerCharacter* Player);
    void DrawJournalPanel(ACFPlayerCharacter* Player);
    void TrackPanel(const FBox2D& Rect);
    void DrawHUDPanel(const FBox2D& Rect,const TArray<FFrontierHUDRow>& Rows);
    float MeasureHUDRows(const TArray<FFrontierHUDRow>& Rows,float Width);
    float DrawHUDText(const FString& Text,FLinearColor Color,const FBox2D& Rect,int32 MaxLines=1);
    FFrontierHUDLayout Layout;
    FFrontierHUDAudit Audit;
    uint64 FrameSerial=0;
    void DrawStorage(ACFPlayerCharacter* Player);
    void DrawWrappedText(const FString& Text,FLinearColor Color,float X,float Y,float Width,int32 MaxLines);
    void DrawPauseMenu(ACFPlayerCharacter* Player);
    void DrawControlHelp(ACFPlayerCharacter* Player);
    void DrawBackpack(ACFPlayerCharacter* Player);
};
