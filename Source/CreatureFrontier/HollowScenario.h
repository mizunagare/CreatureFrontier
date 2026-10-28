#pragma once
#include "CoreMinimal.h"
class UWorld;
class ACFPlayerCharacter;
void RunHollowScenario(UWorld* World,ACFPlayerCharacter* Hero,int Mode);
void RunHollowPreview(UWorld* World,ACFPlayerCharacter* Hero);
void RunWildSightScenario(UWorld* World,ACFPlayerCharacter* Hero);
TArray<FVector> MakeHollowRoutePoints();
void RunHollowAdventure(UWorld* World,ACFPlayerCharacter* Hero,int Mode);
void RunPlayerPaceScenario(UWorld* World,ACFPlayerCharacter* Hero);
