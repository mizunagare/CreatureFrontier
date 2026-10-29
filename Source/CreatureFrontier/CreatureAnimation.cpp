#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "WorkStation.h"
#include "CompanionConstructionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"

void ACreatureCharacter::BeginStrikePose(const AActor* Target)
{
    StrikePoseRemaining=.28f;
    StrikeDirectionLocal=Target?GetActorRotation().UnrotateVector(Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D():FVector::ForwardVector;
    if(StrikeDirectionLocal.IsNearlyZero()) StrikeDirectionLocal=FVector::ForwardVector;
}

void ACreatureCharacter::UpdateCreaturePresentation(float DeltaSeconds)
{
    if(!VisualPivot || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds<0) return;
    const float Step=FMath::Min(DeltaSeconds,.1f);
    PoseTime=FMath::Fmod(PoseTime+Step,600.f);
    const float Time=PoseTime+static_cast<float>(GetUniqueID()%97)*.17f;
    const float Speed=GetVelocity().Size2D();
    const bool Dead=Health<=0;
    const bool Moving=!Dead && Speed>10;
    const bool Resting=!Dead && !Moving && Activity==ECreatureActivity::Rest;
    const bool Working=!Dead && !Moving && Activity==ECreatureActivity::Work && (Construction->IsWorking() || (IsValid(WorkSite) &&
        WorkSite->RequiredSpecialty!=EWorkSpecialty::Transport && WorkSite->RequiredSpecialty!=EWorkSpecialty::Feeding &&
        !FeedingSite.IsValid() && ((WorkSite->RequiredSpecialty==EWorkSpecialty::Farming && FVector::Dist2D(GetActorLocation(),WorkSite->GetActorLocation())<=220) || bGatheringActive)));
    const bool Charging=!Dead && (SpecialWindupRemaining>0 || AlphaWindup>0);
    VisualState=Dead?TEXT("Downed"):HitPoseRemaining>0?TEXT("Hurt"):Charging?TEXT("Charging"):StrikePoseRemaining>0?TEXT("Striking"):
        AffectionPoseRemaining>0?(bPlayfulResponse?TEXT("Playing"):TEXT("Affection")):Resting?TEXT("Resting"):Moving?TEXT("Moving"):Working?TEXT("Working"):TEXT("Idle");
    RestPoseBlend=FMath::FInterpTo(RestPoseBlend,VisualState==TEXT("Resting")?1.f:0.f,Step,8.f);
    const bool Stone=Definition && Definition->Form==ECreatureForm::Stone;
    const float Gait=Time*(Stone?7.f:11.f);
    const float Motion=Moving?FMath::Clamp(Speed/180.f,.2f,1.f):0;
    const float Breath=FMath::Sin(Time*2.2f);
    FVector Location(0,0,(Moving?FMath::Abs(FMath::Sin(Gait))*2.5f:Breath*.7f)-RestPoseBlend*22);
    FRotator Rotation(0,0,Moving?FMath::Sin(Gait)*2*Motion:0);
    FVector Scale(1+RestPoseBlend*.05f,1,1-RestPoseBlend*.28f+Breath*.006f);
    if(VisualState==TEXT("Working")) { Rotation.Pitch=FMath::Sin(Time*6)*8; Location.Z+=FMath::Abs(FMath::Sin(Time*6))*2; }
    if(VisualState==TEXT("Affection")) { Rotation.Roll=FMath::Sin(Time*5)*(bFavoriteResponse?13:7); Rotation.Pitch=-6; Location.Z+=FMath::Abs(FMath::Sin(Time*5))*2; }
    if(VisualState==TEXT("Playing")) { const float Joy=bFavoriteResponse?1.5f:1.f;Location.Z+=FMath::Abs(FMath::Sin(Time*10))*10*Joy;Rotation.Yaw=FMath::Sin(Time*5)*12*Joy;Scale.Z+=FMath::Sin(Time*10)*.06f; }
    if(VisualState==TEXT("Charging")) { Rotation.Pitch=-12; Location.Z-=8; Scale.Z-=.08f; }
    if(VisualState==TEXT("Striking"))
    {
        const float Pulse=FMath::Sin(PI*(1-FMath::Clamp(StrikePoseRemaining/.28f,0.f,1.f)));
        Location+=StrikeDirectionLocal*(Pulse*18); Rotation.Yaw=StrikeDirectionLocal.Rotation().Yaw; Rotation.Pitch-=Pulse*15;
    }
    if(VisualState==TEXT("Hurt")) { Location.X-=FMath::Sin(PI*FMath::Clamp(HitPoseRemaining/.2f,0.f,1.f))*9; Rotation.Roll+=8; }
    if(Dead) { Location=FVector::ZeroVector; Rotation=FRotator(0,0,65); Scale=FVector::OneVector; StrikePoseRemaining=0; HitPoseRemaining=0; AffectionPoseRemaining=0; }
    VisualPivot->SetRelativeLocationAndRotation(Location,Rotation); VisualPivot->SetRelativeScale3D(Scale);
    for(int32 Index=0;Index<Feet.Num() && Index<NeutralFeet.Num();++Index)
    {
        FTransform Pose=NeutralFeet[Index];
        if(Moving && !Charging)
        {
            const float Wave=FMath::Sin(Gait+((Index==0 || Index==3)?0:PI));
            Pose.AddToTranslation(FVector(Wave*9*Motion,0,FMath::Max(0.f,Wave)*9*Motion));
            Pose.SetRotation((FRotator(Wave*18*Motion,0,0).Quaternion()*Pose.GetRotation()).GetNormalized());
        }
        Feet[Index]->SetRelativeTransform(Pose);
    }
    for(int32 Index=0;Index<AccentParts.Num() && Index<NeutralAccents.Num();++Index)
    {
        FTransform Pose=NeutralAccents[Index];
        if(!Dead && !Stone)
            Pose.SetRotation((FRotator(0,FMath::Sin(Time*3+Index)*3,0).Quaternion()*Pose.GetRotation()).GetNormalized());
        AccentParts[Index]->SetRelativeTransform(Pose);
    }
    // 眼白与瞳孔同步压缩，避免眨眼时瞳孔脱离眼睛。
    const float BlinkPhase=FMath::Fmod(Time,4.3f);
    const float EyeOpen=Dead?1.f:(RestPoseBlend>.7f || AffectionPoseRemaining>0)?.12f:BlinkPhase<.14f?.08f:1.f;
    for(int32 Index=0;Index<EyeParts.Num() && Index<NeutralEyes.Num();++Index)
    {
        FTransform Eye=NeutralEyes[Index],Pupil=NeutralPupils[Index];
        Eye.SetScale3D(Eye.GetScale3D()*FVector(1,1,EyeOpen)); Pupil.SetScale3D(Pupil.GetScale3D()*FVector(1,1,EyeOpen));
        EyeParts[Index]->SetRelativeTransform(Eye); Pupils[Index]->SetRelativeTransform(Pupil);
    }
    StrikePoseRemaining=FMath::Max(0.f,StrikePoseRemaining-Step);
    HitPoseRemaining=FMath::Max(0.f,HitPoseRemaining-Step);
    AffectionPoseRemaining=FMath::Max(0.f,AffectionPoseRemaining-Step);
}
