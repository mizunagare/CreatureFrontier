#pragma once
#include "CoreMinimal.h"
class UWorld;
class ACFPlayerCharacter;
class AHomeModule;
void RunHomeDoorScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode=0);
void RunHomeDoorNavigationScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Rotated=false);
void RunHomeDoorPlayerScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Rotated=false);
void RunHomeDoorFarmScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode=0);
namespace CFDoorNavigationQA
{
    FVector Point(FVector Position,float Yaw);
    FVector Original(FVector Position,float Yaw);
    AHomeModule* Room(UWorld* World,ACFPlayerCharacter* Hero,float Orientation);
}
