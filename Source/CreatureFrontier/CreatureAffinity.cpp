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
        if(!Known(Definition)) return TEXT("Untyped");
        return Definition->Form==ECreatureForm::Leaf?TEXT("Leaf"):Definition->Form==ECreatureForm::Ember?TEXT("Ember"):TEXT("Stone");
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
        if(!Known(Definition)) return TEXT("Untyped - normal damage");
        const TCHAR* Strong=Definition->Form==ECreatureForm::Leaf?TEXT("Stone"):Definition->Form==ECreatureForm::Stone?TEXT("Ember"):TEXT("Leaf");
        const TCHAR* Weak=Definition->Form==ECreatureForm::Leaf?TEXT("Ember"):Definition->Form==ECreatureForm::Stone?TEXT("Leaf"):TEXT("Stone");
        return FString::Printf(TEXT("%s > %s x1.5 | vs %s x0.75"),FamilyName(Definition),Strong,Weak);
    }
}
