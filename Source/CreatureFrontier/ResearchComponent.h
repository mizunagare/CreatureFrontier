#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FrontierItems.h"
#include "ResearchComponent.generated.h"
class AResourceNode;

UCLASS()
class CREATUREFRONTIER_API UResearchComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    bool Has(EFrontierResearch Research) const;
    static bool Meets(const FResearchProgress& State,EFrontierResearch Research);
    const FResearchProgress& GetProgress() const { return Progress; }
    static bool Validate(const FResearchProgress& State) { return !State.bCrystalStudy || State.bCrystalSample; }
    bool Restore(const FResearchProgress& State);
    FString GetHint() const;
private:
    friend class ACFPlayerCharacter;
    friend class AResourceNode;
    void RecordHarvest(const AResourceNode* Source);
    void CompleteCrystalStudy() { Progress.bCrystalStudy=true; }
    UPROPERTY() FResearchProgress Progress;
};
