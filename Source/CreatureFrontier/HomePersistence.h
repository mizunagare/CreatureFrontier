#pragma once
#include "CoreMinimal.h"
class UFrontierSaveGame;
class AHomeModule;

class FHomeRestoreTransaction
{
public:
    explicit FHomeRestoreTransaction(UWorld* InWorld):World(InWorld) {}
    ~FHomeRestoreTransaction();
    bool Stage(UFrontierSaveGame* Save);
    AHomeModule* Find(FGuid Id) const;
    void Commit();
private:
    UWorld* World;
    TArray<AHomeModule*> Staged;
    bool bCommitted=false;
};
