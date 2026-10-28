#pragma once
#include "CoreMinimal.h"
class AResourceNode;
class UFrontierSaveGame;

// 后续任意读档步骤失败，都必须完整保留当前资源账本。
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
