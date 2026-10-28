#include "FarmCropComponent.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "WorkStation.h"
#include "FrontierSaveGame.h"
#include "InventoryComponent.h"
#include "BaseBuildingComponent.h"
#include "Kismet/GameplayStatics.h"

int32 UFarmCropComponent::Stage() const
{ return !bPlanted?-1:GrowthWork>=RequiredWork?3:GrowthWork>=RequiredWork*2/3?2:GrowthWork>=RequiredWork/3?1:0; }
FString UFarmCropComponent::Status() const
{
    const TCHAR* State=Stage()<0?TEXT("空地"):Stage()==0?TEXT("幼苗"):Stage()==1?TEXT("生长中"):Stage()==2?TEXT("开花中"):TEXT("已成熟");
    return FString::Printf(TEXT("%s %.1f/6 | 种子 %d/30\nG 播种/照料/收获 | F2 存入种子 F3 取出种子"),State,GrowthWork,Seeds);
}
void UFarmCropComponent::Refresh()
{ if(auto* Site=Cast<AWorkStation>(GetOwner())) Site->RefreshFarm(); }
bool UFarmCropComponent::Sow()
{
    if(bPlanted || Seeds<=0) return false;
    --Seeds; bPlanted=true; GrowthWork=0; Refresh(); return true;
}
bool UFarmCropComponent::Harvest()
{
    auto* Site=Cast<AWorkStation>(GetOwner());
    if(!Site || !bPlanted || GrowthWork<RequiredWork || !Site->AdjustFoodStock(1)) return false;
    bPlanted=false; GrowthWork=0; Refresh(); return true;
}
bool UFarmCropComponent::Interact(ACFPlayerCharacter* Hero,int32 Mode)
{
    auto* Site=Cast<AWorkStation>(GetOwner());
    if(!Site || !Site->HasAuthority() || Site->IsActorBeingDestroyed() || Site->RequiredSpecialty!=EWorkSpecialty::Farming ||
        !Hero || Hero->Health<=0 || Hero->GetWorld()!=GetWorld() || UGameplayStatics::IsGamePaused(this) || Hero->Building->IsActive() ||
        Hero->IsBackpackOpen() || Hero->IsRosterOpen() || Hero->IsJournalOpen() || Hero->IsHelpVisible() || Hero->GetDistanceTo(Site)>300) return false;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(CropInteract),false,Hero);
    if(GetWorld()->LineTraceSingleByChannel(Hit,Hero->GetPawnViewLocation(),Site->GetActorLocation()+FVector(0,0,30),ECC_Visibility,Params) && Hit.GetActor()!=Site) return false;
    bool Done=false;
    if(Mode==1 && Seeds<30 && Hero->Inventory->Consume(EFrontierItem::Seeds,1)) {++Seeds;Done=true;}
    else if(Mode==2 && Seeds>0 && Hero->Inventory->AddItem(EFrontierItem::Seeds,1)) {--Seeds;Done=true;}
    else if(Mode==0)
    {
        if(!bPlanted)
        {
            if(Seeds==0 && Hero->Inventory->Consume(EFrontierItem::Seeds,1)) ++Seeds;
            Done=Sow();
        }
        else if(GrowthWork>=RequiredWork) Done=Harvest();
        else if(GetWorld()->GetTimeSeconds()>=NextManualWork)
        { GrowthWork=FMath::Min(RequiredWork,GrowthWork+1); NextManualWork=GetWorld()->GetTimeSeconds()+1;Done=true; }
    }
    if(Done) Refresh();
    Hero->Feedback=Done?Status():TEXT("农圃未改变：请检查种子、容量或1秒照料冷却；O 取出收获的食物");
    return Done;
}
bool UFarmCropComponent::Work(ACreatureCharacter* Pet,ACFPlayerCharacter* Hero,float Amount)
{
    auto* Site=Cast<AWorkStation>(GetOwner());
    if(!Site || !Site->HasAuthority() || Site->IsActorBeingDestroyed() || Site->RequiredSpecialty!=EWorkSpecialty::Farming ||
        !Hero || Hero->Health<=0 || !Pet || Pet->Health<=0 || Pet->Tamer!=Hero->GetController() || Pet->Disposition!=ECreatureDisposition::Tamed ||
        Pet->Activity!=ECreatureActivity::Work || Pet->WorkSite!=Site || Pet->GetDistanceTo(Site)>220 || !FMath::IsFinite(Amount) || Amount<0 ||
        UGameplayStatics::IsGamePaused(this)) return false;
    FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(CropWork),false,Pet);
    if(GetWorld()->LineTraceSingleByChannel(Hit,Pet->GetActorLocation(),Site->GetActorLocation()+FVector(0,0,30),ECC_Visibility,Params) && Hit.GetActor()!=Site) return false;
    if(Amount==0) return Harvest(); // A previously ripe crop can settle after capacity is freed; never grows/sows here.
    if(!bPlanted && !Sow()) return false;
    const int32 Before=Stage(); GrowthWork=FMath::Min(RequiredWork,GrowthWork+Amount);
    if(Stage()!=Before) Refresh();
    return Harvest();
}
bool UFarmCropComponent::Validate(const FFacilitySaveRecord& Record)
{
    return Record.Seeds>=0 && Record.Seeds<=30 && FMath::IsFinite(Record.GrowthWork) && Record.GrowthWork>=0 && Record.GrowthWork<=RequiredWork &&
        (Record.bPlanted || Record.GrowthWork==0) && (Record.Specialty==EWorkSpecialty::Farming || (!Record.bPlanted && Record.Seeds==0 && Record.GrowthWork==0));
}
