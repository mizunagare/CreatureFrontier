#include "CFHUD.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "CanvasTypes.h"

void ACFHUD::TrackPanel(const FBox2D& Rect)
{
    Audit.bFits &= Layout.Contains(Rect);
    for(const auto& Other:Audit.Panels)Audit.bNoOverlap &= !FFrontierHUDLayout::Overlap(Rect,Other);
    if(!Audit.bInterface)Audit.bCenterClear &= !FFrontierHUDLayout::Overlap(Rect,Layout.Center);
    Audit.Panels.Add(Rect);
}
float ACFHUD::DrawHUDText(const FString& Text,FLinearColor Color,const FBox2D& Rect,int32 MaxLines)
{
    if(Text.IsEmpty() || MaxLines<=0 || Rect.GetSize().X<4)return 0;
    auto* Font=GEngine->GetSmallFont();FTextSizingParameters Params(0,0,Rect.GetSize().X,0,Font);Params.Scaling=FVector2D(Layout.FontScale);
    TArray<FWrappedStringElement> Lines;Canvas->WrapString(Params,0,FStringView(Text),Lines);
    const int32 Count=FMath::Min3(MaxLines,Lines.Num(),static_cast<int32>(FMath::FloorToInt(Rect.GetSize().Y/Layout.Line)));
    if(Count<Lines.Num())++Audit.Elided;
    for(int32 Index=0;Index<Count;++Index)
    {
        FString Line=Lines[Index].Value;
        bool Ellipsis=Index==Count-1 && Count<Lines.Num();float W=0,H=0;GetTextSize(Line,W,H,Font,Layout.FontScale);
        if(W>Rect.GetSize().X)Ellipsis=true;
        if(Ellipsis)
        {
            while(!Line.IsEmpty())
            {
                GetTextSize(Line+TEXT("..."),W,H,Font,Layout.FontScale);if(W<=Rect.GetSize().X)break;
                const uint32 Last=Line[Line.Len()-1];Line.LeftChopInline(1);
                if(Last>=0xDC00 && Last<=0xDFFF && !Line.IsEmpty())Line.LeftChopInline(1);
            }
            Line+=TEXT("...");GetTextSize(Line,W,H,Font,Layout.FontScale);
        }
        const float Y=Rect.Min.Y+Index*Layout.Line;
        Audit.bFits &= Layout.Contains(FFrontierHUDLayout::Box(Rect.Min.X,Y,FMath::Max(1.f,W),FMath::Max(1.f,H))) && W<=Rect.GetSize().X+.5f && H<=Layout.Line+.5f;
        DrawText(Line,Color,Rect.Min.X,Y,Font,Layout.FontScale);++Audit.TextLines;
    }
    return Count*Layout.Line;
}
void ACFHUD::DrawHUDPanel(const FBox2D& Rect,const TArray<FFrontierHUDRow>& Rows)
{
    TrackPanel(Rect);DrawRect(FLinearColor(.018f,.032f,.048f,.95f),Rect.Min.X,Rect.Min.Y,Rect.GetSize().X,Rect.GetSize().Y);
    float Y=Rect.Min.Y+Layout.Padding;
    for(const auto& Row:Rows)
    {
        const float Available=Rect.Max.Y-Layout.Padding-Y;if(Available<Layout.Line){++Audit.Elided;break;}
        Y+=DrawHUDText(Row.Text,Row.Color,FFrontierHUDLayout::Box(Rect.Min.X+Layout.Padding,Y,Rect.GetSize().X-2*Layout.Padding,Available),Row.MaxLines);
    }
}
float ACFHUD::MeasureHUDRows(const TArray<FFrontierHUDRow>& Rows,float Width)
{
    int32 Count=0;for(const auto& Row:Rows)
    {FTextSizingParameters Params(0,0,Width,0,GEngine->GetSmallFont());Params.Scaling=FVector2D(Layout.FontScale);TArray<FWrappedStringElement> Lines;Canvas->WrapString(Params,0,FStringView(Row.Text),Lines);Count+=FMath::Min(Row.MaxLines,Lines.Num());}
    return Count*Layout.Line+2*Layout.Padding;
}
