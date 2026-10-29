#include "CompanionNamingComponent.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "BaseBuildingComponent.h"
#include "StorageInteractionComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

bool UCompanionNamingComponent::IsValidName(const FString& Name)
{
    if(Name.Len()>24 || Name!=Name.TrimStartAndEnd()) return false;
    for(int32 Index=0;Index<Name.Len();++Index)
    {
        const uint32 C=Name[Index];
        if(C<32 || (C>=127 && C<=159) || (C>=0x200B && C<=0x200F) || (C>=0x2028 && C<=0x202E) ||
            (C>=0x2060 && C<=0x206F) || C==0xFEFF) return false;
        if(C>=0xD800 && C<=0xDBFF)
        {if(++Index>=Name.Len() || Name[Index]<0xDC00 || Name[Index]>0xDFFF) return false;}
        else if(C>=0xDC00 && C<=0xDFFF) return false;
    }
    return true;
}
bool UCompanionNamingComponent::CanRename(ACreatureCharacter* Pet) const
{
    const auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());
    if(!Hero || !Hero->HasAuthority() || Hero->Health<=0 || !IsValid(Pet) || Pet->IsActorBeingDestroyed() || Pet->GetWorld()!=GetWorld() ||
        Pet->Health<=0 || Pet->Disposition!=ECreatureDisposition::Tamed || Pet->Tamer!=Hero->GetController() || Hero->GetDistanceTo(Pet)>250 ||
        (Pet->Activity!=ECreatureActivity::Follow && Pet->Activity!=ECreatureActivity::Rest) || Pet->FocusTarget || Pet->SpecialWindupRemaining>0) return false;
    for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)
        if(It->Disposition!=ECreatureDisposition::Tamed && It->Health>0 && Pet->GetDistanceTo(*It)<600) return false;
    FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionNameSight),false,Hero);
    return !GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetPawnViewLocation(),Pet->GetActorLocation(),ECC_Visibility,Params) || Hit.GetActor()==Pet;
}
bool UCompanionNamingComponent::RenameCompanion(ACreatureCharacter* Pet,const FString& Input)
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());const FString Name=Input.TrimStartAndEnd();
    if(!CanRename(Pet) || !IsValidName(Name) || (UGameplayStatics::IsGamePaused(this) && (!IsOpen() || Target.Get()!=Pet)))
    {if(Hero) Hero->Feedback=TEXT("命名未改变：需在2.5米内瞄准自己的安全空闲伙伴；限单行、24个UTF-16单元");return false;}
    if(Pet->Nickname!=Name) {Pet->Nickname=Name;Pet->AcknowledgeName();}
    Hero->Feedback=FString::Printf(TEXT("%s | %s | 保留个体身份，命名不增加属性或亲密度"),*Pet->GetCreatureName(),*Pet->GetSpeciesName());
    return true;
}
bool UCompanionNamingComponent::Open(ACreatureCharacter* Pet)
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());auto* PC=Hero?Cast<APlayerController>(Hero->GetController()):nullptr;
    if(IsOpen() || !CanRename(Pet) || !PC || !GetWorld()->GetGameViewport() || !FSlateApplication::IsInitialized() ||
        UGameplayStatics::IsGamePaused(this) || Hero->Building->IsActive() || Hero->Storage->IsOpen() || Hero->IsBackpackOpen() || Hero->IsJournalOpen() || Hero->IsHelpVisible()) return false;
    if(!UGameplayStatics::SetGamePaused(this,true)) return false;
    Hero->Feedback=TEXT("回车确认，退出键取消。留空恢复物种名。关闭后按 F5 保存。");
    if(Hero->IsRosterOpen()) Hero->ToggleRoster();
    Target=Pet;Hero->CancelMeleeAttack();Hero->GetCharacterMovement()->StopMovementImmediately();Hero->ConsumeMovementInputVector();Hero->StopJumping();
    auto CommitEntry=[this](){if(Entry.IsValid()) Commit(Entry->GetText().ToString());return FReply::Handled();};
    // 显式绘制深色背景，避免中文标题在明亮场景上失去对比。
    Dialog=SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(.015f,.025f,.04f,.97f)).ForegroundColor(FLinearColor::White)
        .HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(24)
    [SNew(SBox).WidthOverride(760)
        [SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",24)).Text(FText::FromString(TEXT("给伙伴命名 | 世界已暂停")))]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",18)).Text(FText::FromString(Pet->GetSpeciesName()+TEXT(" | 留空恢复物种名 | 限24个UTF-16单元")))]
            +SVerticalBox::Slot().AutoHeight().Padding(8)
            [SAssignNew(Entry,SEditableTextBox).Font(FCoreStyle::GetDefaultFontStyle("Regular",22)).Text(FText::FromString(Pet->Nickname)).SelectAllTextWhenFocused(true)
                .OnTextCommitted_Lambda([this](const FText& Text,ETextCommit::Type Type){if(Type==ETextCommit::OnEnter) Commit(Text.ToString());})
                .OnKeyDownHandler_Lambda([this](const FGeometry&,const FKeyEvent& Event){if(Event.GetKey()==EKeys::Escape){Close();return FReply::Handled();}return FReply::Unhandled();})]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SButton).OnClicked_Lambda(CommitEntry)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",18)).Text(FText::FromString(TEXT("确认 / 回车键")))]]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SButton).OnClicked_Lambda([this](){Close();return FReply::Handled();})[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",18)).Text(FText::FromString(TEXT("取消 / 退出键")))]]
            +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",16)).Text_Lambda([Hero](){return FText::FromString(Hero->Feedback);}).WrapTextAt(740)]
        ]
    ];
    GetWorld()->GetGameViewport()->AddViewportWidgetContent(Dialog.ToSharedRef(),100);
    PC->bShowMouseCursor=true;FInputModeUIOnly Input;Input.SetWidgetToFocus(Entry);PC->SetInputMode(Input);
    FSlateApplication::Get().SetKeyboardFocus(Entry);return true;
}
void UCompanionNamingComponent::OpenAimed()
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());if(!Hero) return;
    auto* Pet=Hero->IsRosterOpen()?Hero->GetRosterHighlight():Hero->TraceCreature();
    if(!Open(Pet)) Hero->Feedback=TEXT("F4：先关闭其他面板，再于2.5米内瞄准自己的安全空闲伙伴");
}
bool UCompanionNamingComponent::Commit(const FString& Input)
{if(!IsOpen() || !RenameCompanion(Target.Get(),Input)) return false;Close();return true;}
void UCompanionNamingComponent::Close()
{
    if(!Dialog.IsValid()) return;
    if(GetWorld() && GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(Dialog.ToSharedRef());
    Entry.Reset();Dialog.Reset();Target.Reset();
    if(auto* Hero=Cast<ACFPlayerCharacter>(GetOwner())) if(auto* PC=Cast<APlayerController>(Hero->GetController()))
    {PC->bShowMouseCursor=false;PC->SetInputMode(FInputModeGameOnly());}
    UGameplayStatics::SetGamePaused(this,false);
}
void UCompanionNamingComponent::EndPlay(const EEndPlayReason::Type Reason) {Close();Super::EndPlay(Reason);}
