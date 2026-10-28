#include "CFHUD.h"
#include "CFPlayerCharacter.h"
#include "ResourceNode.h"
#include "WorkStation.h"
#include "WildEncounter.h"
#include "WorldLoot.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

void ACFHUD::DrawWorldLabels(ACFPlayerCharacter* Player)
{
    if(Audit.bInterface || Audit.bPaused)return;
    auto* PC=GetOwningPlayerController();if(!PC)return;
    FVector Eye;FRotator Rotation;PC->GetPlayerViewPoint(Eye,Rotation);
    struct FLabel {AActor* Actor;UTextRenderComponent* Text;float Distance;};
    TArray<FLabel> Labels;
    auto Add=[&](AActor* Actor,float Range)
    {
        auto* Text=Actor->FindComponentByClass<UTextRenderComponent>();
        const float Distance=FVector::Dist(Eye,Actor->GetActorLocation());
        if(Text && Distance<=Range && !Text->Text.IsEmpty())Labels.Add({Actor,Text,Distance});
    };
    for(TActorIterator<AWorkStation> It(GetWorld());It;++It)Add(*It,900);
    for(TActorIterator<AResourceNode> It(GetWorld());It;++It)Add(*It,600);
    for(TActorIterator<AWildEncounter> It(GetWorld());It;++It)Add(*It,900);
    for(TActorIterator<AWorldLoot> It(GetWorld());It;++It)Add(*It,600);
    Labels.Sort([](const FLabel& A,const FLabel& B){return A.Distance==B.Distance?A.Actor->GetName()<B.Actor->GetName():A.Distance<B.Distance;});
    int32 Drawn=0;
    for(const auto& Label:Labels)
    {
        // 默认离线3D字体不含汉字，改用支持中文回退字体的屏幕投影文字。
        // 名牌仍需距离、正面、视线检测，且避开准星、界面和其他名牌。
        const FVector Point=Label.Text->GetComponentLocation();FVector2D Screen;
        if(FVector::DotProduct(Point-Eye,Rotation.Vector())<=0 || !PC->ProjectWorldLocationToScreen(Point,Screen))continue;
        FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(ChineseWorldLabel),false,Player);
        if(GetWorld()->LineTraceSingleByChannel(Hit,Eye,Point,ECC_Visibility,Query) && Hit.GetActor()!=Label.Actor)continue;
        const FString Text=Label.Text->Text.ToString();float Width=0,Height=0;
        GetTextSize(Text,Width,Height,GEngine->GetSmallFont(),Layout.FontScale);
        // 字形测量与断行存在取整差异，给短中文名预留额外宽度。
        Width=FMath::Min(Width+8*Layout.Unit,220*Layout.Unit)+12*Layout.Unit;
        const auto Rect=FFrontierHUDLayout::Box(Screen.X-Width*.5f,Screen.Y-Layout.Line,Width,Layout.Line+8*Layout.Unit);
        if(!Layout.Contains(Rect) || FFrontierHUDLayout::Overlap(Rect,Layout.Center))continue;
        bool Blocked=false;for(const auto& Panel:Audit.Panels)Blocked |= FFrontierHUDLayout::Overlap(Rect,Panel);
        if(Blocked)continue;
        TrackPanel(Rect);DrawRect(FLinearColor(.015f,.025f,.04f,.85f),Rect.Min.X,Rect.Min.Y,Rect.GetSize().X,Rect.GetSize().Y);
        DrawHUDText(Text,FLinearColor(.85f,.95f,.85f),FFrontierHUDLayout::Box(Rect.Min.X+6*Layout.Unit,Rect.Min.Y+4*Layout.Unit,Width-12*Layout.Unit,Layout.Line),1);
        if(++Drawn==6)break;
    }
}
