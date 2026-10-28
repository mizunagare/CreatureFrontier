#pragma once
#include "CoreMinimal.h"

enum class EFrontierHUDContext : uint8 { None, Companion, Wild, Resource, Garden, FoodStore, Storage, Facility, Building, Door };
struct FFrontierHUDLayout
{
    FVector2D Viewport;
    float Unit=1,Margin=16,Line=20,FontScale=1.12f,Padding=12;
    FBox2D Vitals,Feedback,Objective,Research,Context,Alert,Center;
    static FBox2D Box(float X,float Y,float W,float H){return FBox2D(FVector2D(X,Y),FVector2D(X+W,Y+H));}
    static FFrontierHUDLayout Build(float W,float H,bool Interface)
    {
        FFrontierHUDLayout L;L.Viewport=FVector2D(W,H);L.Unit=FMath::Clamp(FMath::Min(W/1280.f,H/720.f),.85f,1.5f);
        L.Margin=16*L.Unit;L.Line=20*L.Unit;L.FontScale=1.12f*L.Unit;L.Padding=12*L.Unit;
        const float Left=FMath::Min(400*L.Unit,W*.32f),Right=FMath::Min(460*L.Unit,W*.36f),M=L.Margin,P=L.Padding;
        L.Vitals=Box(M,M,Left,8*L.Line+2*P);L.Feedback=Box(M,L.Vitals.Max.Y+M,Left,3*L.Line+2*P);
        L.Objective=Box(M,H-M-(6*L.Line+2*P),Left,6*L.Line+2*P);
        L.Research=Box(W-M-Right,M,Right,4*L.Line+2*P);
        L.Context=Box(W-M-Right,L.Research.Max.Y+M,Right,H-M-(L.Research.Max.Y+M));
        L.Alert=Interface?L.Objective:Box(L.Vitals.Max.X+M,M,L.Research.Min.X-L.Vitals.Max.X-2*M,4*L.Line+2*P);
        L.Center=Box(W*.4f,H*.3f,W*.2f,H*.4f);return L;
    }
    bool Contains(const FBox2D& R) const
    {return R.bIsValid && R.Min.X>=0 && R.Min.Y>=0 && R.Max.X<=Viewport.X+.1 && R.Max.Y<=Viewport.Y+.1 && R.GetSize().X>0 && R.GetSize().Y>0;}
    static bool Overlap(const FBox2D& A,const FBox2D& B)
    {return A.Min.X<B.Max.X-.1 && A.Max.X>B.Min.X+.1 && A.Min.Y<B.Max.Y-.1 && A.Max.Y>B.Min.Y+.1;}
};
struct FFrontierHUDRow
{
    FString Text;FLinearColor Color=FLinearColor::White;int32 MaxLines=1;
    FFrontierHUDRow(FString In,FLinearColor Tint=FLinearColor::White,int32 Lines=1):Text(MoveTemp(In)),Color(Tint),MaxLines(Lines){}
};
struct FFrontierHUDAudit
{
    uint64 Frame=0;FVector2D Viewport=FVector2D::ZeroVector;
    bool bInterface=false,bPaused=false,bFits=true,bNoOverlap=true,bCenterClear=true,bAlert=false;
    EFrontierHUDContext Context=EFrontierHUDContext::None;
    int32 TextLines=0,Elided=0,RosterRows=0;
    TArray<FBox2D> Panels;
};
