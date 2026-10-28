#include "CreatureAffinity.h"
#include "CreatureDefinition.h"
namespace CreatureAffinity
{
    static bool Known(const UCreatureDefinition* Definition)
    {
        return Definition && (Definition->Form==ECreatureForm::Leaf || Definition->Form==ECreatureForm::Ember || Definition->Form==ECreatureForm::Stone);
    }
    const TCHAR* FamilyName(const UCreatureDefinition* Definition)
    {
        if(!Known(Definition)) return TEXT("无属性");
        return Definition->Form==ECreatureForm::Leaf?TEXT("草叶"):Definition->Form==ECreatureForm::Ember?TEXT("余烬"):TEXT("岩石");
    }
    float Multiplier(const UCreatureDefinition* Attacker,const UCreatureDefinition* Defender)
    {
        if(!Known(Attacker) || !Known(Defender) || Attacker->Form==Defender->Form) return 1;
        const bool Strong=(Attacker->Form==ECreatureForm::Leaf && Defender->Form==ECreatureForm::Stone) ||
            (Attacker->Form==ECreatureForm::Stone && Defender->Form==ECreatureForm::Ember) ||
            (Attacker->Form==ECreatureForm::Ember && Defender->Form==ECreatureForm::Leaf);
        return Strong?1.5f:.75f;
    }
    FString Describe(const UCreatureDefinition* Definition)
    {
        if(!Known(Definition)) return TEXT("无属性，伤害正常");
        const TCHAR* Strong=Definition->Form==ECreatureForm::Leaf?TEXT("岩石"):Definition->Form==ECreatureForm::Stone?TEXT("余烬"):TEXT("草叶");
        const TCHAR* Weak=Definition->Form==ECreatureForm::Leaf?TEXT("余烬"):Definition->Form==ECreatureForm::Stone?TEXT("草叶"):TEXT("岩石");
        return FString::Printf(TEXT("%s 克制 %s ×1.5 | 对 %s ×0.75"),FamilyName(Definition),Strong,Weak);
    }
}
