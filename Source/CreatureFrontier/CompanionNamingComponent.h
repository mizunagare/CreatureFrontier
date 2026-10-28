#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CompanionNamingComponent.generated.h"
class ACreatureCharacter;
class SWidget;
class SEditableTextBox;
UCLASS()
class CREATUREFRONTIER_API UCompanionNamingComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    static bool IsValidName(const FString& Name);
    bool RenameCompanion(ACreatureCharacter* Pet,const FString& Input);
    bool Open(ACreatureCharacter* Pet);
    void OpenAimed();
    void Close();
    bool IsOpen() const { return Dialog.IsValid(); }
    bool Commit(const FString& Input);
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    bool CanRename(ACreatureCharacter* Pet) const;
    TWeakObjectPtr<ACreatureCharacter> Target;
    TSharedPtr<SWidget> Dialog;
    TSharedPtr<SEditableTextBox> Entry;
};
