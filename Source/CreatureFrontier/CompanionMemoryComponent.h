#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CompanionMemoryComponent.generated.h"
class ACFPlayerCharacter;
class AResourceNode;
class SWidget;
UENUM(BlueprintType)
enum class ECrystalJourneyMemory : uint8 { None, Pending, QuietThanks, Celebration };
UCLASS()
class CREATUREFRONTIER_API UCompanionMemoryComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    ECrystalJourneyMemory GetMemory() const { return Memory; }
    static bool ValidateSnapshot(ECrystalJourneyMemory State,bool OwnedTamed,bool Sampled);
    void RestoreSnapshot(ECrystalJourneyMemory State);
    FString GetSummary() const;
    bool Open(ACFPlayerCharacter* Hero);
    bool Commit(bool Celebrate);
    void Close();
    bool IsOpen() const { return Dialog.IsValid(); }
    static void CloseAll(UWorld* World);
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class AResourceNode;
    static void RecordHarvest(AResourceNode* Source,ACFPlayerCharacter* Hero,AActor* Harvester);
    bool CanRecall(ACFPlayerCharacter* Hero) const;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,meta=(AllowPrivateAccess="true")) ECrystalJourneyMemory Memory=ECrystalJourneyMemory::None;
    TWeakObjectPtr<ACFPlayerCharacter> Listener;
    TSharedPtr<SWidget> Dialog;
};
