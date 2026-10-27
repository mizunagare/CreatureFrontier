#pragma once
#include "CoreMinimal.h"
class AResourceNode;
class UFrontierSaveGame;

// Failure at ANY later load step must leave the live resource ledger intact.
class FResourceRestoreTransaction
{
public:
    explicit FResourceRestoreTransaction(UWorld* InWorld):World(InWorld) {}
    ~FResourceRestoreTransaction();
    bool Stage(const UFrontierSaveGame* Save);
    void Commit();
    AResourceNode* Find(const FGuid& Id) const;
private:
    UWorld* World;
    TArray<AResourceNode*> Staged;
    bool bCommitted=false;
};
