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
    Progress.bCrystalSample=true; Hero->Feedback=TEXT("发现水晶样本！回基地按C，选择研究水晶");
}
FString UResearchComponent::GetHint() const
{
    if(Progress.bCrystalStudy) return TEXT("已研究水晶 | C 共鸣捕捉器 | B/8 水晶灯 | 0 捕捉模式");
    if(Progress.bCrystalSample) return TEXT("水晶样本 | 回初始基地工作台，按C研究水晶");
    return TEXT("远征 | 东北水晶高地 / 南侧余烬洞窟 | M 采集，返家加工");
}
