#include "CompanionSkillTrail.h"
#include "CreatureDefinition.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ACompanionSkillTrail::ACompanionSkillTrail()
{
    PrimaryActorTick.bCanEverTick=true;
    SetActorEnableCollision(false);
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> S(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> C(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> N(TEXT("/Engine/BasicShapes/Cone.Cone"));
    Sphere=S.Object;Cube=C.Object;Cone=N.Object;
    for(int32 Index=0;Index<7;++Index)
    {
        auto* Mark=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Mark%d"),Index));
        Mark->SetupAttachment(RootComponent);Mark->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mark->SetCanEverAffectNavigation(false);Mark->SetCastShadow(false);Marks.Add(Mark);
    }
    InitialLifeSpan=.5f;
}
void ACompanionSkillTrail::Initialize(const FVector& From,const FVector& To,const UCreatureDefinition* Definition)
{
    Origin=From;Destination=To;Age=0;
    const ECreatureForm Form=Definition?Definition->Form:ECreatureForm::Leaf;
    for(const auto& Mark:Marks)
    {
        Mark->SetStaticMesh(Form==ECreatureForm::Stone?Cube:Form==ECreatureForm::Leaf?Cone:Sphere);
        if(Definition && Definition->PaletteMaterial)
        {
            auto* Material=UMaterialInstanceDynamic::Create(Definition->PaletteMaterial,this);
            Material->SetVectorParameterValue(TEXT("Tint"),Definition->AccentColor);Mark->SetMaterial(0,Material);
        }
    }
    Tick(0);
}
void ACompanionSkillTrail::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);Age+=DeltaSeconds;
    const FVector Direction=Destination-Origin;
    for(int32 Index=0;Index<Marks.Num();++Index)
    {
        // 整条轨迹从施法者延向目标，脉动沿轨迹前进；不跟踪后续移动或穿透描边。
        const float Along=(Index+1.f)/(Marks.Num()+1.f);
        auto* Mark=Marks[Index].Get();
        Mark->SetWorldLocation(FMath::Lerp(Origin,Destination,Along));
        Mark->SetWorldRotation(Direction.Rotation());
        const float Pulse=.07f+.07f*FMath::Max(0.f,1.f-FMath::Abs(Along-Age/.5f)*3.f);
        Mark->SetWorldScale3D(FVector(Pulse*1.8f,Pulse,Pulse)*FMath::Clamp((.5f-Age)/.15f,0.f,1.f));
    }
}
