#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "CreatureAffinity.h"
#include "EngineUtils.h"
FString ACFPlayerCharacter::GetGroupMatchupText(const ACreatureCharacter* Target) const
{
    if(!IsValid(Target)) return TEXT("没有目标");
    int32 Strong=0,Resisted=0,Normal=0;
    for(TActorIterator<ACreatureCharacter> It(GetWorld());It;++It)
    {
        if(It->Health<=0 || !IsCommandTarget(*It)) continue;
        const float Scale=CreatureAffinity::Multiplier(It->Definition,Target->Definition);
        if(Scale>1) ++Strong; else if(Scale<1) ++Resisted; else ++Normal;
    }
    return FString::Printf(TEXT("%s | 队伍：%d克制 / %d被抵抗 / %d正常"),CreatureAffinity::FamilyName(Target->Definition),Strong,Resisted,Normal);
}
