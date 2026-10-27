#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FrontierEditorLibrary.generated.h"

UCLASS()
class CREATUREFRONTIER_API UFrontierEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Frontier|Editor")
    static bool PreparePrototypeNavigation(UWorld* World);
};
