#pragma once
#include "CoreMinimal.h"
class UWorld;
class ACFPlayerCharacter;
void RunHomeDoorScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode=0);
void RunHomeDoorNavigationScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Rotated=false);
