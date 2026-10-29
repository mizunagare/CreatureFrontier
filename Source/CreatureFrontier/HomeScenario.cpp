#include "HomeScenario.h"
#include "HomeBuildingComponent.h"
#include "HomeModule.h"
#include "HomeLayout.h"
#include "BaseBuildingComponent.h"
#include "CFPlayerCharacter.h"
#include "CFHUD.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "WorkStation.h"
#include "ResourceNode.h"
#include "InventoryComponent.h"
#include "FrontierSaveGame.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "TimerManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Components/BoxComponent.h"
#include "InputRoutingQA.h"
#include "HomeDoorScenario.h"
#include "InputKeyEventArgs.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "PlayerEquipmentComponent.h"

UHomeMovementProbe::UHomeMovementProbe() { PrimaryComponentTick.bCanEverTick=true; }

void RunHomeWindowMeleeScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    Hero->SetActorLocation(FVector(-400,-1140,40));Hero->GetCharacterMovement()->DisableMovement();Hero->Inventory->Restore({{EFrontierItem::Material,100}});
    Hero->HomeBuilding->Selected=EHomeModuleKind::Floor;
    auto* Floor=Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-800,-40)));
    Hero->HomeBuilding->Selected=EHomeModuleKind::Window;
    auto* Window=Floor?Hero->HomeBuilding->BuildAt(FTransform(FVector(-400,-1000,-30)),Floor->PersistentId):nullptr;
    if(!Window){UE_LOG(LogTemp,Error,TEXT("CF_WINDOW_MELEE FAIL fixture"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    FPlayerEquipmentState Gear;Gear.bOwnSpear=true;Gear.bSpearEquipped=true;Hero->Equipment->Restore(Gear);
    auto* Target=World->SpawnActor<ACreatureCharacter>(FVector(-400,-880,60),FRotator::ZeroRotator);
    if(!Target){UE_LOG(LogTemp,Error,TEXT("CF_WINDOW_MELEE FAIL target"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    // 固定靶位隔离窗台/窗洞/上沿/侧柱；高处靶位不是自然战斗或飞行生物证据。
    Target->SetActorTickEnabled(false);Target->GetCharacterMovement()->DisableMovement();
    auto Strike=[Hero,Target](FVector Player,FVector Victim)
    {
        Hero->SetActorLocation(Player);Target->SetActorLocation(Victim);
        const auto View=(Victim-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);Hero->Attack();
    };
    const auto Timer=MakeShared<FTimerHandle>();const float Initial=Target->Health;const float Damage=Hero->GetPlayerAttackDamage();
    Strike(FVector(-400,-1140,40),FVector(-400,-880,60));
    World->GetTimerManager().SetTimer(*Timer,[World,Hero,Target,Strike,Timer,Initial,Damage,Step=0,Pass=true]() mutable
    {
        const float Expected=(Step==0?Initial:Step<4?Initial-Damage:Initial-2*Damage);
        Pass &= FMath::IsNearlyEqual(Target->Health,Expected,.01) && !Hero->IsMeleePending();
        if(Step==0 || Step==2 || Step==3)Pass &= Hero->Feedback.Contains(TEXT("障碍物阻挡"));
        if(Step==0)Strike(FVector(-400,-1140,40),FVector(-400,-880,170));
        else if(Step==1)Strike(FVector(-400,-1140,40),FVector(-400,-880,300));
        else if(Step==2)Strike(FVector(-550,-1140,40),FVector(-550,-880,170));
        else if(Step==3)Strike(FVector(-400,-1140,40),FVector(-400,-880,170));
        else
        {
            Pass &= Hero->Inventory->Count(EFrontierItem::Material)==82;
            const bool Result=Pass;const float Health=Target->Health;FTimerHandle Handle=*Timer;World->GetTimerManager().ClearTimer(Handle);
            UE_LOG(LogTemp,Display,TEXT("CF_WINDOW_MELEE %s sill=1 opening=2 upper=1 side=1 health=%.1f"),Result?TEXT("PASS"):TEXT("FAIL"),Health);
            FPlatformMisc::RequestExitWithStatus(false,Result?0:1);return;
        }
        ++Step;
    },.65f,true);
}

void RunHomeWindowInputScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    // 地板是唯一直接创建的出发夹具，窗墙必须经过正常选择/旋转/Attack输入。
    Hero->SetActorLocation(FVector(-450,-400,40));Hero->Inventory->Restore({{EFrontierItem::Material,100}});
    Hero->HomeBuilding->Selected=EHomeModuleKind::Floor;
    auto* Floor=Hero->HomeBuilding->BuildAt(FTransform(FRotator::ZeroRotator,FVector(-400,-800,-40)));
    auto Aim=[Hero]() {const auto View=(FVector(-400,-950,-30)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);};
    Aim();
    bool Setup=Floor && CFInputQA::UniqueBindings(Hero) && CFInputQA::Press(Hero,EKeys::B) && CFInputQA::Press(Hero,EKeys::Eight);
    for(int32 I=0;Setup && I<7;++I)Setup &= CFInputQA::Press(Hero,EKeys::V);
    Setup &= Hero->HomeBuilding->Selected==EHomeModuleKind::Window && Hero->HomeBuilding->GetPreview()!=nullptr;
    if(!Setup){UE_LOG(LogTemp,Error,TEXT("CF_WINDOW_INPUT FAIL setup"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    auto Tap=[World,Hero]()
    {
        auto* PC=CastChecked<APlayerController>(Hero->GetController());
        PC->InputKey(FInputKeyEventArgs(nullptr,IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),EKeys::LeftMouseButton,IE_Pressed,FPlatformTime::Cycles64()));
        FTimerHandle Release;TWeakObjectPtr<APlayerController> Weak=PC;
        World->GetTimerManager().SetTimer(Release,[Weak](){if(auto* C=Weak.Get())C->InputKey(FInputKeyEventArgs(nullptr,IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),EKeys::LeftMouseButton,IE_Released,FPlatformTime::Cycles64()));},.05f,false);
    };
    auto Step=MakeShared<int32>(0);auto Timer=MakeShared<FTimerHandle>();Tap();
    World->GetTimerManager().SetTimer(*Timer,[World,Hero,Aim,Tap,Step,Timer]()
    {
        const auto Records=HomeLayout::Collect(World);
        int32 Windows=0;bool South=false,East=false;
        for(const auto& R:Records)if(R.Definition.Get() && R.Definition.Get()->Kind==EHomeModuleKind::Window)
        {++Windows;South |= R.Transform.GetLocation().Equals(FVector(-400,-1000,-30),.01) && FMath::IsNearlyZero(R.Transform.Rotator().Yaw,.01);East |= R.Transform.GetLocation().Equals(FVector(-200,-800,-30),.01) && FMath::IsNearlyEqual(R.Transform.Rotator().Yaw,90.,.01);}
        bool Pass=true;bool Done=false;
        if(*Step==0){Pass=Windows==1 && South && Hero->Inventory->Count(EFrontierItem::Material)==82; if(Pass)Tap();}
        else if(*Step==1)
        {
            Pass=Windows==1 && Hero->Inventory->Count(EFrontierItem::Material)==82 && Hero->HomeBuilding->GetHint().Contains(TEXT("占用"));
            if(Pass){Pass=CFInputQA::Press(Hero,EKeys::Nine);Aim();Tap();}
        }
        else
        {
            Pass=Windows==2 && South && East && Hero->Inventory->Count(EFrontierItem::Material)==72 && CFInputQA::Press(Hero,EKeys::B) && !Hero->Building->IsActive() && !Hero->HomeBuilding->GetPreview();Done=true;
        }
        if(!Pass || Done)
        {
            const int32 Phase=*Step,Materials=Hero->Inventory->Count(EFrontierItem::Material);auto Handle=*Timer;
            World->GetTimerManager().ClearTimer(Handle);
            UE_LOG(LogTemp,Display,TEXT("CF_WINDOW_INPUT %s phase=%d windows=%d materials=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Phase,Windows,Materials);
            FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
        }
        ++*Step;
    },.3f,true);
}

void RunHomeWindowScenario(UWorld* World,ACFPlayerCharacter* Hero)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("CFHomeReplaceRead")))
    {
        const bool DoorMode=FParse::Param(FCommandLine::Get(),TEXT("CFHomeReplaceDoorRead"));
        const bool ClosedMode=DoorMode && FParse::Param(FCommandLine::Get(),TEXT("CFHomeReplaceDoorClosed"));
        const FString Slot=ClosedMode?TEXT("HomeReplaceDoorClosed"):DoorMode?TEXT("HomeReplaceDoorGood"):TEXT("HomeReplaceGood");
        const auto* Input=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
        bool Pass=Input && Input->FormatVersion==FrontierCurrentSaveVersion && Input->HomeModules.Num()==4;
        int32 Windows=0,Roofs=0,Doors=0;
        if(Input)for(const auto& Record:Input->HomeModules)if(auto* D=Record.Definition.LoadSynchronous())
        {if(D->Kind==EHomeModuleKind::Window){++Windows;Pass &= Record.PaidMaterials==D->MaterialCost && Record.FloorId.IsValid();}if(D->Kind==EHomeModuleKind::Roof)++Roofs;
            if(D->Kind==EHomeModuleKind::Door){++Doors;Pass &= Record.DoorState==(ClosedMode?1:2) && Record.PaidMaterials==D->MaterialCost && Record.FloorId.IsValid();}}
        Pass &= Roofs==1 && (DoorMode?(Windows==0 && Doors==1):(Windows==1 && Doors==0));
        for(int32 Attempt=0;Pass && Attempt<2;++Attempt)
        {
            Pass &= Hero->LoadProgress(Slot);
            const auto Live=HomeLayout::Collect(World);Pass &= Live.Num()==Input->HomeModules.Num() && Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Input->InventoryStacks);
            for(const auto& Expected:Input->HomeModules)
            {
                const auto* Actual=Live.FindByPredicate([&Expected](const FHomeModuleRecord& R){return R.Id==Expected.Id;});
                Pass &= Actual && Actual->Definition==Expected.Definition && Actual->FloorId==Expected.FloorId && Actual->Transform.Equals(Expected.Transform,.01) &&
                    Actual->PaidMaterials==Expected.PaidMaterials && Actual->PaidExtraItems.OrderIndependentCompareEqual(Expected.PaidExtraItems) && Actual->DoorState==Expected.DoorState;
            }
            if(DoorMode)for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && It->GetKind()==EHomeModuleKind::Door)
            {
                FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(HomeReplaceDoorRead),false,Hero);const auto T=It->GetActorTransform();
                const bool Blocked=World->LineTraceSingleByChannel(Hit,T.TransformPosition(FVector(0,-150,170)),T.TransformPosition(FVector(0,150,170)),ECC_Visibility,Params);
                Pass &= It->DoorState==(ClosedMode?1:2) && (ClosedMode?(Blocked && Hit.GetActor()==*It):!Blocked);
            }
        }
        if(DoorMode)UE_LOG(LogTemp,Display,TEXT("CF_HOME_REPLACE_DOOR_READ %s loads=2 doors=%d open=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Doors,ClosedMode?0:1);
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_REPLACE_READ %s loads=2 windows=%d roofs=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Windows,Roofs);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("CFHomeReplaceTest")))
    {
        const float Orientation=FParse::Param(FCommandLine::Get(),TEXT("CFHomeReplaceRotated"))?90.f:0.f;
        const auto Point=[Orientation](FVector Position){const FVector Pivot(-400,-800,0);return Pivot+FRotator(0,Orientation,0).RotateVector(Position-Pivot);};
        Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorLocation(Point(FVector(-400,-1250,60)));
        Hero->Inventory->Restore({{EFrontierItem::Material,200}});
        auto Build=[Hero,Orientation,Point](EHomeModuleKind Kind,FVector At,FGuid Floor=FGuid())
        {Hero->HomeBuilding->Selected=Kind;const bool Wall=Kind==EHomeModuleKind::Wall || Kind==EHomeModuleKind::Window || Kind==EHomeModuleKind::Door || Kind==EHomeModuleKind::Doorway;
            return Hero->HomeBuilding->BuildAt(FTransform(FRotator(0,Wall?Orientation:0,0),Point(At)),Floor);};
        auto* Floor=Build(EHomeModuleKind::Floor,FVector(-400,-800,-40));
        auto* Wall=Floor?Build(EHomeModuleKind::Wall,FVector(-400,-1000,-30),Floor->PersistentId):nullptr;
        auto* Opposite=Wall?Build(EHomeModuleKind::Wall,FVector(-400,-600,-30),Floor->PersistentId):nullptr;
        auto* Roof=Opposite?Build(EHomeModuleKind::Roof,FVector(-400,-800,280),Floor->PersistentId):nullptr;
        bool Pass=Roof && !Hero->HomeBuilding->Dismantle(Wall);
        if(Pass)
        {
            const FGuid Id=Wall->PersistentId;const auto Transform=Wall->GetActorTransform();
            const int32 Before=Hero->Inventory->Count(EFrontierItem::Material),Refund=Wall->PaidMaterials/2;
            Hero->HomeBuilding->Selected=EHomeModuleKind::Window;
            AHomeModule* Window=nullptr;
            if(FParse::Param(FCommandLine::Get(),TEXT("CFHomeReplaceInput")))
            {
                Pass &= CFInputQA::UniqueBindings(Hero) && CFInputQA::Press(Hero,EKeys::B) && CFInputQA::Press(Hero,EKeys::Eight);
                const auto Aim=(Wall->GetActorTransform().TransformPosition(FVector(145,0,170))-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
                Hero->SetActorRotation(FRotator(0,Aim.Yaw,0));
                Hero->GetController()->SetControlRotation(Aim);Hero->FirstPersonCamera->SetWorldRotation(Aim);
                Pass &= CFInputQA::Press(Hero,EKeys::Insert) && IsValid(Wall) && Hero->Inventory->Count(EFrontierItem::Material)==Before && Hero->Feedback.Contains(TEXT("再按Insert确认"));
                if(FParse::Param(FCommandLine::Get(),TEXT("CFHomeReplaceCancel")))
                {
                    const auto Timer=MakeShared<FTimerHandle>();
                    World->GetTimerManager().SetTimer(*Timer,[World,Hero,Wall,Roof,Id,Aim,Before,Refund,Pass,Timer,At=World->GetTimeSeconds(),Phase=0]() mutable
                    {
                        auto Finish=[&](bool Good){UE_LOG(LogTemp,Display,TEXT("CF_HOME_REPLACE_CANCEL %s phase=%d materials=%d feedback=%s"),Good?TEXT("PASS"):TEXT("FAIL"),Phase,Hero->Inventory->Count(EFrontierItem::Material),*Hero->Feedback);
                            FTimerHandle Handle=*Timer;World->GetTimerManager().ClearTimer(Handle);FPlatformMisc::RequestExitWithStatus(false,Good?0:1);};
                        auto Fresh=[&](){const bool Good=CFInputQA::Press(Hero,EKeys::Insert) && IsValid(Wall) && Hero->Inventory->Count(EFrontierItem::Material)==Before && Hero->Feedback.Contains(TEXT("再按Insert确认"));
                            UE_LOG(LogTemp,Display,TEXT("CF_HOME_REPLACE_CANCEL_FRESH phase=%d good=%d materials=%d feedback=%s"),Phase,Good,Hero->Inventory->Count(EFrontierItem::Material),*Hero->Feedback);return Good;};
                        if(!Pass){Finish(false);return;}
                        if(Phase==0)
                        {
                            if(World->GetTimeSeconds()-At<5.4)return;
                            Pass &= Fresh();
                            // 真实预览Tick应在下一阶段前看到视线离开，不直接调用清理内部状态。
                            Hero->SetActorRotation(FRotator::ZeroRotator);Hero->GetController()->SetControlRotation(FRotator(70,0,0));Hero->FirstPersonCamera->SetWorldRotation(FRotator(70,0,0));
                            Phase=1;At=World->GetTimeSeconds();return;
                        }
                        if(World->GetTimeSeconds()-At<.4)return;
                        if(Phase==1)
                        {
                            Hero->SetActorRotation(FRotator(0,Aim.Yaw,0));Hero->GetController()->SetControlRotation(Aim);Hero->FirstPersonCamera->SetWorldRotation(Aim);
                            Pass &= Fresh();
                            for(int32 I=0;I<8;++I)Pass &= CFInputQA::Press(Hero,EKeys::V);
                            Pass &= Fresh();
                            Pass &= CFInputQA::Press(Hero,EKeys::B) && !Hero->Building->IsActive();
                            Pass &= CFInputQA::Press(Hero,EKeys::B) && CFInputQA::Press(Hero,EKeys::Eight) && Fresh();
                            Phase=2;At=World->GetTimeSeconds();return;
                        }
                        if(Phase==2)
                        {
                            Pass &= CFInputQA::Press(Hero,EKeys::Insert);
                            AHomeModule* Replaced=nullptr;
                            for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && It->PersistentId==Id && It->GetKind()==EHomeModuleKind::Window)Replaced=*It;
                            Pass &= Replaced && IsValid(Roof) && Hero->Inventory->Count(EFrontierItem::Material)==Before-10+Refund;
                            Finish(Pass);return;
                        }
                    },.1f,true);
                    return;
                }
                if(FParse::Param(FCommandLine::Get(),TEXT("CFHomeReplacePreview")))
                {
                    const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("HomeReplacePreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
                    IFileManager::Get().MakeDirectory(*Root,true);
                    UE_LOG(LogTemp,Display,TEXT("CF_HOME_REPLACE_PREVIEW_ROOT %s"),*Root);
                    FTimerHandle Shot,Exit;
                    World->GetTimerManager().SetTimer(Shot,[Root](){FScreenshotRequest::RequestScreenshot(Root/TEXT("Confirm.png"),false,false);},.6f,false);
                    World->GetTimerManager().SetTimer(Exit,[Hero,Wall,Root,Before,Pass](){const bool Ready=Pass && IsValid(Wall) && Hero->Inventory->Count(EFrontierItem::Material)==Before && IFileManager::Get().FileSize(*(Root/TEXT("Confirm.png")))>1000;
                        UE_LOG(LogTemp,Display,TEXT("CF_HOME_REPLACE_PREVIEW %s"),Ready?TEXT("PASS"):TEXT("FAIL"));FPlatformMisc::RequestExitWithStatus(false,Ready?0:1);},1.5f,false);
                    return;
                }
                Pass &= CFInputQA::Press(Hero,EKeys::Nine) && CFInputQA::Press(Hero,EKeys::Insert) && IsValid(Wall) && Hero->Inventory->Count(EFrontierItem::Material)==Before;
                Pass &= CFInputQA::Press(Hero,EKeys::Insert);
                for(TActorIterator<AHomeModule> It(World);It;++It)if(!It->bPreview && It->PersistentId==Id && It->GetKind()==EHomeModuleKind::Window)Window=*It;
                Pass &= CFInputQA::Press(Hero,EKeys::B) && !Hero->Building->IsActive();
            }
            else Window=Hero->HomeBuilding->ReplaceWall(Wall);
            Pass &= Window && IsValid(Roof) && Window->PersistentId==Id && Window->FloorId==Floor->PersistentId && Window->GetActorTransform().Equals(Transform) &&
                Window->PaidMaterials==10 && Hero->Inventory->Count(EFrontierItem::Material)==Before-10+Refund;
            if(Window)
            {
                if(FParse::Param(FCommandLine::Get(),TEXT("CFHomeReplaceWrite")))
                {
                    Pass &= Hero->SaveProgress(TEXT("HomeReplaceGood"));
                    UE_LOG(LogTemp,Display,TEXT("CF_HOME_REPLACE_WRITE %s"),Pass?TEXT("PASS"):TEXT("FAIL"));
                }
                const int32 After=Hero->Inventory->Count(EFrontierItem::Material);
                Pass &= !Hero->HomeBuilding->ReplaceWall(Window) && Hero->Inventory->Count(EFrontierItem::Material)==After;
                Hero->HomeBuilding->Selected=EHomeModuleKind::Wall;Hero->Inventory->Restore({{EFrontierItem::Material,0}});
                Pass &= !Hero->HomeBuilding->ReplaceWall(Window) && IsValid(Window) && Window->GetKind()==EHomeModuleKind::Window && Hero->Inventory->Count(EFrontierItem::Material)==0;
                Hero->Inventory->Restore({{EFrontierItem::Material,After}});
                Hero->SetActorLocation(Point(FVector(-400,-1000,140)));
                Pass &= !Hero->HomeBuilding->ReplaceWall(Window) && IsValid(Window) && Hero->Inventory->Count(EFrontierItem::Material)==After;
                Hero->SetActorLocation(Point(FVector(-400,-1250,60)));
                auto* Restored=Hero->HomeBuilding->ReplaceWall(Window);
                const int32 Cost=UHomeModuleDefinition::LoadDefault(EHomeModuleKind::Wall)->MaterialCost;
                Pass &= Restored && Restored->PersistentId==Id && Restored->PaidMaterials==Cost && Hero->Inventory->Count(EFrontierItem::Material)==After-Cost+5 && IsValid(Roof);
                FString Reason;Pass &= HomeLayout::Validate(HomeLayout::Collect(World),Reason);
                if(Pass && FParse::Param(FCommandLine::Get(),TEXT("CFHomeReplaceDoor")))
                {
                    AHomeModule* Current=Restored;
                    int32 Transitions=0;
                    for(const auto Kind:{EHomeModuleKind::Doorway,EHomeModuleKind::Door,EHomeModuleKind::Window,EHomeModuleKind::Wall})
                    {
                        const auto* Next=UHomeModuleDefinition::LoadDefault(Kind);
                        const int32 Materials=Hero->Inventory->Count(EFrontierItem::Material),Paid=Current->PaidMaterials;
                        Hero->HomeBuilding->Selected=Kind;
                        if(Kind==EHomeModuleKind::Door)
                        {
                            Hero->SetActorLocation(Point(FVector(-400,-900,60)));
                            Pass &= !Hero->HomeBuilding->ReplaceWall(Current) && IsValid(Current) && Hero->Inventory->Count(EFrontierItem::Material)==Materials;
                            Hero->SetActorLocation(Point(FVector(-400,-1250,60)));
                        }
                        auto* Replaced=Hero->HomeBuilding->ReplaceWall(Current);
                        Pass &= Replaced && Next && Replaced->PersistentId==Id && Replaced->FloorId==Floor->PersistentId && Replaced->GetActorTransform().Equals(Transform) &&
                            Replaced->PaidMaterials==Next->MaterialCost && Replaced->PaidExtraItems.OrderIndependentCompareEqual(Next->ExtraCosts) &&
                            Hero->Inventory->Count(EFrontierItem::Material)==Materials-Next->MaterialCost+Paid/2 && IsValid(Roof);
                        if(!Pass)break;
                        Current=Replaced;++Transitions;
                        auto CenterHit=[World,Hero,Current]()
                        {FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(HomeReplaceDoorRay),false,Hero);
                            const auto T=Current->GetActorTransform();return World->LineTraceSingleByChannel(Hit,T.TransformPosition(FVector(0,-150,170)),T.TransformPosition(FVector(0,150,170)),ECC_Visibility,Params)?Hit.GetActor():nullptr;};
                        if(Kind==EHomeModuleKind::Door)
                        {
                            Pass &= Current->DoorState==1 && CenterHit()==Current;
                            const bool ClosedSave=FParse::Param(FCommandLine::Get(),TEXT("CFHomeReplaceDoorClosed"));
                            if(ClosedSave && FParse::Param(FCommandLine::Get(),TEXT("CFHomeReplaceDoorWrite")))
                            {
                                Pass &= Hero->SaveProgress(TEXT("HomeReplaceDoorClosed"));
                                UE_LOG(LogTemp,Display,TEXT("CF_HOME_REPLACE_DOOR_WRITE %s closed=1"),Pass?TEXT("PASS"):TEXT("FAIL"));
                            }
                            const auto View=(Current->GetActorTransform().TransformPosition(FVector(0,0,125))-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
                            Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
                            Pass &= Current->ToggleDoor(Hero) && Current->DoorState==2 && CenterHit()==nullptr;
                            if(!ClosedSave && FParse::Param(FCommandLine::Get(),TEXT("CFHomeReplaceDoorWrite")))
                            {
                                Pass &= Hero->SaveProgress(TEXT("HomeReplaceDoorGood"));
                                UE_LOG(LogTemp,Display,TEXT("CF_HOME_REPLACE_DOOR_WRITE %s"),Pass?TEXT("PASS"):TEXT("FAIL"));
                            }
                        }
                        else Pass &= Current->DoorState==0 && (Kind==EHomeModuleKind::Wall?CenterHit()==Current:CenterHit()==nullptr);
                        Pass &= HomeLayout::Validate(HomeLayout::Collect(World),Reason);
                    }
                    UE_LOG(LogTemp,Display,TEXT("CF_HOME_REPLACE_DOOR %s transitions=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Transitions);
                    if(Pass)Restored=Current;
                }
                if(Pass && FParse::Param(FCommandLine::Get(),TEXT("CFHomeReplaceCapacity")))
                {
                    Hero->HomeBuilding->Selected=EHomeModuleKind::Window;
                    const auto Bag=Hero->Inventory->GetStacks();
                    UGameplayStatics::SetGamePaused(World,true);
                    const bool Paused=!Hero->HomeBuilding->ReplaceWall(Restored);
                    UGameplayStatics::SetGamePaused(World,false);
                    Hero->Health=0;const bool Downed=!Hero->HomeBuilding->ReplaceWall(Restored);Hero->Health=100;
                    Hero->SetActorLocation(Point(FVector(-400,-1800,60)));const bool Far=!Hero->HomeBuilding->ReplaceWall(Restored);Hero->SetActorLocation(Point(FVector(-400,-1250,60)));
                    const bool Unchanged=IsValid(Restored) && Restored->PersistentId==Id && Restored->PaidMaterials==Cost && Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Bag);
                    Pass &= Paused && Downed && Far && Unchanged && Hero->Inventory->Restore({{EFrontierItem::Material,UInventoryComponent::StackLimit}});
                    // 注入合法范围内的历史支付记录，专门验证净返还溢出；不是当前造价或自然经济证据。
                    Restored->PaidMaterials=100;
                    const bool Overflow=!Hero->HomeBuilding->ReplaceWall(Restored) && IsValid(Restored) && Restored->PaidMaterials==100 &&
                        Restored->PersistentId==Id && Hero->Inventory->Count(EFrontierItem::Material)==UInventoryComponent::StackLimit && HomeLayout::Collect(World).Num()==4;
                    Restored->PaidMaterials=Cost;
                    auto* FullStackResult=Hero->HomeBuilding->ReplaceWall(Restored);
                    const bool Net=FullStackResult && FullStackResult->PersistentId==Id && FullStackResult->PaidMaterials==10 &&
                        Hero->Inventory->Count(EFrontierItem::Material)==UInventoryComponent::StackLimit-10+Cost/2 && IsValid(Roof);
                    Pass &= Overflow && Net && HomeLayout::Validate(HomeLayout::Collect(World),Reason);
                    UE_LOG(LogTemp,Display,TEXT("CF_HOME_REPLACE_CAPACITY %s paused=%d downed=%d far=%d overflow=%d net=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Paused,Downed,Far,Overflow,Net);
                }
            }
        }
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_REPLACE %s support=%d yaw=%.0f"),Pass?TEXT("PASS"):TEXT("FAIL"),IsValid(Roof),Orientation);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("CFWindowMeleeTest"))){RunHomeWindowMeleeScenario(World,Hero);return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("CFWindowInputTest"))){RunHomeWindowInputScenario(World,Hero);return;}
    if(FParse::Param(FCommandLine::Get(),TEXT("CFWindowRead")))
    {
        // 冷启动读取只使用磁盘期望值，不摆放夹具、不赠送材料、不另写存档。
        const auto* Input=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("WindowGood"),0));
        bool Pass=Input && Input->FormatVersion==FrontierCurrentSaveVersion && Input->HomeModules.Num()==5 && Input->InventoryStacks.FindRef(EFrontierItem::Material)==56;
        int32 Windows=0;
        if(Input)for(const auto& R:Input->HomeModules)if(auto* D=R.Definition.LoadSynchronous();D && D->Kind==EHomeModuleKind::Window)
        {++Windows;Pass &= R.PaidMaterials==10;}
        Pass &= Windows==2;
        for(int32 Attempt=0;Pass && Attempt<2;++Attempt)
        {
            Pass &= Hero->LoadProgress(TEXT("WindowGood"));
            const auto Actual=HomeLayout::Collect(World);
            Pass &= Actual.Num()==Input->HomeModules.Num() && Hero->Inventory->GetStacks().OrderIndependentCompareEqual(Input->InventoryStacks);
            for(const auto& Expected:Input->HomeModules)
            {
                const auto* Found=Actual.FindByPredicate([&](const auto& R){return R.Id==Expected.Id;});
                Pass &= Found && Found->Definition==Expected.Definition && Found->FloorId==Expected.FloorId &&
                    Found->Transform.Equals(Expected.Transform,.01) && Found->PaidMaterials==Expected.PaidMaterials &&
                    Found->PaidExtraItems.OrderIndependentCompareEqual(Expected.PaidExtraItems) && Found->DoorState==Expected.DoorState;
            }
            for(TActorIterator<AHomeModule> It(World);It;++It)if(It->GetKind()==EHomeModuleKind::Window && !It->bPreview)
            {
                FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(WindowColdGeometry),false,Hero);
                const auto T=It->GetActorTransform();
                Pass &= !World->LineTraceSingleByChannel(Hit,T.TransformPosition(FVector(0,-80,170)),T.TransformPosition(FVector(0,80,170)),ECC_Visibility,Params);
                Pass &= World->LineTraceSingleByChannel(Hit,T.TransformPosition(FVector(0,-80,60)),T.TransformPosition(FVector(0,80,60)),ECC_Visibility,Params) && Hit.GetActor()==*It;
            }
        }
        UE_LOG(LogTemp,Display,TEXT("CF_WINDOW_READ %s windows=%d loads=2"),Pass?TEXT("PASS"):TEXT("FAIL"),Windows);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
    }
    // 独立灰盒夹具；不借用其他建筑专项作为窗洞/支撑验收。
    Hero->SetActorLocation(FVector(-450,-400,40));
    Hero->Inventory->Restore({{EFrontierItem::Material,100}});
    auto Build=[Hero](EHomeModuleKind Kind,FVector Position,FGuid Floor=FGuid(),double Yaw=0.)
    {Hero->HomeBuilding->Selected=Kind;return Hero->HomeBuilding->BuildAt(FTransform(FRotator(0,Yaw,0),Position),Floor);};
    auto* Floor=Build(EHomeModuleKind::Floor,FVector(-400,-800,-40));
    auto* Window=Floor?Build(EHomeModuleKind::Window,FVector(-400,-1000,-30),Floor->PersistentId):nullptr;
    auto* Back=Floor?Build(EHomeModuleKind::Wall,FVector(-400,-600,-30),Floor->PersistentId):nullptr;
    auto* Roof=Back && Window?Build(EHomeModuleKind::Roof,FVector(-400,-800,280),Floor->PersistentId):nullptr;
    bool Built=Floor && Window && Back && Roof && Hero->Inventory->Count(EFrontierItem::Material)==66;
    auto* Side=Built?Build(EHomeModuleKind::Window,FVector(-600,-800,-30),Floor->PersistentId,90.):nullptr;
    Built &= Side && Hero->Inventory->Count(EFrontierItem::Material)==56;
    bool Hole=Built,Frame=Built,PawnBlocked=Built;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(WindowGeometry),false,Hero);
    for(auto* Module:{Window,Side})if(Module)
    {
        auto Ray=[&](double Height,FHitResult& Hit)
        {return World->LineTraceSingleByChannel(Hit,Module->GetActorTransform().TransformPosition(FVector(0,-80,Height)),Module->GetActorTransform().TransformPosition(FVector(0,80,Height)),ECC_Visibility,Params);};
        FHitResult Hit;
        Hole &= !Ray(170,Hit);
        Frame &= Ray(60,Hit) && Hit.GetActor()==Module && Ray(260,Hit) && Hit.GetActor()==Module;
        const auto* Capsule=Hero->GetCapsuleComponent();
        PawnBlocked &= World->SweepSingleByChannel(Hit,Module->GetActorTransform().TransformPosition(FVector(0,-100,100)),Module->GetActorTransform().TransformPosition(FVector(0,100,100)),FQuat::Identity,ECC_Pawn,
            FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Params) && Hit.GetActor()==Module;
    }
    const bool Support=Built && !Hero->HomeBuilding->Dismantle(Window) && IsValid(Window);
    const FGuid WindowId=Window?Window->PersistentId:FGuid(),SideId=Side?Side->PersistentId:FGuid();
    bool Saved=Built && Hero->SaveProgress(TEXT("WindowGood"));
    auto* Legacy=Saved?Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("WindowGood"),0)):nullptr;
    bool Rejected=false;
    if(Legacy){Legacy->FormatVersion=31;Rejected=UGameplayStatics::SaveGameToSlot(Legacy,TEXT("WindowLegacy31"),0) && !Hero->LoadProgress(TEXT("WindowLegacy31")) && IsValid(Window) && Hero->Inventory->Count(EFrontierItem::Material)==56;}
    Saved &= Hero->LoadProgress(TEXT("WindowGood")) && Hero->LoadProgress(TEXT("WindowGood"));
    const auto Restored=HomeLayout::Collect(World);
    Saved &= Restored.ContainsByPredicate([WindowId](const auto& R){return R.Id==WindowId && R.Definition.Get() && R.Definition.Get()->Kind==EHomeModuleKind::Window;}) &&
        Restored.ContainsByPredicate([SideId](const auto& R){return R.Id==SideId && R.Definition.Get() && R.Definition.Get()->Kind==EHomeModuleKind::Window;});
    const bool Pass=Built && Hole && Frame && PawnBlocked && Support && Rejected && Saved;
    UE_LOG(LogTemp,Display,TEXT("CF_WINDOW %s built=%d hole=%d frame=%d pawn=%d support=%d legacy=%d save=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Built,Hole,Frame,PawnBlocked,Support,Rejected,Saved);
    if(Pass && FParse::Param(FCommandLine::Get(),TEXT("CFWindowPreview")))
    {
        const FString Root=FPaths::ProjectSavedDir()/TEXT("WindowPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
        IFileManager::Get().MakeDirectory(*Root,true);
        UE_LOG(LogTemp,Display,TEXT("CF_WINDOW_PREVIEW_ROOT %s"),*Root);
        Hero->GetCharacterMovement()->DisableMovement();
        Hero->Feedback=TEXT("开放窗墙：窗洞可观察外部，窗框保留碰撞 | 支撑屋顶时不可直接拆除");
        auto Stage=MakeShared<int32>(0);auto Timer=MakeShared<FTimerHandle>();
        World->GetTimerManager().SetTimer(*Timer,[World,Hero,Root,Stage,Timer]()
        {
            const TCHAR* Names[]={TEXT("Outside"),TEXT("Corner"),TEXT("Inside")};
            if(*Stage>=6)
            {
                bool Ready=true;for(const auto* Name:Names)Ready &= IFileManager::Get().FileSize(*(Root/(FString(Name)+TEXT(".png"))))>1000;
                World->GetTimerManager().ClearTimer(*Timer);
                UE_LOG(LogTemp,Display,TEXT("CF_WINDOW_PREVIEW %s scenes=3"),Ready?TEXT("PASS"):TEXT("FAIL"));
                FPlatformMisc::RequestExitWithStatus(false,Ready?0:1);return;
            }
            const int32 Scene=*Stage/2;
            if(*Stage%2==0)
            {
                const FVector Positions[]={FVector(-400,-1300,90),FVector(-850,-1200,100),FVector(-400,-800,60)};
                Hero->SetActorLocation(Positions[Scene]);
                const auto View=(FVector(-400,-1000,140)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
                Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
            }
            else
            {
                auto* PC=Cast<APlayerController>(Hero->GetController());auto* HUD=PC?Cast<ACFHUD>(PC->GetHUD()):nullptr;
                if(!HUD || !HUD->GetLayoutAudit().bFits || !HUD->GetLayoutAudit().bNoOverlap)
                {World->GetTimerManager().ClearTimer(*Timer);UE_LOG(LogTemp,Error,TEXT("CF_WINDOW_PREVIEW FAIL layout"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
                FScreenshotRequest::RequestScreenshot(Root/(FString(Names[Scene])+TEXT(".png")),true,false);
            }
            ++*Stage;
        },.6f,true);
        return;
    }
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
void UHomeMovementProbe::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Tick)
{
    Super::TickComponent(DeltaTime,TickType,Tick);
    if(auto* Pawn=Cast<APawn>(GetOwner());Pawn && !Direction.IsNearlyZero()) Pawn->AddMovementInput(Direction);
}

namespace
{
AHomeModule* Find(UWorld* World,EHomeModuleKind Kind,FVector Position)
{
    for(TActorIterator<AHomeModule> It(World);It;++It)
        if(!It->bPreview && It->GetKind()==Kind && It->GetActorLocation().Equals(Position,.01)) return *It;
    return nullptr;
}
AResourceNode* Brush(UWorld* World)
{
    for(TActorIterator<AResourceNode> It(World);It;++It) if(It->PersistentId==FGuid(0xCF170000,0,0,1)) return *It;
    return nullptr;
}
AHomeModule* Build(ACFPlayerCharacter* Hero,EHomeModuleKind Kind,FVector Position,FGuid Floor=FGuid(),double Yaw=0)
{
    Hero->HomeBuilding->Selected=Kind;
    auto* Result=Hero->HomeBuilding->BuildAt(FTransform(FRotator(0,Yaw,0),Position),Floor);
    if(!Result) UE_LOG(LogTemp,Warning,TEXT("CF_HOME build rejected %d %s: %s"),static_cast<int32>(Kind),*Position.ToString(),*Hero->Feedback);
    return Result;
}
bool SnapshotOK(UWorld* World,ACFPlayerCharacter* Hero)
{
    const auto Layout=HomeLayout::Collect(World); FString Reason; TSet<FGuid> Ids; int32 Indoor=0;
    for(const auto& Record:Layout) Ids.Add(Record.Id);
    for(TActorIterator<AWorkStation> It(World);It;++It) if(It->bPlayerBuilt)
        if(FMath::IsNearlyEqual(It->GetActorLocation().Z,5.,.01) && HomeLayout::SupportsFacility(It->GetActorTransform(),Layout)) ++Indoor;
    auto* Source=Brush(World);
    return Layout.Num()==10 && Ids.Num()==10 && HomeLayout::Validate(Layout,Reason) && Indoor==1 &&
        Hero->Inventory->Count(EFrontierItem::Material)==116 && Source && Source->Remaining==7;
}
bool PathInside(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(World,FVector(-400,-1250,40),FVector(-400,-800,60),Hero);
    return Path && Path->IsValid() && !Path->IsPartial() && !Path->PathPoints.IsEmpty() && Path->PathPoints.Last().Z<100;
}
bool PlaceThroughPreview(UWorld* World,ACFPlayerCharacter* Hero,EHomeModuleKind Kind,FVector Observer,FVector Aim,FVector Expected,double Yaw=0)
{
    Hero->SetActorLocation(Observer);
    Hero->FirstPersonCamera->SetWorldRotation((Aim-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
    bool Input=true;
    for(int32 Step=0;Hero->HomeBuilding->Selected!=Kind && Step<8;++Step)Input &= CFInputQA::Press(Hero,EKeys::V);
    Hero->HomeBuilding->UpdatePreview();
    auto* Preview=Hero->HomeBuilding->GetPreview();
    if(Preview && !FMath::IsNearlyEqual(Preview->GetActorRotation().Yaw,Yaw,.01))Input &= CFInputQA::Press(Hero,EKeys::Nine);
    Hero->HomeBuilding->UpdatePreview();Preview=Hero->HomeBuilding->GetPreview();
    const bool Matched=Preview && Preview->GetActorLocation().Equals(Expected,.01) && FMath::IsNearlyEqual(Preview->GetActorRotation().Yaw,Yaw,.01);
    const int32 Before=Hero->Inventory->Count(EFrontierItem::Material);
    if(Input && Matched)Input &= CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton);
    auto* Built=Find(World,Kind,Expected);const auto* Definition=UHomeModuleDefinition::LoadDefault(Kind);
    const bool Pass=Input && Matched && Built && Definition && Hero->Inventory->Count(EFrontierItem::Material)==Before-Definition->MaterialCost;
    UE_LOG(LogTemp,Display,TEXT("CF_HOME_ROOM_INPUT_PART %s kind=%d expected=%s preview=%s material=%d feedback=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),static_cast<int32>(Kind),*Expected.ToString(),Preview?*Preview->GetActorLocation().ToString():TEXT("无"),Hero->Inventory->Count(EFrontierItem::Material),*Hero->Feedback);
    if(!Pass)UE_LOG(LogTemp,Display,TEXT("CF_HOME_ROOM_INPUT_REJECT matched=%d yaw=%.1f expectedYaw=%.1f hint=%s"),Matched,Preview?Preview->GetActorRotation().Yaw:-1,Yaw,*Hero->HomeBuilding->GetHint());
    return Pass;
}
}

void RunHomeScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode)
{
    const FString CrossSlot=TEXT("FrontierHomeCrossProcess");
    if(Mode==2)
    {
        const bool Pass=Hero->LoadProgress(CrossSlot) && SnapshotOK(World,Hero) && Hero->LoadProgress(CrossSlot) && SnapshotOK(World,Hero);
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_READ %s modules=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),HomeLayout::Collect(World).Num());
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1); return;
    }
    UCreatureDefinition* Leaf=nullptr;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It)
    { if(It->Definition && It->Definition->Form==ECreatureForm::Leaf) Leaf=It->Definition; It->Destroy(); }
    if(!Leaf || !Brush(World)) { UE_LOG(LogTemp,Error,TEXT("CF_HOME FAIL fixture")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    Hero->GetCharacterMovement()->DisableMovement(); Hero->SetActorLocation(FVector(-350,-1150,40));
    Hero->Inventory->Restore({{EFrontierItem::Material,200}});
    Hero->FirstPersonCamera->SetWorldRotation((FVector(-400,-800,-50)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
    bool Bindings=CFInputQA::UniqueBindings(Hero) && CFInputQA::Press(Hero,EKeys::B) && CFInputQA::Press(Hero,EKeys::Eight) &&
        CFInputQA::Action(Hero,TEXT("Attack"),EKeys::LeftMouseButton);
    Bindings &= CFInputQA::Press(Hero,EKeys::V) && CFInputQA::Press(Hero,EKeys::Nine);
    const bool Rotated=Hero->HomeBuilding->GetPreview() && FMath::IsNearlyEqual(Hero->HomeBuilding->GetPreview()->GetActorRotation().Yaw,90.,.01);
    Bindings &= CFInputQA::Press(Hero,EKeys::Nine) && CFInputQA::Press(Hero,EKeys::V) && CFInputQA::Press(Hero,EKeys::V) &&
        CFInputQA::Press(Hero,EKeys::V) && CFInputQA::Action(Hero,TEXT("Capture"),EKeys::RightMouseButton);
    auto* FloorA=Find(World,EHomeModuleKind::Floor,FVector(-400,-800,-40));
    bool Input=Bindings && Rotated && FloorA && !Hero->Building->IsActive() && Hero->Inventory->Count(EFrontierItem::Material)==192;
    UE_LOG(LogTemp,Display,TEXT("CF_HOME_INPUT_BINDINGS %s floor=%d rotated=%d cancelled=%d material=%d"),Input?TEXT("PASS"):TEXT("FAIL"),FloorA!=nullptr,Rotated,!Hero->Building->IsActive(),Hero->Inventory->Count(EFrontierItem::Material));
    if(!Input) { UE_LOG(LogTemp,Error,TEXT("CF_HOME FAIL input %s"),*Hero->Feedback); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    if(FParse::Param(FCommandLine::Get(),TEXT("CFHomeFenceTest")))
    {
        bool Pass=CFInputQA::Press(Hero,EKeys::B) && CFInputQA::Press(Hero,EKeys::Eight);
        Pass &= PlaceThroughPreview(World,Hero,EHomeModuleKind::Fence,FVector(-400,-800,60),FVector(-400,-980,-30),FVector(-400,-1000,-30));
        Pass &= PlaceThroughPreview(World,Hero,EHomeModuleKind::Fence,FVector(-400,-800,60),FVector(-400,-620,-30),FVector(-400,-600,-30));
        Pass &= PlaceThroughPreview(World,Hero,EHomeModuleKind::Fence,FVector(-400,-800,60),FVector(-580,-800,-30),FVector(-600,-800,-30),90);
        Pass &= CFInputQA::Action(Hero,TEXT("Capture"),EKeys::RightMouseButton);
        const int32 Before=Hero->Inventory->Count(EFrontierItem::Material);
        Pass &= Before==180 && !Build(Hero,EHomeModuleKind::Roof,FVector(-400,-800,280),FloorA->PersistentId) &&
            !Build(Hero,EHomeModuleKind::Fence,FVector(-400,-1000,-30),FloorA->PersistentId) && Hero->Inventory->Count(EFrontierItem::Material)==Before;
        auto* South=Find(World,EHomeModuleKind::Fence,FVector(-400,-1000,-30));
        auto Blocked=[&](){FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(FencePawn),false,Hero);
            return World->SweepSingleByChannel(Hit,FVector(-400,-1100,60),FVector(-400,-900,60),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,88),Query);};
        const bool Collision=Blocked();
        Hero->SetActorLocation(FVector(-400,-800,60));
        const bool Removed=South && Hero->HomeBuilding->Dismantle(South) && !Blocked();
        const bool Support=!Hero->HomeBuilding->Dismantle(FloorA);
        FString Reason;const auto Layout=HomeLayout::Collect(World);
        Pass &= Collision && Removed && Support && HomeLayout::Validate(Layout,Reason) && Layout.Num()==3 && Hero->Inventory->Count(EFrontierItem::Material)==182;
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_FENCE %s modules=%d material=%d collision=%d removed=%d floorGuard=%d noSave=1"),Pass?TEXT("PASS"):TEXT("FAIL"),Layout.Num(),Hero->Inventory->Count(EFrontierItem::Material),Collision,Removed,Support);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("CFHomeRoomInput")))
    {
        bool Room=CFInputQA::Press(Hero,EKeys::B) && CFInputQA::Press(Hero,EKeys::Eight);
        auto Place=[&](EHomeModuleKind Kind,FVector Observer,FVector Aim,FVector Expected,double Yaw=0){return PlaceThroughPreview(World,Hero,Kind,Observer,Aim,Expected,Yaw);};
        Room &= Place(EHomeModuleKind::Floor,FVector(-450,-900,60),FVector(-800,-800,-50),FVector(-800,-800,-40));
        Room &= Place(EHomeModuleKind::Door,FVector(-570,-850,60),FVector(-400,-980,-30),FVector(-400,-1000,-30));
        Room &= Place(EHomeModuleKind::Wall,FVector(-400,-800,60),FVector(-400,-620,-30),FVector(-400,-600,-30));
        Room &= Place(EHomeModuleKind::Wall,FVector(-400,-800,60),FVector(-220,-800,-30),FVector(-200,-800,-30),90);
        Room &= Place(EHomeModuleKind::Wall,FVector(-800,-800,60),FVector(-800,-980,-30),FVector(-800,-1000,-30));
        Room &= Place(EHomeModuleKind::Wall,FVector(-800,-800,60),FVector(-800,-620,-30),FVector(-800,-600,-30));
        Room &= Place(EHomeModuleKind::Window,FVector(-800,-800,60),FVector(-980,-800,-30),FVector(-1000,-800,-30),90);
        Room &= Place(EHomeModuleKind::Storage,FVector(-800,-850,60),FVector(-900,-700,-30),FVector(-900,-700,-30));
        Room &= Place(EHomeModuleKind::Roof,FVector(-400,-800,60),FVector(-400,-800,-30),FVector(-400,-800,280));
        Room &= Place(EHomeModuleKind::Roof,FVector(-800,-800,60),FVector(-800,-800,-30),FVector(-800,-800,280));
        Room &= CFInputQA::Action(Hero,TEXT("Capture"),EKeys::RightMouseButton) && !Hero->Building->IsActive();
        FString Reason;const auto Layout=HomeLayout::Collect(World);
        Room &= Layout.Num()==11 && HomeLayout::Validate(Layout,Reason) && Hero->Inventory->Count(EFrontierItem::Material)==112;
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_ROOM_INPUT %s modules=%d material=%d noSave=1"),Room?TEXT("PASS"):TEXT("FAIL"),Layout.Num(),Hero->Inventory->Count(EFrontierItem::Material));
        if(Room && FParse::Param(FCommandLine::Get(),TEXT("CFHomeRoomWork"))){RunHomeDoorFarmScenario(World,Hero,FParse::Param(FCommandLine::Get(),TEXT("CFHomeRoomWorkWrite"))?1:0,true);return;}
        FPlatformMisc::RequestExitWithStatus(false,Room?0:1);return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("CFHomeInputOnly")))
    {UE_LOG(LogTemp,Display,TEXT("CF_HOME_INPUT_ONLY PASS noSave=1"));FPlatformMisc::RequestExitWithStatus(false,0);return;}
    const FGuid FloorAId=FloorA->PersistentId;
    bool Reject=!Build(Hero,EHomeModuleKind::Floor,FVector(-400,-800,-40)) &&
        !Build(Hero,EHomeModuleKind::Floor,FVector(-350,-800,-40)) &&
        !Build(Hero,EHomeModuleKind::Roof,FVector(-400,-800,280),FloorAId) && Hero->Inventory->Count(EFrontierItem::Material)==192;
    auto* Blocker=World->SpawnActor<ACreatureCharacter>(FVector(0,-800,40),FRotator::ZeroRotator);
    Reject &= !Build(Hero,EHomeModuleKind::Floor,FVector(0,-800,-40)); Blocker->Destroy();
    auto* FloorB=Build(Hero,EHomeModuleKind::Floor,FVector(0,-800,-40));
    if(!FloorB) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
    const FGuid FloorBId=FloorB->PersistentId;
    bool Built=Build(Hero,EHomeModuleKind::Doorway,FVector(-400,-1000,-30),FloorAId) &&
        Build(Hero,EHomeModuleKind::Wall,FVector(-400,-600,-30),FloorAId) &&
        Build(Hero,EHomeModuleKind::Wall,FVector(-600,-800,-30),FloorAId,90) &&
        Build(Hero,EHomeModuleKind::Wall,FVector(0,-1000,-30),FloorBId) &&
        Build(Hero,EHomeModuleKind::Wall,FVector(0,-600,-30),FloorBId) &&
        Build(Hero,EHomeModuleKind::Wall,FVector(200,-800,-30),FloorBId,90) &&
        Build(Hero,EHomeModuleKind::Roof,FVector(-400,-800,280),FloorAId) &&
        Build(Hero,EHomeModuleKind::Roof,FVector(0,-800,280),FloorBId);
    Hero->Building->Selected=EWorkSpecialty::Gathering;
    auto* Indoor=Hero->Building->BuildAt(FTransform(FRotator::ZeroRotator,FVector(0,-800,5),FVector(2,1,.7)));
    Built &= Indoor && Hero->Inventory->Count(EFrontierItem::Material)==116 && HomeLayout::Collect(World).Num()==10;
    if(!Built) { UE_LOG(LogTemp,Error,TEXT("CF_HOME FAIL room %s"),*Hero->Feedback); FPlatformMisc::RequestExitWithStatus(false,1); return; }
    auto* Source=Brush(World); Source->SetActorLocation(FVector(-200,-900,0)); Source->Remaining=7; Source->Refresh();
    if(Mode==1)
    {
        const bool Pass=SnapshotOK(World,Hero) && Hero->SaveProgress(CrossSlot);
        UE_LOG(LogTemp,Display,TEXT("CF_HOME_WRITE %s"),Pass?TEXT("PASS"):TEXT("FAIL")); FPlatformMisc::RequestExitWithStatus(false,Pass?0:1); return;
    }
    Reject &= !Hero->HomeBuilding->Dismantle(FloorA) && !Hero->HomeBuilding->Dismantle(Find(World,EHomeModuleKind::Wall,FVector(-400,-600,-30)));
    Hero->Inventory->Restore({{EFrontierItem::Material,999}});
    Reject &= !Hero->HomeBuilding->Dismantle(Find(World,EHomeModuleKind::Roof,FVector(-400,-800,280)));
    Hero->Inventory->Restore({{EFrontierItem::Material,116}});
    const FString Slot=TEXT("FrontierHomeAutomation");
    bool Saved=Hero->SaveProgress(Slot) && Hero->LoadProgress(Slot) && SnapshotOK(World,Hero) && Hero->LoadProgress(Slot) && SnapshotOK(World,Hero);
    UGameplayStatics::SetGamePaused(World,true);
    Saved &= Hero->LoadProgress(Slot) && SnapshotOK(World,Hero) && UGameplayStatics::IsGamePaused(World);
    UGameplayStatics::SetGamePaused(World,false);
    bool Invalid=true;
    for(int32 Case=0;Case<8;++Case)
    {
        auto* LiveFloor=Find(World,EHomeModuleKind::Floor,FVector(-400,-800,-40));
        auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0));
        if(Case==0) { const auto Duplicate=Bad->HomeModules[0]; Bad->HomeModules.Add(Duplicate); }
        if(Case==1) Bad->HomeModules[0].Transform.AddToTranslation(FVector(0,0,100));
        if(Case==2) for(auto& Record:Bad->HomeModules) if(Record.Definition.Get() && Record.Definition.Get()->Kind==EHomeModuleKind::Roof) { Record.FloorId=FGuid::NewGuid(); break; }
        if(Case==3) Bad->HomeModules[0].Definition=TSoftObjectPtr<UHomeModuleDefinition>(FSoftObjectPath(TEXT("/Game/Missing.Home")));
        if(Case==4) Bad->HomeModules.RemoveAll([](const FHomeModuleRecord& Record){return Record.Transform.GetLocation().Equals(FVector(-400,-600,-30),.01);});
        if(Case==5) { FCreatureSaveRecord Record; Record.Id=FGuid::NewGuid(); Record.CreatureClass=ACreatureCharacter::StaticClass(); Record.Health=-1; Bad->Creatures.Add(Record); }
        if(Case==6) for(auto& Record:Bad->HomeModules) if(Record.Id==FloorBId) { Record.Transform.SetLocation(FVector(400,-800,-40)); break; }
        if(Case==7) Bad->HomeModules[0].PaidMaterials=1000;
        UGameplayStatics::SaveGameToSlot(Bad,TEXT("FrontierHomeInvalid"),0);
        Invalid &= !Hero->LoadProgress(TEXT("FrontierHomeInvalid")) && IsValid(LiveFloor) && LiveFloor->GetActorEnableCollision() && SnapshotOK(World,Hero);
    }
    auto* TerrainBlock=World->SpawnActor<AActor>(); auto* TerrainBox=NewObject<UBoxComponent>(TerrainBlock);
    TerrainBlock->SetRootComponent(TerrainBox); TerrainBox->SetBoxExtent(FVector(80,20,80)); TerrainBox->SetCollisionObjectType(ECC_WorldStatic);
    TerrainBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); TerrainBox->SetCollisionResponseToAllChannels(ECR_Block); TerrainBox->SetCanEverAffectNavigation(false);
    TerrainBox->RegisterComponent(); TerrainBlock->SetActorLocation(FVector(-400,-600,80));
    Invalid &= !Hero->LoadProgress(Slot) && SnapshotOK(World,Hero); TerrainBlock->Destroy();
    auto* PaidRoof=Find(World,EHomeModuleKind::Roof,FVector(0,-800,280));
    auto* RoofDefinition=PaidRoof->Definition.Get(); const int32 OriginalCost=RoofDefinition->MaterialCost;
    RoofDefinition->MaterialCost=14;
    bool PaidRefund=Hero->HomeBuilding->Dismantle(PaidRoof) && Hero->Inventory->Count(EFrontierItem::Material)==121;
    RoofDefinition->MaterialCost=OriginalCost;
    PaidRefund &= Build(Hero,EHomeModuleKind::Roof,FVector(0,-800,280),FloorBId) && Hero->Inventory->Count(EFrontierItem::Material)==111;
    PaidRefund &= Hero->LoadProgress(Slot) && SnapshotOK(World,Hero);
    // 即使没有墙壁，也不能拆除室内设施脚下的地板。
    Hero->SetActorLocation(FVector(350,-1050,40));
    bool Support=Hero->HomeBuilding->Dismantle(Find(World,EHomeModuleKind::Roof,FVector(0,-800,280))) &&
        Hero->HomeBuilding->Dismantle(Find(World,EHomeModuleKind::Wall,FVector(0,-1000,-30))) &&
        Hero->HomeBuilding->Dismantle(Find(World,EHomeModuleKind::Wall,FVector(0,-600,-30))) &&
        Hero->HomeBuilding->Dismantle(Find(World,EHomeModuleKind::Wall,FVector(200,-800,-30)));
    auto* LiveB=Find(World,EHomeModuleKind::Floor,FVector(0,-800,-40));
    Support &= !Hero->HomeBuilding->Dismantle(LiveB);
    for(TActorIterator<AWorkStation> It(World);It;++It) if(It->bPlayerBuilt) Support &= Hero->Building->DismantleStation(*It);
    Support &= Hero->HomeBuilding->Dismantle(LiveB) && Hero->LoadProgress(Slot) && SnapshotOK(World,Hero);
    auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,0)); Legacy->FormatVersion=17; Legacy->Facilities.Reset();
    FFacilitySaveRecord LegacySite; LegacySite.Id=FGuid::NewGuid(); LegacySite.Specialty=EWorkSpecialty::Gathering;
    LegacySite.Transform=FTransform(FRotator::ZeroRotator,FVector(0,-800,-14),FVector(2,1,.7)); Legacy->Facilities.Add(LegacySite);
    UGameplayStatics::SaveGameToSlot(Legacy,TEXT("FrontierHomeLegacy"),0);
    bool Migration=Hero->LoadProgress(TEXT("FrontierHomeLegacy")) && HomeLayout::Collect(World).IsEmpty() && Hero->Inventory->Count(EFrontierItem::Material)==116 && Brush(World)->Remaining==7;
    int32 LegacySites=0;
    for(TActorIterator<AWorkStation> It(World);It;++It) if(It->bPlayerBuilt && FMath::IsNearlyEqual(It->GetActorLocation().Z,-14.,.01)) ++LegacySites;
    Migration &= LegacySites==1;
    Migration &= Hero->LoadProgress(Slot) && SnapshotOK(World,Hero);
    Hero->GetCharacterMovement()->DisableMovement(); Hero->SetActorLocation(FVector(-400,-1250,40));
    bool Sealed=Hero->HomeBuilding->Dismantle(Find(World,EHomeModuleKind::Roof,FVector(-400,-800,280))) &&
        Hero->HomeBuilding->Dismantle(Find(World,EHomeModuleKind::Doorway,FVector(-400,-1000,-30))) &&
        Build(Hero,EHomeModuleKind::Wall,FVector(-400,-1000,-30),FloorAId) && Build(Hero,EHomeModuleKind::Roof,FVector(-400,-800,280),FloorAId);
    const bool Baseline=Input && Reject && Built && Saved && Invalid && PaidRefund && Support && Migration && Sealed;
    UE_LOG(LogTemp,Display,TEXT("CF_HOME checks input=%d reject=%d build=%d saved=%d invalid=%d paid_refund=%d support=%d migration=%d sealed=%d"),Input,Reject,Built,Saved,Invalid,PaidRefund,Support,Migration,Sealed);
    const bool Preview=FParse::Param(FCommandLine::Get(),TEXT("CFHomePreview"));
    auto* Movement=NewObject<UHomeMovementProbe>(Hero); Movement->RegisterComponent();
    TSharedRef<FTimerHandle> Poll=MakeShared<FTimerHandle>();
    World->GetTimerManager().SetTimer(*Poll,[World,Hero,Leaf,FloorAId,Baseline,Preview,Movement,Poll,Phase=0,Started=World->GetTimeSeconds(),Pass=true,Pet=static_cast<ACreatureCharacter*>(nullptr),WorkBefore=0]() mutable
    {
        const float Elapsed=World->GetTimeSeconds()-Started;
        auto Advance=[&](int32 Next){ Phase=Next; Started=World->GetTimeSeconds(); };
        if(Phase==0 && Elapsed>=2)
        {
            FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(HomeClosed),false,Hero);
            const bool Wall=World->LineTraceSingleByChannel(Hit,FVector(-400,-1200,80),FVector(-400,-800,80),ECC_Visibility,Params) && Cast<AHomeModule>(Hit.GetActor());
            Pass &= Wall && !PathInside(World,Hero);
            Pass &= Hero->HomeBuilding->Dismantle(Find(World,EHomeModuleKind::Roof,FVector(-400,-800,280))) &&
                Hero->HomeBuilding->Dismantle(Find(World,EHomeModuleKind::Wall,FVector(-400,-1000,-30))) &&
                Build(Hero,EHomeModuleKind::Doorway,FVector(-400,-1000,-30),FloorAId) && Build(Hero,EHomeModuleKind::Roof,FVector(-400,-800,280),FloorAId);
            Advance(1); return;
        }
        if(Phase==1 && Elapsed>=2)
        {
            Pass &= PathInside(World,Hero);
            Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking); Hero->GetController()->SetControlRotation(FRotator(0,90,0)); Movement->Direction=FVector(0,1,0); Advance(2);
        }
        if(Phase==2)
        {
            if(Hero->GetActorLocation().Y<-880 && Elapsed<6) return;
            Pass &= Hero->GetActorLocation().Y>-950 && Hero->GetActorLocation().Z>50;
            Hero->GetCharacterMovement()->StopMovementImmediately(); Movement->Direction=FVector(0,-1,0); Advance(3); return;
        }
        if(Phase==3)
        {
            if(Hero->GetActorLocation().Y>-1200 && Elapsed<6) return;
            Pass &= Hero->GetActorLocation().Y<-1100;
            Movement->Direction=FVector::ZeroVector; Hero->GetCharacterMovement()->DisableMovement(); Hero->SetActorLocation(FVector(-400,-680,60)); Hero->GetController()->SetControlRotation(FRotator(0,90,0));
            const FTransform Transform(FVector(-400,-1350,40));
            Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            Pet->Definition=Leaf; Pet->FinishSpawning(Transform); Pet->Tame(Hero->GetController()); Pet->SetActivity(ECreatureActivity::Follow); Advance(4); return;
        }
        if(Phase==4)
        {
            if((Pet->GetActorLocation().Y<-950 || FVector::Dist2D(Pet->GetActorLocation(),Pet->GetFollowDestination())>40 || Pet->GetVelocity().Size2D()>15) && Elapsed<10) return;
            Pass &= Pet->GetActorLocation().Y>-950 && Pet->GetActorLocation().Z>50 && FVector::Dist2D(Pet->GetActorLocation(),Pet->GetFollowDestination())<=40;
            for(TActorIterator<AWorkStation> It(World);It;++It) if(It->bPlayerBuilt) Pet->WorkSite=*It;
            Pet->SetActivity(ECreatureActivity::Work); WorkBefore=Hero->Inventory->Count(EFrontierItem::Material); Advance(5); return;
        }
        if(Phase==5)
        {
            if(Hero->Inventory->Count(EFrontierItem::Material)==WorkBefore && Elapsed<8) return;
            Pass &= Hero->Inventory->Count(EFrontierItem::Material)==WorkBefore+1 && Brush(World)->Remaining==6 && Pet->Experience==1;
            Hero->CommandFollow(); Hero->SetActorLocation(FVector(-400,-1250,40)); Hero->GetController()->SetControlRotation(FRotator(0,-90,0)); Advance(6); return;
        }
        if(Phase==6)
        {
            if(Pet->GetActorLocation().Y>-1050 && Elapsed<10) return;
            Pass &= Pet->GetActorLocation().Y<-1050;
            const bool Final=Baseline && Pass;
            UE_LOG(LogTemp,Display,TEXT("CF_HOME %s physics_navigation_work=%d hero=%s pet=%s"),Final?TEXT("PASS"):TEXT("FAIL"),Pass,*Hero->GetActorLocation().ToString(),*Pet->GetActorLocation().ToString());
            FTimerHandle ActivePoll=*Poll;
            if(Preview && Final)
            {
                Hero->FirstPersonCamera->bUsePawnControlRotation=false; Hero->FirstPersonCamera->SetAbsolute(true,true,false);
                Hero->FirstPersonCamera->SetWorldLocation(FVector(-1050,-1450,380)); Hero->FirstPersonCamera->SetWorldRotation((FVector(-200,-800,80)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
                FTimerHandle Exterior,Interior,Done;
                World->GetTimerManager().SetTimer(Exterior,[](){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("HomeExterior.png"),false,false);},1.f,false);
                World->GetTimerManager().SetTimer(Interior,[Hero](){Hero->FirstPersonCamera->SetWorldLocation(FVector(-400,-900,150)); Hero->FirstPersonCamera->SetWorldRotation((FVector(0,-760,80)-Hero->FirstPersonCamera->GetComponentLocation()).Rotation()); FTimerHandle Shot; Hero->GetWorldTimerManager().SetTimer(Shot,[](){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("HomeInterior.png"),false,false);},.5f,false);},2.f,false);
                World->GetTimerManager().SetTimer(Done,[](){FPlatformMisc::RequestExitWithStatus(false,0);},4.f,false);
            }
            else FPlatformMisc::RequestExitWithStatus(false,Final?0:1);
            World->GetTimerManager().ClearTimer(ActivePoll);
        }
    },.05f,true);
}
