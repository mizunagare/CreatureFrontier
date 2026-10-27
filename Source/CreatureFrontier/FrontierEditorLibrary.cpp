#include "FrontierEditorLibrary.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationData.h"
#include "EngineUtils.h"
#if WITH_EDITOR
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#endif

bool UFrontierEditorLibrary::PreparePrototypeNavigation(UWorld* World)
{
#if WITH_EDITOR
    if (!World || World->IsGameWorld()) return false;
    ANavMeshBoundsVolume* Volume = nullptr;
    for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It) { Volume = *It; break; }
    if (!Volume) Volume = World->SpawnActor<ANavMeshBoundsVolume>();
    if (!Volume) return false;
    Volume->SetActorLocation(FVector(0, 0, 200));
    auto* Builder = NewObject<UCubeBuilder>();
    Builder->X = 3900; Builder->Y = 3900; Builder->Z = 1000;
    UActorFactory::CreateBrushForVolumeActor(Volume, Builder);
    Volume->SetActorLabel(TEXT("Prototype Navigation Bounds"));
    Volume->MarkPackageDirty();
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!Nav) return false;
    FlushAsyncLoading();
    Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
    Nav->OnNavigationBoundsUpdated(Volume);
    Nav->OnWorldInitDone(FNavigationSystemRunMode::EditorMode);
    Nav->Build();
    for (TActorIterator<ANavigationData> It(World); It; ++It)
    {
        It->EnsureBuildCompletion();
        It->MarkPackageDirty();
    }
    return Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) != nullptr;
#else
    return false;
#endif
}
