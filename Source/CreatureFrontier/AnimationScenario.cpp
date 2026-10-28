#include "AnimationScenario.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CFPlayerCharacter.h"
#include "InventoryComponent.h"
#include "WorkStation.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "Engine/GameViewportClient.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "TimerManager.h"

void RunAnimationScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    TMap<ECreatureForm,UCreatureDefinition*> Definitions;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) { if(It->Definition) Definitions.Add(It->Definition->Form,It->Definition); It->Destroy(); }
    if(Definitions.Num()!=3) { UE_LOG(LogTemp,Error,TEXT("CF_ANIMATION FAIL species fixtures")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->SetActorLocation(FVector(-1050,-1050,180)); Hero->GetCharacterMovement()->DisableMovement();
    const FRotator View=(FVector(-350,-1050,0)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
    Hero->GetController()->SetControlRotation(View); Hero->FirstPersonCamera->SetWorldRotation(View);
    TArray<ACreatureCharacter*> Pets;
    bool States=true,Isolation=true,Reset=true;
    for(int32 Index=0;Index<3;++Index)
    {
        auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),FTransform(FRotator(0,180,0),FVector(-350,-1050+(Index-1)*300,40)));
        Pet->Definition=Definitions.FindRef(static_cast<ECreatureForm>(Index)); UGameplayStatics::FinishSpawningActor(Pet,Pet->GetActorTransform()); Pet->Tame(Hero->GetController());
        Pet->SetActorTickEnabled(false); Pet->GetCharacterMovement()->DisableMovement(); Pets.Add(Pet);
        const FTransform Root=Pet->GetActorTransform(); const float Radius=Pet->GetCapsuleComponent()->GetUnscaledCapsuleRadius();
        auto* Pivot=Pet->CreatureVisual->GetAttachParent();
        auto Find=[Pet](const TCHAR* Name) { TArray<UStaticMeshComponent*> Pieces; Pet->GetComponents(Pieces); for(auto* Piece:Pieces) if(Piece->GetFName()==FName(Name)) return Piece; return static_cast<UStaticMeshComponent*>(nullptr); };
        auto* Foot=Find(TEXT("FootFL")); auto* Eye=Find(TEXT("EyeLeft")); auto* Warning=Find(TEXT("AlphaWarning"));
        if(!Foot || !Eye || !Warning) { States=false; continue; }
        Pet->UpdateCreaturePresentation(.05f); const FTransform NeutralFoot=Foot->GetRelativeTransform();
        States &= Pet->GetVisualState()==TEXT("Idle");
        Pet->GetCharacterMovement()->Velocity=FVector(180,0,0); Pet->UpdateCreaturePresentation(.05f);
        States &= Pet->GetVisualState()==TEXT("Moving") && !Foot->GetRelativeTransform().Equals(NeutralFoot,.001f);
        Pet->GetCharacterMovement()->Velocity=FVector::ZeroVector; Pet->SetActivity(ECreatureActivity::Rest);
        for(int32 Step=0;Step<20;++Step) Pet->UpdateCreaturePresentation(.05f);
        States &= Pet->GetVisualState()==TEXT("Resting") && Pivot->GetRelativeScale3D().Z<.8 && Eye->GetRelativeScale3D().Z<.05;
        auto* Site=World->SpawnActor<AWorkStation>(Pet->GetActorLocation(),FRotator::ZeroRotator); Site->RequiredSpecialty=EWorkSpecialty::Farming; Pet->WorkSite=Site; Pet->SetActivity(ECreatureActivity::Work); Pet->UpdateCreaturePresentation(.05f);
        States &= Pet->GetVisualState()==TEXT("Working");
        Pet->SpecialWindupRemaining=.6f; Pet->UpdateCreaturePresentation(.05f);
        States &= Pet->GetVisualState()==TEXT("Charging") && Pivot->GetRelativeRotation().Pitch<-5;
        Isolation &= Warning->GetAttachParent()==Pet->GetRootComponent() && Warning->GetUpVector().Equals(FVector::UpVector,.001f);
        Pet->TakeDamage(1,FDamageEvent(),nullptr,nullptr); Pet->UpdateCreaturePresentation(.05f); States &= Pet->GetVisualState()==TEXT("Hurt");
        Pet->Health=0; Pet->UpdateCreaturePresentation(.05f); States &= Pet->GetVisualState()==TEXT("Downed") && FMath::IsNearlyEqual(Pivot->GetRelativeRotation().Roll,65.f,.01f);
        Pet->Health=Pet->GetMaxHealth(); Pet->SpecialWindupRemaining=0; Pet->WorkSite=nullptr; Site->Destroy(); Pet->SetActivity(ECreatureActivity::Idle);
        for(int32 Step=0;Step<1000;++Step) Pet->UpdateCreaturePresentation(.016f);
        Reset &= Pet->GetVisualState()==TEXT("Idle") && Foot->GetRelativeTransform().Equals(NeutralFoot,.001f) && Pivot->GetRelativeRotation().IsNearlyZero(.01f) && FMath::Abs(Pivot->GetRelativeScale3D().Z-1)<.01;
        Isolation &= Pet->GetActorTransform().Equals(Root,.001f) && Pet->GetCapsuleComponent()->GetUnscaledCapsuleRadius()==Radius;
        TArray<UStaticMeshComponent*> Pieces; Pet->GetComponents(Pieces);
        for(auto* Piece:Pieces) Isolation &= Piece->GetCollisionEnabled()==ECollisionEnabled::NoCollision && !Piece->CanEverAffectNavigation();
    }
    // 通过真实的 AI 近战决策触发动作，不直接伪造动画触发。
    auto* Attacker=Pets[0]; auto* Target=World->SpawnActor<ACreatureCharacter>(Attacker->GetActorLocation()+FVector(120,0,0),FRotator::ZeroRotator);
    Target->SetActorTickEnabled(false); Target->GetCharacterMovement()->DisableMovement(); Attacker->FocusTarget=Target; Attacker->SetActivity(ECreatureActivity::Combat);
    const float Before=Target->Health; Attacker->Tick(.016f);
    // 首次表现采样为攻击的中性起始姿态，随后推进到突进阶段。
    Attacker->UpdateCreaturePresentation(.07f); Attacker->UpdateCreaturePresentation(.07f);
    const bool Strike=Target->Health<Before && Attacker->GetVisualState()==TEXT("Striking") && Attacker->CreatureVisual->GetAttachParent()->GetComponentLocation().X>Attacker->GetActorLocation().X;
    UE_LOG(LogTemp,Display,TEXT("CF_ANIMATION melee hp=%.2f before=%.2f state=%s activity=%d root=%s pivot=%s target=%s"),Target->Health,Before,*Attacker->GetVisualState().ToString(),static_cast<int32>(Attacker->Activity),*Attacker->GetActorLocation().ToString(),*Attacker->CreatureVisual->GetAttachParent()->GetComponentLocation().ToString(),*Target->GetActorLocation().ToString());
    Target->Destroy();
    for(auto* Pet:Pets) { Pet->ClearCombatOrders(); Pet->SetActivity(ECreatureActivity::Follow); }
    bool Persistence=Hero->SaveProgress(TEXT("FrontierAnimationAutomation")) && Hero->LoadProgress(TEXT("FrontierAnimationAutomation"));
    Pets.Reset();
    for(TActorIterator<ACreatureCharacter> It(World);It;++It) if(It->Tamer==Hero->GetController())
    {
        It->SetActorTickEnabled(false); It->GetCharacterMovement()->DisableMovement(); Pets.Add(*It);
        Persistence &= It->GetVisualState()==TEXT("Idle") && It->CreatureVisual->GetAttachParent()->GetRelativeScale3D().Equals(FVector::OneVector,.001f);
    }
    Persistence &= Pets.Num()==3;
    const bool Passed=States && Isolation && Reset && Strike && Persistence;
    UE_LOG(LogTemp,Display,TEXT("CF_ANIMATION %s states=%d isolation=%d reset=%d strike=%d persistence=%d"),Passed?TEXT("PASS"):TEXT("FAIL"),States,Isolation,Reset,Strike,Persistence);
    if(Passed && FParse::Param(FCommandLine::Get(),TEXT("CFAnimationPreview")))
    {
        Hero->GetCharacterMovement()->DisableMovement();
        for(auto* Pet:Pets)
        {
            Pet->CarriedFood=Pet->Definition->Form==ECreatureForm::Leaf?2:0;
            if(Pet->Definition->Form==ECreatureForm::Ember) Pet->SetActivity(ECreatureActivity::Rest);
            if(Pet->Definition->Form==ECreatureForm::Stone) Pet->SpecialWindupRemaining=.6f;
            for(int32 Step=0;Step<20;++Step) Pet->UpdateCreaturePresentation(.05f);
            Pet->Tick(0.f); // 刷新携带物与技能效果，不推进模拟时间。
        }
        const FString Root=FPaths::ProjectSavedDir()/TEXT("AnimationPreview"); IFileManager::Get().MakeDirectory(*Root,true);
        FTimerHandle Shot,Done; World->GetTimerManager().SetTimer(Shot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("poses.png"),false,false);},3.f,false);
        World->GetTimerManager().SetTimer(Done,[](){FPlatformMisc::RequestExitWithStatus(false,0);},5.f,false); return;
    }
    FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
}
