#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureDefinition.h"
#include "Camera/CameraComponent.h"

bool ACFPlayerCharacter::HasJournalEntry(const UCreatureDefinition* Definition,bool bRequireCaptured) const
{
    if (!Definition) return false;
    for (const auto& Entry : Journal)
        if (Entry.Definition.Get()==Definition && (!bRequireCaptured || Entry.bCaptured)) return true;
    return false;
}

void ACFPlayerCharacter::RecordJournal(UCreatureDefinition* Definition,bool bCaptured)
{
    if (!HasAuthority() || !Definition) return;
    for (auto& Entry : Journal)
        if (Entry.Definition.Get()==Definition) { Entry.bCaptured |= bCaptured; return; }
    if (Journal.Num()>=128) return;
    FCreatureJournalEntry Entry; Entry.Definition=Definition; Entry.bCaptured=bCaptured;
    Journal.Add(Entry); JournalAssets.AddUnique(Definition);
    Journal.Sort([](const auto& A,const auto& B) { return A.Definition.ToSoftObjectPath().ToString()<B.Definition.ToSoftObjectPath().ToString(); });
}

bool ACFPlayerCharacter::ObserveCreature(ACreatureCharacter* Creature)
{
    if (!HasAuthority() || !GetController() || Health<=0 || !IsValid(Creature) || !Creature->Definition || Creature->Health<=0 || GetDistanceTo(Creature)>1200) return false;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(JournalSight),false,this);
    if (GetWorld()->LineTraceSingleByChannel(Hit,FirstPersonCamera->GetComponentLocation(),Creature->GetActorLocation(),ECC_Visibility,Params) && Hit.GetActor()!=Creature) return false;
    RecordJournal(Creature->Definition,Creature->Disposition==ECreatureDisposition::Tamed && Creature->Tamer==GetController());
    return true;
}

void ACFPlayerCharacter::ObserveAimedCreature() { ObserveCreature(TraceCreature()); }

void ACFPlayerCharacter::ToggleJournal()
{
    bJournalOpen=!bJournalOpen; JournalPage=0;
    if (bJournalOpen) { bRosterOpen=false; bHelpVisible=false; bBackpackOpen=false; }
}
void ACFPlayerCharacter::NextJournalPage() { if(bJournalOpen && !Journal.IsEmpty()) JournalPage=(JournalPage+1)%Journal.Num(); }
void ACFPlayerCharacter::PreviousJournalPage() { if(bJournalOpen && !Journal.IsEmpty()) JournalPage=(JournalPage+Journal.Num()-1)%Journal.Num(); }
