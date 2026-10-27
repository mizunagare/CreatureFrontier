#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureAffinity.h"
#include "EngineUtils.h"
FString ACFPlayerCharacter::GetGroupMatchupText(const ACreatureCharacter* Target) const
{
    if(!IsValid(Target)) return TEXT("No target");
    int32 Strong=0,Resisted=0,Normal=0;
    for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)
    {
        if(It->Health<=0 || !IsCommandTarget(*It)) continue;
        const float Scale=CreatureAffinity::Multiplier(It->Definition,Target->Definition);
        if(Scale>1) ++Strong; else if(Scale<1) ++Resisted; else ++Normal;
    }
    return FString::Printf(TEXT("%s | group: %d strong / %d resisted / %d normal"),CreatureAffinity::FamilyName(Target->Definition),Strong,Resisted,Normal);
}
