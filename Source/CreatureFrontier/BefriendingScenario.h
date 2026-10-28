#pragma once
class UWorld;
class ACFPlayerCharacter;
void RunBefriendingScenario(UWorld* World,ACFPlayerCharacter* Hero,int Mode);
void RunBefriendingPreview(UWorld* World,ACFPlayerCharacter* Hero);
void RunCompanionDefenseScenario(UWorld* World,ACFPlayerCharacter* Hero,int Mode);
void RunCompanionStanceScenario(UWorld* World,ACFPlayerCharacter* Hero);
void RunCompanionRetreatScenario(UWorld* World,ACFPlayerCharacter* Hero);
