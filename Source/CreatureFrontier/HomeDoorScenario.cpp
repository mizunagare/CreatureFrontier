#include "HomeDoorScenario.h"
#include "HomeModule.h"
#include "HomeBuildingComponent.h"
#include "HomeLayout.h"
#include "CFPlayerCharacter.h"
#include "FrontierSaveGame.h"
#include "InventoryComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

namespace CFHomeDoorQA
{
AHomeModule* FindDoor(UWorld* World)
{for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && !It->bStaged && It->GetKind()==EHomeModuleKind::Door)return *It;return nullptr;}
void Aim(ACFPlayerCharacter* Hero,const FVector& Point)
{const auto Rotation=(Point-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(Rotation);Hero->FirstPersonCamera->SetWorldRotation(Rotation);}
}
void RunHomeDoorScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode)
{
    using namespace CFHomeDoorQA;
    if(Mode>0)
    {
        const TCHAR* Slot=Mode==1?TEXT("DoorOpen"):TEXT("DoorGood");
        const bool Loaded=Hero->LoadProgress(Slot);auto* Door=FindDoor(World);
        auto* Saved=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
        const auto* Record=Saved?Saved->HomeModules.FindByPredicate([](const auto& R){return R.Definition.Get() && R.Definition.Get()->Kind==EHomeModuleKind::Door;}):nullptr;
        FHitResult Hit;const bool Blocked=World->LineTraceSingleByChannel(Hit,FVector(-400,-1100,70),FVector(-400,-900,70),ECC_Visibility) && Hit.GetActor()==Door;
        const bool Pass=Loaded && Door && Record && Saved->FormatVersion==FrontierCurrentSaveVersion && Door->PersistentId==Record->Id && Door->DoorState==(Mode==1?2:1) &&
            Blocked==(Mode==2) && Hero->Inventory->Count(EFrontierItem::Material)==80 && HomeLayout::Collect(World).Num()==2;
        UE_LOG(LogTemp,Display,TEXT("CF_DOOR_%s_READ %s"),Mode==1?TEXT("OPEN"):TEXT("CLOSED"),Pass?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
    }
    Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorLocation(FVector(-400,-1250,60));
    Hero->Inventory->Restore({{EFrontierItem::Material,100}});
    Hero->HomeBuilding->Selected=EHomeModuleKind::Floor;
    auto* Floor=Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-800,-40)));
    if(!Floor){UE_LOG(LogTemp,Error,TEXT("CF_DOOR FAIL floor %s"),*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,1);return;}
    // 旧格式只保留原建筑；未来字段不得把地板变成门。
    bool Legacy=Hero->SaveProgress(TEXT("DoorLegacy29"));
    auto* Old=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("DoorLegacy29"),0));
    if(Old){Old->FormatVersion=29;Old->HomeModules[0].DoorState=255;Legacy &= UGameplayStatics::SaveGameToSlot(Old,TEXT("DoorLegacy29"),0) && Hero->LoadProgress(TEXT("DoorLegacy29")) && !FindDoor(World);}
    else Legacy=false;
    Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorLocation(FVector(-400,-1250,60));
    FGuid FloorId;for(const auto& Record:HomeLayout::Collect(World))if(Record.Definition.Get()->Kind==EHomeModuleKind::Floor)FloorId=Record.Id;
    Hero->HomeBuilding->Selected=EHomeModuleKind::Door;
    auto* Door=Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-1000,-30)),FloorId);
    if(!Door){UE_LOG(LogTemp,Error,TEXT("CF_DOOR FAIL build %s"),*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const auto Id=Door->PersistentId;
    bool Build=Door->DoorState==1 && Hero->Inventory->Count(EFrontierItem::Material)==80;
    Hero->SetActorLocation(FVector(-400,-1200,60));CFHomeDoorQA::Aim(Hero,FVector(-400,-1000,95));
    auto Blocked=[&](){FHitResult Hit;return World->LineTraceSingleByChannel(Hit,FVector(-400,-1100,70),FVector(-400,-900,70),ECC_Visibility) && Hit.GetActor()==Door;};
    bool Solid=Blocked();
    bool Gates=true;
    UGameplayStatics::SetGamePaused(World,true);Gates &= !Door->ToggleDoor(Hero) && Door->DoorState==1;UGameplayStatics::SetGamePaused(World,false);
    Hero->Health=0;Gates &= !Door->ToggleDoor(Hero);Hero->Health=100;
    Hero->SetActorLocation(FVector(-400,-1500,60));CFHomeDoorQA::Aim(Hero,FVector(-400,-1000,95));Gates &= !Door->ToggleDoor(Hero);
    Hero->SetActorLocation(FVector(-400,-1200,60));CFHomeDoorQA::Aim(Hero,FVector(-400,-1000,95));
    Door->bPreview=true;Gates &= !Door->ToggleDoor(Hero);Door->bPreview=false;
    Door->bStaged=true;Gates &= !Door->ToggleDoor(Hero);Door->bStaged=false;
    Hero->CommandWork();bool Open=Door->DoorState==2 && !Blocked();
    // 对开启后的门框瞄准，不能把透过门洞的射线误当作命中门。
    CFHomeDoorQA::Aim(Hero,FVector(-555,-1000,120));
    auto* Obstacle=World->SpawnActor<AStaticMeshActor>(FVector(-400,-900,70),FRotator::ZeroRotator);
    Obstacle->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    Obstacle->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Obstacle->SetActorScale3D(FVector(.5));Obstacle->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    bool Occupied=!Door->ToggleDoor(Hero) && Door->DoorState==2;
    Obstacle->Destroy();
    Hero->CommandWork();bool Closed=Door->DoorState==1 && Blocked() && Hero->Inventory->Count(EFrontierItem::Material)==80;
    Hero->HomeBuilding->Selected=EHomeModuleKind::Storage;FString Reason;
    bool Reserved=!Hero->HomeBuilding->ValidatePlacement(FTransform(FVector(-400,-900,-30)),FloorId,Reason) && Reason.Contains(TEXT("活动空间"));
    bool Save=Hero->SaveProgress(TEXT("DoorGood")) && Hero->LoadProgress(TEXT("DoorGood"));Door=FindDoor(World);
    Save &= Door && Door->DoorState==1 && Door->PersistentId==Id;
    bool Invalid=true;
    for(int32 Case=0;Case<6 && Door;++Case)
    {
        auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("DoorGood"),0));
        auto* Record=Bad?Bad->HomeModules.FindByPredicate([Id](const auto& R){return R.Id==Id;}):nullptr;
        if(!Record){Invalid=false;break;}
        if(Case<3)Record->DoorState=Case==0?0:Case==1?3:255;
        if(Case==3){auto* FloorRecord=Bad->HomeModules.FindByPredicate([](const auto& R){return R.Definition.Get()->Kind==EHomeModuleKind::Floor;});if(!FloorRecord){Invalid=false;break;}FloorRecord->DoorState=1;}
        if(Case==4)Bad->PlayerTransform.SetLocation(FVector(-400,-1000,70));
        if(Case==5)Bad->FormatVersion=29;
        Invalid &= UGameplayStatics::SaveGameToSlot(Bad,TEXT("DoorBad"),0) && !Hero->LoadProgress(TEXT("DoorBad")) && IsValid(Door) && Door->PersistentId==Id && Door->DoorState==1;
    }
    Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorLocation(FVector(-400,-1200,60));CFHomeDoorQA::Aim(Hero,FVector(-400,-1000,95));
    bool OpenSave=Door && Door->ToggleDoor(Hero) && Hero->SaveProgress(TEXT("DoorOpen")) && Hero->LoadProgress(TEXT("DoorOpen"));Door=FindDoor(World);
    OpenSave &= Door && Door->DoorState==2 && Door->PersistentId==Id;
    const bool Pass=Legacy && Build && Solid && Gates && Open && Occupied && Closed && Reserved && Save && Invalid && OpenSave;
    UE_LOG(LogTemp,Display,TEXT("CF_DOOR %s legacy=%d build=%d solid=%d gates=%d open=%d occupied=%d close=%d reserve=%d save=%d invalid=%d openSave=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Legacy,Build,Solid,Gates,Open,Occupied,Closed,Reserved,Save,Invalid,OpenSave);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
