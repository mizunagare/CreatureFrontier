#include "HUDScenario.h"
#include "CFHUD.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "CompanionMemoryComponent.h"
#include "CompanionPersonalityComponent.h"
#include "InventoryComponent.h"
#include "ResearchComponent.h"
#include "ResourceNode.h"
#include "BaseBuildingComponent.h"
#include "WorkStation.h"
#include "FarmCropComponent.h"
#include "InputRoutingQA.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Containers/Ticker.h"
#include "UnrealClient.h"
#include "FrontierOrientation.h"

namespace CFHUDQA
{
bool Layouts()
{
    bool Pass=true;
    for(const auto Size:TArray<FIntPoint>{{800,450},{960,540},{1024,768},{1280,720},{1600,900},{1920,1080},{2560,1440}})for(bool Interface:{false,true})
    {
        const auto L=FFrontierHUDLayout::Build(Size.X,Size.Y,Interface);
        TArray<FBox2D> Rects={L.Vitals,L.Feedback,L.Alert};if(!Interface){Rects.Add(L.Objective);Rects.Add(L.Research);Rects.Add(L.Context);Rects.Add(L.Compass);}
        for(int32 A=0;A<Rects.Num();++A)
        {Pass &= L.Contains(Rects[A]) && (Interface || !FFrontierHUDLayout::Overlap(Rects[A],L.Center));for(int32 B=A+1;B<Rects.Num();++B)Pass &= !FFrontierHUDLayout::Overlap(Rects[A],Rects[B]);}
        Pass &= FFrontierHUDLayout::Overlap(L.Vitals,L.Vitals) && FFrontierHUDLayout::Overlap(L.Center,L.Center) && !L.Contains(FFrontierHUDLayout::Box(-1,0,40,40));
    }
    return Pass;
}
bool Orientation()
{
    bool Pass=true;const TCHAR* WorldNames[]={TEXT("东"),TEXT("东北"),TEXT("北"),TEXT("西北"),TEXT("西"),TEXT("西南"),TEXT("南"),TEXT("东南")};
    const TCHAR* RelativeNames[]={TEXT("前方"),TEXT("右前"),TEXT("右侧"),TEXT("右后"),TEXT("后方"),TEXT("左后"),TEXT("左侧"),TEXT("左前")};
    for(int32 I=0;I<8;++I)
    {
        for(double Offset:{-720.,-360.,0.,360.,720.})Pass &= FString(FrontierOrientation::Heading(I*45.+Offset))==WorldNames[I];
        const double A=FMath::DegreesToRadians(I*45.);const FVector Home(FMath::Cos(A)*2000,FMath::Sin(A)*2000,0);
        Pass &= FrontierOrientation::HomeHint(FVector::ZeroVector,0,Home).Contains(RelativeNames[I]);
        Pass &= FrontierOrientation::HomeHint(FVector::ZeroVector,I*45.,Home).Contains(TEXT("前方"));
    }
    Pass &= FString(FrontierOrientation::Heading(22.4))==TEXT("东") && FString(FrontierOrientation::Heading(22.6))==TEXT("东北") && FString(FrontierOrientation::Heading(-22.4))==TEXT("东") && FString(FrontierOrientation::Heading(-22.6))==TEXT("东南");
    Pass &= FrontierOrientation::HomeHint(FVector(800,0,0),0,FVector::ZeroVector).Contains(TEXT("附近8米")) && !FrontierOrientation::HomeHint(FVector(801,0,0),0,FVector::ZeroVector).Contains(TEXT("附近"));
    Pass &= FrontierOrientation::HomeHint(FVector(0,0,-1000),0,FVector::ZeroVector).Contains(TEXT("上方10米")) && FrontierOrientation::HomeHint(FVector(0,0,1000),0,FVector::ZeroVector).Contains(TEXT("下方10米"));
    Pass &= FrontierOrientation::HomeHint(FVector(500,0,1000),0,FVector::ZeroVector).Contains(TEXT("直线")) && !FrontierOrientation::HomeHint(FVector(500,0,1000),0,FVector::ZeroVector).Contains(TEXT("附近"));
    UE_LOG(LogTemp,Display,TEXT("CF_ORIENTATION %s headings=40 relative=16 boundary=6 height=3"),Pass?TEXT("PASS"):TEXT("FAIL"));return Pass;
}
void AimHUD(ACFPlayerCharacter* Hero,AActor* Target,FVector Offset)
{
    Hero->SetActorLocation(Target->GetActorLocation()+Offset);const auto View=(Target->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation();
    Hero->GetController()->SetControlRotation(View);Hero->FirstPersonCamera->SetWorldRotation(View);
}
struct FPreview
{
    UWorld* World=nullptr;ACFPlayerCharacter* Hero=nullptr;ACFHUD* HUD=nullptr;ACreatureCharacter* Pet=nullptr;ACreatureCharacter* Boss=nullptr;AResourceNode* Resource=nullptr;AWorkStation* Garden=nullptr;
    FString Root;int32 Step=0;bool AwaitShot=false;double Next=0,Deadline=0;uint64 LastFrame=0;TMap<EFrontierItem,int32> Bag;
    bool Rescue=false;
    const TCHAR* Name() const {const TCHAR* Names[]={TEXT("Companion"),TEXT("Roster"),TEXT("RosterDanger"),TEXT("HelpDanger"),TEXT("Resource"),TEXT("Garden"),TEXT("Building"),TEXT("Downed"),TEXT("CompanionDownedNoMedicine"),TEXT("CompanionDownedMedicine")};return Names[Step];}
    void Setup()
    {
        if(Hero->IsRosterOpen())Hero->ToggleRoster();if(Hero->IsHelpVisible())Hero->ToggleHelp();if(Hero->Building->IsActive())Hero->ToggleBuilding();Hero->Health=100;
        Hero->Feedback=TEXT("一段很长的探索反馈：伙伴记得这次旅程，也保留了原来的工作指派。按F1查看操作帮助，绘制此面板不会改变任何玩法状态。")+FString::ChrN(120,TEXT('W'));
        if(Step<=1){AimHUD(Hero,Pet,FVector(0,-200,0));if(Step==1){Hero->ToggleRoster();const auto Roster=Hero->GetCompanionRoster();for(int32 I=0;I<Roster.Num() && Hero->GetRosterHighlight()!=Roster.Last();++I)Hero->NextRosterCompanion();if(auto* Highlight=Hero->GetRosterHighlight())Highlight->Nickname=FString::ChrN(24,TEXT('苔'));}}
        if(Step==2 || Step==3){AimHUD(Hero,Boss,FVector(-250,0,0));Boss->AlphaWindup=1.2f;if(Step==2)Hero->ToggleRoster();else Hero->ToggleHelp();}
        if(Step==4)AimHUD(Hero,Resource,FVector(-230,0,40));
        if(Step==5)AimHUD(Hero,Garden,FVector(0,-230,55));
        if(Step==6){Hero->SetActorLocation(FVector(-450,-1000,40));Hero->ToggleBuilding();
            if(FParse::Param(FCommandLine::Get(),TEXT("CFHUDHomePreview")))
            {
                Hero->Building->SwitchCategory();const FString Hint=Hero->Building->GetHint();
                const bool Complete=Hint.Contains(TEXT("房屋")) && Hint.Contains(TEXT("V选择")) && Hint.Contains(TEXT("9旋转")) &&
                    Hint.Contains(TEXT("8设施")) && Hint.Contains(TEXT("Insert替换")) && Hint.Contains(TEXT("左键放置")) &&
                    Hint.Contains(TEXT("右键/B取消")) && Hint.Contains(TEXT("X拆除"));
                UE_LOG(LogTemp,Display,TEXT("CF_HUD_HOME_HINT %s"),Complete?TEXT("PASS"):TEXT("FAIL"));
                if(!Complete)FPlatformMisc::RequestExitWithStatus(false,1);
            }}
        if(Step==7){AimHUD(Hero,Pet,FVector(0,-200,0));Hero->Health=0;}
        // 两个新增画面只验证救助信息，不冒充自然战斗或实际用药验收。
        if(Step>=8){AimHUD(Hero,Pet,FVector(0,-200,0));Pet->Health=0;Hero->Inventory->Restore({{EFrontierItem::Material,100},{EFrontierItem::Food,3},{EFrontierItem::Medicine,Step==9?1:0}});Bag=Hero->Inventory->GetStacks();}
        Next=FPlatformTime::Seconds()+.5;AwaitShot=false;
    }
    bool Tick()
    {
        const double Now=FPlatformTime::Seconds();if(Now>Deadline){UE_LOG(LogTemp,Error,TEXT("CF_HUD_PREVIEW FAIL timeout step=%d"),Step);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
        if(Now<Next)return true;
        if(AwaitShot)
        {
            if(IFileManager::Get().FileSize(*(Root/(FString(Name())+TEXT(".png"))))<1000)return true;
            if(++Step==(Rescue?10:8)){UE_LOG(LogTemp,Display,TEXT("CF_HUD_PREVIEW PASS scenes=%d"),Rescue?10:8);FPlatformMisc::RequestExitWithStatus(false,0);return false;}
            LastFrame=HUD->GetLayoutAudit().Frame;Setup();return true;
        }
        const auto& A=HUD->GetLayoutAudit();if(A.Frame<=LastFrame)return true;
        const EFrontierHUDContext Expected=Step==0 || Step>=7?EFrontierHUDContext::Companion:Step<=3?EFrontierHUDContext::None:Step==4?EFrontierHUDContext::Resource:Step==5?EFrontierHUDContext::Garden:EFrontierHUDContext::Building;
        bool SameBag=true;for(int32 Item=0;Item<static_cast<int32>(EFrontierItem::Count);++Item)SameBag &= Hero->Inventory->Count(static_cast<EFrontierItem>(Item))==Bag.FindRef(static_cast<EFrontierItem>(Item));
        const bool RosterValid=(Step==1 || Step==2)?A.RosterRows>0 && A.RosterRows<8:A.RosterRows==0;
        const bool Pass=A.bFits && A.bNoOverlap && A.bCenterClear && A.CompassLines==(A.bInterface?0:2) && A.Context==Expected && A.bInterface==(Step>=1 && Step<=3) && A.bAlert==(Step==2 || Step==3 || Step==7) && A.TextLines>0 && RosterValid && SameBag && Pet->BondPoints==0 && Pet->Experience==0 && Pet->Stamina==35 && Pet->Nutrition==40 && Pet->Memory->GetMemory()==ECrystalJourneyMemory::QuietThanks;
        UE_LOG(LogTemp,Display,TEXT("CF_ORIENTATION_FRAME %s scene=%s compassLines=%d interface=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Name(),A.CompassLines,A.bInterface);
        UE_LOG(LogTemp,Display,TEXT("CF_HUD_FRAME %s scene=%s viewport=%.0fx%.0f fit=%d separate=%d center=%d context=%d interface=%d alert=%d lines=%d elided=%d rows=%d"),Pass?TEXT("PASS"):TEXT("FAIL"),Name(),A.Viewport.X,A.Viewport.Y,A.bFits,A.bNoOverlap,A.bCenterClear,int32(A.Context),A.bInterface,A.bAlert,A.TextLines,A.Elided,A.RosterRows);
        if(!Pass){FPlatformMisc::RequestExitWithStatus(false,1);return false;}
        FScreenshotRequest::RequestScreenshot(Root/(FString(Name())+TEXT(".png")),true,false);AwaitShot=true;Next=Now+.25;return true;
    }
};
}
void RunHUDScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Preview)
{
    const bool Geometry=CFHUDQA::Layouts() && CFInputQA::UniqueBindings(Hero) && CFHUDQA::Orientation();UE_LOG(LogTemp,Display,TEXT("CF_HUD_LAYOUT %s configurations=14 negativeControls=1"),Geometry?TEXT("PASS"):TEXT("FAIL"));
    if(!Preview || !Geometry){FPlatformMisc::RequestExitWithStatus(false,Geometry?0:1);return;}
    auto* PC=Cast<APlayerController>(Hero->GetController());auto* HUD=PC?Cast<ACFHUD>(PC->GetHUD()):nullptr;UCreatureDefinition* Leaf=nullptr;UCreatureDefinition* Stone=nullptr;
    for(TActorIterator<ACreatureCharacter> It(World);It;++It){if(It->Definition && It->Definition->Form==ECreatureForm::Leaf)Leaf=It->Definition;if(It->Definition && It->Definition->Form==ECreatureForm::Stone)Stone=It->Definition;It->Destroy();}
    auto State=MakeShared<CFHUDQA::FPreview>();State->World=World;State->Hero=Hero;State->HUD=HUD;
    State->Rescue=FParse::Param(FCommandLine::Get(),TEXT("CFHUDRescuePreview"));
    for(TActorIterator<AResourceNode> It(World);It;++It)if(It->PersistentId==FGuid(0xCF170000,0,0,1))State->Resource=*It;
    if(!HUD || !Leaf || !Stone || !State->Resource){UE_LOG(LogTemp,Error,TEXT("CF_HUD_PREVIEW FAIL fixture"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Hero->GetCharacterMovement()->DisableMovement();Hero->SetActorLocation(FVector(-450,0,90));Hero->Inventory->Restore({{EFrontierItem::Material,100},{EFrontierItem::Food,3}});
    Hero->Building->Selected=EWorkSpecialty::Farming;State->Garden=Hero->Building->BuildAt(FTransform(FRotator::ZeroRotator,FVector(-400,-700,-15),FVector(2,1,.7)));
    if(!State->Garden){UE_LOG(LogTemp,Error,TEXT("CF_HUD_PREVIEW FAIL garden fixture: %s"),*Hero->Feedback);FPlatformMisc::RequestExitWithStatus(false,1);return;}
    State->Garden->Crop->Seeds=2;State->Garden->Crop->bPlanted=true;State->Garden->Crop->GrowthWork=3.5f;State->Garden->RefreshFarm();
    auto Make=[World,Hero](UCreatureDefinition* Definition,FVector Position,bool Owned)
    {const FTransform At(FRotator(0,-90,0),Position);auto* Pet=World->SpawnActorDeferred<ACreatureCharacter>(ACreatureCharacter::StaticClass(),At,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);Pet->Definition=Definition;Pet->FinishSpawning(At);if(Owned)Pet->Tame(Hero->GetController());Pet->SetActorTickEnabled(false);Pet->GetCharacterMovement()->DisableMovement();return Pet;};
    State->Pet=Make(Leaf,FVector(-450,-850,40),true);State->Pet->Nickname=TEXT("小苔");State->Pet->AssignFieldResource(State->Resource);State->Pet->bResumeWorkAfterRest=true;State->Pet->SetActivity(ECreatureActivity::Rest);State->Pet->Stamina=35;State->Pet->Nutrition=40;State->Pet->CarriedFood=3;
    State->Pet->Personality->RestoreSnapshot(ECompanionPreference::Quiet,true);State->Pet->Memory->RestoreSnapshot(ECrystalJourneyMemory::QuietThanks);FResearchProgress Progress;Progress.bCrystalSample=true;Hero->Research->Restore(Progress);
    for(int32 I=1;I<8;++I){auto* P=Make(Leaf,FVector(-450+200*I,-850,40),true);P->Nickname=FString::Printf(TEXT("旅途伙伴%d"),I);}
    State->Boss=Make(Stone,FVector(900,-900,40),false);State->Boss->bAlpha=true;
    State->Bag=Hero->Inventory->GetStacks();
    State->Root=FPaths::ProjectSavedDir()/TEXT("HUDPreview")/FGuid::NewGuid().ToString(EGuidFormats::Digits);IFileManager::Get().MakeDirectory(*State->Root,true);UE_LOG(LogTemp,Display,TEXT("CF_HUD_PREVIEW_ROOT %s"),*State->Root);
    if(FParse::Param(FCommandLine::Get(),TEXT("CFHUDStancePreview")))
    {
        Hero->ClearCompanionSelection();
        if(!CFInputQA::Press(Hero,EKeys::F8) || !State->Pet->bAvoidCombat)
        {UE_LOG(LogTemp,Error,TEXT("CF_HUD_STANCE FAIL input"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        UE_LOG(LogTemp,Display,TEXT("CF_HUD_STANCE PASS avoid=1"));
    }
    State->Deadline=FPlatformTime::Seconds()+45;State->Setup();FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float){return State->Tick();}),.1f);
}
