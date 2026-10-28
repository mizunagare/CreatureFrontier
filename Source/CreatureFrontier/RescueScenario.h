#pragma once
class UWorld;
class ACFPlayerCharacter;
void RunRescueGateScenario(UWorld* World, ACFPlayerCharacter* Hero);
void RunNaturalRescueCheckpoint(UWorld* World, ACFPlayerCharacter* Hero);
void RunNaturalRescueResume(UWorld* World, ACFPlayerCharacter* Hero, bool ReadFinal=false);
