#include "CompanionMemoryComponent.h"
#include "CreatureCharacter.h"
#include "CFPlayerCharacter.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "BaseBuildingComponent.h"
#include "CompanionNamingComponent.h"
#include "StorageInteractionComponent.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

class SJourneyMemoryPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SJourneyMemoryPanel){} SLATE_END_ARGS()
    void Construct(const FArguments&,TSharedRef<SWidget> Content,TFunction<void(int32)> InChoose)
    {Choose=MoveTemp(InChoose);ChildSlot[Content];}
    virtual bool SupportsKeyboardFocus() const override {return true;}
    virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent& Event) override
    {
        if(Event.GetKey()==EKeys::Escape){Choose(0);return FReply::Handled();}
        if(Event.GetKey()==EKeys::One || Event.GetKey()==EKeys::NumPadOne){Choose(1);return FReply::Handled();}
        if(Event.GetKey()==EKeys::Two || Event.GetKey()==EKeys::NumPadTwo){Choose(2);return FReply::Handled();}
        return FReply::Unhandled();
    }
private:
    TFunction<void(int32)> Choose;
};
bool UCompanionMemoryComponent::ValidateSnapshot(ECrystalJourneyMemory State,bool OwnedTamed,bool Sampled)
{
    return State==ECrystalJourneyMemory::None || (OwnedTamed && Sampled &&
        (State==ECrystalJourneyMemory::Pending || State==ECrystalJourneyMemory::QuietThanks || State==ECrystalJourneyMemory::Celebration));
}
void UCompanionMemoryComponent::RestoreSnapshot(ECrystalJourneyMemory State)
{if(GetOwner()->HasAuthority()) Memory=State;}
FString UCompanionMemoryComponent::GetSummary() const
{
    switch(Memory)
    {
    case ECrystalJourneyMemory::Pending:return TEXT("水晶之旅 | 回家后按 F7 分享回忆");
    case ECrystalJourneyMemory::QuietThanks:return TEXT("水晶之旅 | 归家后的轻声致谢");
    case ECrystalJourneyMemory::Celebration:return TEXT("水晶之旅 | 归家后一起庆祝");
    default:return TEXT("尚无共同旅程回忆");
    }
}
void UCompanionMemoryComponent::RecordHarvest(AResourceNode* Source,ACFPlayerCharacter* Hero,AActor* Harvester)
{
    if(!IsValid(Source) || !Source->HasAuthority() || !Source->Definition || Source->Definition->Discovery!=EFrontierResearch::CrystalSample ||
        !IsValid(Hero) || Hero->Health<=0 || !Source->CanReach(Hero,800)) return;
    int32 Witnesses=0;
    for(TActorIterator<ACreatureCharacter> It(Source->GetWorld());It;++It)
    {
        auto* Pet=*It;
        if(Pet->IsActorBeingDestroyed() || Pet->Health<=0 || Pet->Disposition!=ECreatureDisposition::Tamed || Pet->Tamer!=Hero->GetController() ||
            Pet->Memory->Memory!=ECrystalJourneyMemory::None || !Source->CanReach(Pet,600) ||
            (Pet->Activity!=ECreatureActivity::Follow && Pet->Activity!=ECreatureActivity::Combat && !(Pet==Harvester && Pet->Activity==ECreatureActivity::Work))) continue;
        Pet->Memory->Memory=ECrystalJourneyMemory::Pending;++Witnesses;
    }
    if(Witnesses>0) Hero->Feedback=FString::Printf(TEXT("%d 位伙伴见证了水晶发现。一起回家后，瞄准伙伴按 F7"),Witnesses);
}
bool UCompanionMemoryComponent::CanShareAtHome(ACFPlayerCharacter* Hero) const
{
    const auto* Pet=Cast<ACreatureCharacter>(GetOwner());
    if(!IsValid(Hero) || !Hero->HasAuthority() || Hero->Health<=0 || !Pet || Pet->IsActorBeingDestroyed() || Pet->GetWorld()!=Hero->GetWorld() ||
        Pet->Health<=0 || Pet->Disposition!=ECreatureDisposition::Tamed || Pet->Tamer!=Hero->GetController() || Hero->GetDistanceTo(Pet)>250 ||
        (Pet->Activity!=ECreatureActivity::Follow && Pet->Activity!=ECreatureActivity::Rest) || Pet->FocusTarget || Pet->SpecialWindupRemaining>0 ||
        FVector::Dist(Hero->GetActorLocation(),UBaseBuildingComponent::BaseOrigin())>800 || !Hero->IsNearWorkStation()) return false;
    for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)
        if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0 && It->Activity!=ECreatureActivity::ReturnHome && Pet->GetDistanceTo(*It)<600) return false;
    FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(JourneyRecallSight),false,Hero);
    return !GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetPawnViewLocation(),Pet->GetActorLocation(),ECC_Visibility,Params) || Hit.GetActor()==Pet;
}
bool UCompanionMemoryComponent::CanRecall(ACFPlayerCharacter* Hero) const
{return (Memory==ECrystalJourneyMemory::Pending || RescueMemory==ERescueJourneyMemory::Pending) && CanShareAtHome(Hero);}
bool UCompanionMemoryComponent::Open(ACFPlayerCharacter* Hero)
{
    auto* PC=IsValid(Hero)?Cast<APlayerController>(Hero->GetController()):nullptr;
    if(IsOpen() || !CanRecall(Hero) || !PC || PC->GetPawn()!=Hero || UGameplayStatics::IsGamePaused(this) || !GetWorld()->GetGameViewport() || !FSlateApplication::IsInitialized() ||
        Hero->Building->IsActive() || Hero->Storage->IsOpen() || Hero->IsBackpackOpen() || Hero->IsHelpVisible() || Hero->IsJournalOpen() || Hero->Naming->IsOpen()) return false;
    if(!UGameplayStatics::SetGamePaused(this,true)) return false;
    bRecallingRescue=Memory!=ECrystalJourneyMemory::Pending;
    Listener=Hero;if(Hero->IsRosterOpen())Hero->ToggleRoster();Hero->CancelMeleeAttack();Hero->GetCharacterMovement()->StopMovementImmediately();Hero->StopJumping();Hero->ConsumeMovementInputVector();
    DialogController=PC;
    PC->OnPossessedPawnChanged.AddUniqueDynamic(this,&UCompanionMemoryComponent::HandleDialogPawnChanged);
    Hero->OnDestroyed.AddUniqueDynamic(this,&UCompanionMemoryComponent::HandleDialogListenerDestroyed);
    const auto* Pet=CastChecked<ACreatureCharacter>(GetOwner());
    const TWeakObjectPtr<UCompanionMemoryComponent> WeakSelf(this);
    auto Choice=[WeakSelf](int32 Reply){if(auto* Self=WeakSelf.Get()){if(Reply==0)Self->Close();else Self->Commit(Reply==2);}};
    auto Content=SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.015f,.025f,.04f,.97f)).ForegroundColor(FLinearColor::White).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(24)
    [SNew(SBox).WidthOverride(760)[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",24)).Text(FText::FromString(bRecallingRescue?TEXT("一起平安归来 | 世界已暂停"):TEXT("从水晶高地归来 | 世界已暂停")))]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",20)).WrapTextAt(740).Text(FText::FromString(Pet->GetCreatureName()+(bRecallingRescue?TEXT("曾在冒险中倒下，你用药救起了它。如今一起回到了家，你们在工作台边停下来，确认彼此都还在身旁。"):TEXT("在你身旁见证了水晶的采出。回到工作台边，你们停下来，一起回味这段旅程。"))))]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SButton).OnClicked_Lambda([Choice](){Choice(1);return FReply::Handled();})[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",20)).Text(FText::FromString(bRecallingRescue?TEXT("1 · 安静陪伴：别怕，我们已经回家了"):TEXT("1 · 轻声致谢：真高兴我们一起回到了家")))]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SButton).OnClicked_Lambda([Choice](){Choice(2);return FReply::Handled();})[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",20)).Text(FText::FromString(bRecallingRescue?TEXT("2 · 庆祝归来：以后也要一起平安回来！"):TEXT("2 · 一起庆祝：这是属于我们的发现！")))]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SButton).OnClicked_Lambda([Choice](){Choice(0);return FReply::Handled();})[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",18)).Text(FText::FromString(TEXT("以后再聊 / 退出键 · 暂时保留这段回忆")))]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).WrapTextAt(740).Text(FText::FromString(TEXT("共同回忆不会带来数值奖励。按1/2选择，也可用Tab和回车。关闭后按F5保存。")))]
    ]];
    Dialog=SNew(SJourneyMemoryPanel,Content,Choice);GetWorld()->GetGameViewport()->AddViewportWidgetContent(Dialog.ToSharedRef(),100);
    PC->bShowMouseCursor=true;FInputModeUIOnly Input;Input.SetWidgetToFocus(Dialog);PC->SetInputMode(Input);FSlateApplication::Get().SetKeyboardFocus(Dialog);return true;
}
bool UCompanionMemoryComponent::Commit(bool Celebrate)
{
    auto* Hero=Listener.Get();auto* PC=DialogController.Get();if(!IsOpen() || !PC || PC->GetPawn()!=Hero || !CanShareAtHome(Hero) ||
        (bRecallingRescue?RescueMemory!=ERescueJourneyMemory::Pending:Memory!=ECrystalJourneyMemory::Pending)) return false;
    if(bRecallingRescue)RescueMemory=Celebrate?ERescueJourneyMemory::Celebration:ERescueJourneyMemory::QuietThanks;
    else Memory=Celebrate?ECrystalJourneyMemory::Celebration:ECrystalJourneyMemory::QuietThanks;
    auto* Pet=CastChecked<ACreatureCharacter>(GetOwner());Pet->RespondToMemory(Celebrate);
    Hero->Feedback=Pet->GetCreatureName()+TEXT("记住了：")+(bRecallingRescue?GetRescueSummary():GetSummary())+TEXT(" | 不增加物品、亲密度或战斗属性");Close();return true;
}
void UCompanionMemoryComponent::Close()
{
    if(!Dialog.IsValid())return;
    auto* PC=DialogController.Get();
    if(PC)PC->OnPossessedPawnChanged.RemoveDynamic(this,&UCompanionMemoryComponent::HandleDialogPawnChanged);
    if(auto* Hero=Listener.Get())Hero->OnDestroyed.RemoveDynamic(this,&UCompanionMemoryComponent::HandleDialogListenerDestroyed);
    if(GetWorld() && GetWorld()->GetGameViewport())GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(Dialog.ToSharedRef());Dialog.Reset();
    // 主人可能已失去控制或销毁，仍用打开窗口时的控制器恢复输入。
    if(PC){PC->bShowMouseCursor=false;PC->SetInputMode(FInputModeGameOnly());}
    Listener.Reset();DialogController.Reset();bRecallingRescue=false;UGameplayStatics::SetGamePaused(this,false);
}
void UCompanionMemoryComponent::HandleDialogPawnChanged(APawn* Previous,APawn* Current)
{if(IsOpen() && (!Listener.IsValid() || Current!=Listener.Get()))Close();}
void UCompanionMemoryComponent::HandleDialogListenerDestroyed(AActor* DestroyedActor)
{Close();}
void UCompanionMemoryComponent::CloseAll(UWorld* World)
{if(World)for(TActorIterator<ACreatureCharacter> It(World);It;++It)It->Memory->Close();}
void UCompanionMemoryComponent::EndPlay(const EEndPlayReason::Type Reason){Close();Super::EndPlay(Reason);}
