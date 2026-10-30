#pragma once
class UWorld;
class ACFPlayerCharacter;
void RunFarmWaitingScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Full,bool Read);
void RunFarmingScenario(UWorld* World, ACFPlayerCharacter* Hero);
void RunFarmPreview(UWorld* World, ACFPlayerCharacter* Hero);
