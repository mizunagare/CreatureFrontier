#pragma once
#include "CoreMinimal.h"
class UWorld;
class ACFPlayerCharacter;
class AHomeModule;
class ACreatureCharacter;
void RunHomeDoorScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode=0);
void RunHomeDoorNavigationScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Rotated=false);
void RunHomeWindowNavigationScenario(UWorld* World,ACFPlayerCharacter* Hero);
void RunHomeWindowSkillScenario(UWorld* World,ACFPlayerCharacter* Hero);
void RunHomeDoorPlayerScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Rotated=false);
void RunHomeDoorFarmScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode=0);
namespace CFDoorNavigationQA
{
    FVector Point(FVector Position,float Yaw);
    FVector Original(FVector Position,float Yaw);
    AHomeModule* Room(UWorld* World,ACFPlayerCharacter* Hero,float Orientation);
    bool PathInside(UWorld* World,ACreatureCharacter* Pet,float Yaw);
}
