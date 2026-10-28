#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CompanionMemoryComponent.generated.h"
class ACFPlayerCharacter;
class AResourceNode;
class SWidget;
class ACreatureCharacter;
class APlayerController;
class APawn;
UENUM(BlueprintType)
enum class ECrystalJourneyMemory : uint8 { None, Pending, QuietThanks, Celebration };
UENUM(BlueprintType)
enum class ERescueJourneyMemory : uint8 { None=0, Downed=1, Rescued=2, Pending=3, QuietThanks=4, Celebration=5 };
UCLASS()
class CREATUREFRONTIER_API UCompanionMemoryComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UCompanionMemoryComponent();
    ERescueJourneyMemory GetRescueMemory() const { return RescueMemory; }
    FName GetRescueSource() const { return RescueSource; }
    static bool ValidateRescueSnapshot(UWorld* World,uint8 State,FName Source,bool OwnedTamed);
    void RestoreRescueSnapshot(ERescueJourneyMemory State,FName Source);
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
    ECrystalJourneyMemory GetMemory() const { return Memory; }
    static bool ValidateSnapshot(ECrystalJourneyMemory State,bool OwnedTamed,bool Sampled);
    void RestoreSnapshot(ECrystalJourneyMemory State);
    FString GetSummary() const;
    FString GetRescueSummary() const;
    bool Open(ACFPlayerCharacter* Hero);
    bool Commit(bool Celebrate);
    void Close();
    bool IsOpen() const { return Dialog.IsValid(); }
    static void CloseAll(UWorld* World);
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class ACreatureCharacter;
    friend class ACFPlayerCharacter;
    void RecordDowned(const ACreatureCharacter* Attacker);
    void RecordRescued(ACFPlayerCharacter* Hero);
    bool CanShareAtHome(ACFPlayerCharacter* Hero) const;
    UPROPERTY() ERescueJourneyMemory RescueMemory=ERescueJourneyMemory::None;
    UPROPERTY() FName RescueSource;
    friend class AResourceNode;
    static void RecordHarvest(AResourceNode* Source,ACFPlayerCharacter* Hero,AActor* Harvester);
    bool CanRecall(ACFPlayerCharacter* Hero) const;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,meta=(AllowPrivateAccess="true")) ECrystalJourneyMemory Memory=ECrystalJourneyMemory::None;
    TWeakObjectPtr<ACFPlayerCharacter> Listener;
    TWeakObjectPtr<APlayerController> DialogController;
    UFUNCTION() void HandleDialogPawnChanged(APawn* Previous,APawn* Current);
    UFUNCTION() void HandleDialogListenerDestroyed(AActor* DestroyedActor);
    TSharedPtr<SWidget> Dialog;
    // 打开时锁定经历，防止确认时误写另一个待分享事件。
    bool bRecallingRescue=false;
};
