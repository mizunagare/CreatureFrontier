#pragma once
class UWorld;
class ACFPlayerCharacter;
// 继续已有家园的真实资源扩建，不创建替代房壳或伙伴。
void RunHomeExpansionScenario(UWorld* World,ACFPlayerCharacter* Hero,bool PlanOnly);
