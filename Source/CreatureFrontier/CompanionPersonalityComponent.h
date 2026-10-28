#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CompanionPersonalityComponent.generated.h"

UENUM(BlueprintType)
enum class ECompanionPreference : uint8 { None, Quiet, Playful };

UCLASS()
class CREATUREFRONTIER_API UCompanionPersonalityComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    static ECompanionPreference PreferenceForId(const FGuid& Id);
    static bool ValidateSnapshot(ECompanionPreference Preference,bool Discovered,bool OwnedTamed);
    void EnsurePreference();
    void RestoreSnapshot(ECompanionPreference Preference,bool Discovered);
    ECompanionPreference GetPreference() const { return Preferred; }
    bool IsDiscovered() const { return bDiscovered; }
    FString GetSummary() const;
    bool CanRespond(ECompanionPreference Style) const;
    FString Respond(ECompanionPreference Style);
private:
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,meta=(AllowPrivateAccess="true")) ECompanionPreference Preferred=ECompanionPreference::None;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,meta=(AllowPrivateAccess="true")) bool bDiscovered=false;
};
