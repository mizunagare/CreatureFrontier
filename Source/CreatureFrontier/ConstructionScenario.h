#pragma once
class UWorld;
class ACFPlayerCharacter;
void RunConstructionScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read);
void RunConstructionInputScenario(UWorld* World,ACFPlayerCharacter* Hero);
void RunConstructionHomeScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read);
void RunRenovationScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read);
void RunRenovationOccupancyScenario(UWorld* World,ACFPlayerCharacter* Hero);
void RunPlatformConstructionScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read);
void RunUpperDeckConstructionScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read);
void RunUpperDeckLifeScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read);
void RunHomeCareScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read);
void RunWildHomeTripScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read);
void RunHomeRailingScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read);
void RunStarterJourneyScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read);
void RunStarterFarmScenario(UWorld* World,ACFPlayerCharacter* Hero,bool Read);
