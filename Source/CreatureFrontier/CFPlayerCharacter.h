#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CreatureJournalTypes.h"
#include "ObjectiveTypes.h"
#include "CFPlayerCharacter.generated.h"

class UCameraComponent;
class UCaptureComponent;
class ACreatureCharacter;
class UInventoryComponent;
class UBaseBuildingComponent;
class AWorkStation;
class UCreatureDefinition;
class UPlayerEquipmentComponent;
enum class EFrontierItem : uint8;

UCLASS()
class CREATUREFRONTIER_API ACFPlayerCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ACFPlayerCharacter();
    virtual void BeginPlay() override;
    virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer) override;
    UPROPERTY(BlueprintReadOnly, Category="Player")
    float Health = 100.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Inventory")
    TObjectPtr<UInventoryComponent> Inventory;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Equipment") TObjectPtr<UPlayerEquipmentComponent> Equipment;
    void CraftSpear();
    void CraftVest();
    void ToggleSpear();
    void ToggleVest();
    float GetPlayerAttackDamage() const;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building") TObjectPtr<UBaseBuildingComponent> Building;
    void ToggleBuilding();
    void CycleBuilding();
    void DismantleBuilding();
    void CraftCaptureDevice();
    void CraftMedicine();
    void CraftFood();
    void FeedAimedCompanion();
    bool FeedCompanion(ACreatureCharacter* Pet);
    bool PetCompanion(ACreatureCharacter* Pet);
    void PetAimedCompanion();
    AWorkStation* TraceFacility() const;
    void DepositTroughFood();
    void WithdrawTroughFood();
    bool WithdrawCompanionCargo(ACreatureCharacter* Pet);
    void WithdrawAimedCargo();
    void UseMedicine();
    bool IsNearWorkStation() const;
    FString Feedback;
    ACreatureCharacter* TraceCreature() const;
    class AResourceNode* TraceResource() const;
    void GatherAimedResource();
    bool SelectCompanion(ACreatureCharacter* Pet);
    void SelectAimedCompanion();
    void ClearCompanionSelection();
    bool IsCommandTarget(const ACreatureCharacter* Pet) const;
    FString GetCommandScopeText() const;
    FString GetGroupMatchupText(const ACreatureCharacter* Target) const;
    void ObserveAimedCreature();
    bool ObserveCreature(ACreatureCharacter* Creature);
    bool HasJournalEntry(const UCreatureDefinition* Definition,bool bRequireCaptured=false) const;
    const TArray<FCreatureJournalEntry>& GetJournal() const { return Journal; }
    void ToggleJournal();
    bool IsJournalOpen() const { return bJournalOpen; }
    int32 GetJournalPage() const { return JournalPage; }
    void NextJournalPage();
    void PreviousJournalPage();
    void RecordCompanionProduction(const ACreatureCharacter* Pet,EFrontierItem Item);
    void RecordAlphaResolution(const ACreatureCharacter* Creature);
    void ClaimObjectiveReward();
    const FFrontierObjectiveProgress& GetObjectives() const { return Objectives; }
    int32 GetObjectiveProgress(int32 Index) const;
    FString GetObjectiveStatus() const;
    FString GetObjectiveHint() const;
    FString GetObjectiveRewardText() const;
    TArray<ACreatureCharacter*> GetCompanionRoster() const;
    void ToggleRoster();
    void NextRosterCompanion();
    void PreviousRosterCompanion();
    void SelectRosterCompanion();
    bool IsRosterOpen() const { return bRosterOpen; }
    ACreatureCharacter* GetRosterHighlight() const { return RosterHighlight.Get(); }
    void CommandFollow();
    void CommandWork();
    void CommandRest();
    void HealCompanion();
    void CommandFocus();
    void CommandSpecial();
    void RestartLevel();
    bool RespawnAtBase();
    void RequestRespawn() { RespawnAtBase(); }
    float GetRespawnProtectionRemaining() const;
    void QuitGame();
    bool SaveProgress(const FString& Slot = TEXT("FrontierSlot"));
    bool LoadProgress(const FString& Slot = TEXT("FrontierSlot"));
    void QuickSave();
    void QuickLoad();
    void TogglePauseMenu();
    void PauseMenuNext();
    void PauseMenuPrevious();
    void ConfirmInterfaceAction();
    void ToggleHelp();
    void ToggleBackpack();
    void StepBackpackRecipe(int32 Direction);
    void CraftBackpackRecipe();
    bool IsBackpackOpen() const { return bBackpackOpen; }
    int32 GetBackpackRecipe() const { return BackpackRecipe; }
    int32 GetBackpackRecipeCost(int32 Index) const;
    FString GetBackpackRecipeStatus(int32 Index) const;
    bool IsPauseMenuOpen() const { return bPauseMenu; }
    bool IsPauseConfirmationPending() const { return bPauseConfirmation; }
    int32 GetPauseSelection() const { return PauseSelection; }
    bool IsHelpVisible() const { return bHelpVisible; }
    UFUNCTION()
    void CaptureResolved(ACreatureCharacter* Creature, bool bSucceeded);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera")
    TObjectPtr<UCameraComponent> FirstPersonCamera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Creature")
    TObjectPtr<UCaptureComponent> CaptureComponent;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat", meta=(ClampMin="0.0"))
    float AttackDamage = 25.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat", meta=(ClampMin="100.0"))
    float InteractionRange = 1200.0f;

    UFUNCTION(BlueprintCallable, Category="Combat")
    void Attack();
    void CancelMeleeAttack();
    bool IsMeleePending() const { return bMeleePending; }
    float GetPlayerMeleeRange() const;

    UFUNCTION(BlueprintCallable, Category="Creature")
    void CaptureTarget();

protected:
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
    FString GetQuickSaveSlot() const;
    bool ClosePauseMenu();
    bool bPauseMenu=false;
    bool bPauseConfirmation=false;
    bool bHelpVisible=false;
    bool bBackpackOpen=false;
    int32 BackpackRecipe=0;
    int32 PauseSelection=0;
    void ResolveMeleeStrike();
    FTimerHandle MeleeTimer;
    bool bMeleePending=false;
    bool bPendingSpear=false;
    float PendingMeleeDamage=0;
    float PendingMeleeRange=0;
    void InitializeRespawnAnchor();
    bool FindRespawnLocation(FVector& Location) const;
    FTransform RespawnAnchor;
    double RespawnProtectionUntil = 0;
    UPROPERTY() FFrontierObjectiveProgress Objectives;
    void RecordJournal(UCreatureDefinition* Definition,bool bCaptured);
    UPROPERTY() TArray<FCreatureJournalEntry> Journal;
    UPROPERTY() TArray<TObjectPtr<UCreatureDefinition>> JournalAssets;
    FTimerHandle ObservationTimer;
    bool bJournalOpen = false;
    int32 JournalPage = 0;
    void StepRoster(int32 Direction);
    bool bRosterOpen = false;
    TWeakObjectPtr<ACreatureCharacter> RosterHighlight;
    TWeakObjectPtr<ACreatureCharacter> SelectedCompanion;
    bool bSingleCompanionMode = false;
    void MoveForward(float Value);
    void MoveRight(float Value);
    double NextAttackTime = 0.0;
    double NextFeedTime = 0;
};
