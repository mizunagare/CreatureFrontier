#include "ResearchComponent.h"
#include "CFPlayerCharacter.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"

bool UResearchComponent::Has(EFrontierResearch Research) const
{
    return Meets(Progress,Research);
}
bool UResearchComponent::Meets(const FResearchProgress& State,EFrontierResearch Research)
{ return Research==EFrontierResearch::None || (Research==EFrontierResearch::CrystalSample && State.bCrystalSample) || (Research==EFrontierResearch::CrystalStudy && State.bCrystalStudy); }
bool UResearchComponent::Restore(const FResearchProgress& State)
{
    if(!GetOwner() || !GetOwner()->HasAuthority() || !Validate(State)) return false;
    Progress=State; return true;
}
void UResearchComponent::RecordHarvest(const AResourceNode* Source)
{
    auto* Hero=Cast<ACFPlayerCharacter>(GetOwner());
    if(!Hero || !Hero->HasAuthority() || Hero->Health<=0 || !Source || !Source->Definition || Source->Definition->Discovery!=EFrontierResearch::CrystalSample || Progress.bCrystalSample) return;
    Progress.bCrystalSample=true; Hero->Feedback=TEXT("Crystal sample discovered! Return to base: C -> Study crystal");
}
FString UResearchComponent::GetHint() const
{
    if(Progress.bCrystalStudy) return TEXT("CRYSTAL STUDIED | C: resonance device | B/8: crystal lantern | 0: capture mode");
    if(Progress.bCrystalSample) return TEXT("CRYSTAL SAMPLE | Return to original base workbench; C -> Study crystal");
    return TEXT("EXPEDITION | Crystal Rise NE: finite crystals guarded by alpha | M gather");
}
