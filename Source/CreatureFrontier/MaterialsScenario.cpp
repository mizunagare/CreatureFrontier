#include "MaterialsScenario.h"
#include "CFPlayerCharacter.h"
#include "CreatureCharacter.h"
#include "InventoryComponent.h"
#include "ResourceNode.h"
#include "ResourceDefinition.h"
#include "CraftingCatalog.h"
#include "FrontierSaveGame.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace CFMaterialsQA
{
AResourceNode* Node(UWorld* World,uint32 Id)
{ for(TActorIterator<AResourceNode> It(World);It;++It) if(It->PersistentId==FGuid(0xCF170000,0,0,Id)) return *It; return nullptr; }
bool Snapshot(UWorld* World,ACFPlayerCharacter* Hero)
{
    auto* Saved=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("MaterialsCross"),0));
    bool Pass=Saved && Saved->FormatVersion>=23 && Saved->FormatVersion<=25 && Hero->Inventory->Count(EFrontierItem::Material)==2 &&
        Hero->Inventory->Count(EFrontierItem::CaptureDevice)==2 && Hero->Inventory->Count(EFrontierItem::ResonanceDevice)==2;
    for(uint32 Id=8;Id<=10;++Id) Pass &= Node(World,Id) && Node(World,Id)->Remaining==0 &&
        Hero->Inventory->Count(static_cast<EFrontierItem>(static_cast<uint8>(EFrontierItem::Fiber)+Id-8))==1;
    return Pass && Node(World,8)->RegrowRemaining>0 && Node(World,9)->RegrowRemaining==0 && Node(World,10)->RegrowRemaining==0;
}
}
void RunMaterialsScenario(UWorld* World,ACFPlayerCharacter* Hero,int32 Mode)
{
    using namespace CFMaterialsQA;
    bool Pass=true;
    if(Mode==2) Pass=Hero->LoadProgress(TEXT("MaterialsCross")) && Snapshot(World,Hero) && Hero->LoadProgress(TEXT("MaterialsCross")) && Snapshot(World,Hero);
    else
    {
        for(TActorIterator<ACreatureCharacter> It(World);It;++It) It->Destroy();
        Hero->GetCharacterMovement()->DisableMovement();
        Hero->Inventory->Restore({{EFrontierItem::Material,2}}); // Known processing-cost fixture, not resource yield.
        auto* Catalog=UCraftingCatalog::Load(); Pass=Catalog && Catalog->Validate() && Catalog->Items.Num()==10 && Catalog->Recipes.Num()==11;
        for(uint32 Id=8;Id<=10;++Id)
        {
            auto* Source=Node(World,Id); if(!Source){Pass=false;break;}
            Source->SetActorTickEnabled(false); Source->Remaining=3; Source->HarvestCooldown=0; Source->RegrowRemaining=0;
            Hero->SetActorLocation(Source->GetActorLocation()+FVector(0,-180,90));
            Hero->FirstPersonCamera->SetWorldRotation((Source->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
            for(int32 Count=0;Count<3;++Count){Source->Tick(1); Hero->GatherAimedResource();}
            const auto Item=static_cast<EFrontierItem>(static_cast<uint8>(EFrontierItem::Fiber)+Id-8);
            Pass &= Source->Definition->OutputItem==Item && Hero->Inventory->Count(Item)==3 && Source->Remaining==0;
            const auto Before=Hero->Inventory->GetStacks(); Pass &= !Hero->CraftRecipe(TEXT("WeaveSupplies")) && Before.OrderIndependentCompareEqual(Hero->Inventory->GetStacks());
        }
        auto* Crystal=Node(World,7); if(!Crystal){Pass=false;} else
        {
            Crystal->SetActorTickEnabled(false); Crystal->Remaining=2; Crystal->HarvestCooldown=0;
            Hero->SetActorLocation(Crystal->GetActorLocation()+FVector(0,-180,90));
            Hero->FirstPersonCamera->SetWorldRotation((Crystal->GetActorLocation()-Hero->FirstPersonCamera->GetComponentLocation()).Rotation());
            for(int32 Count=0;Count<2;++Count){Crystal->Tick(1); Hero->GatherAimedResource();}
            Pass &= Hero->Inventory->Count(EFrontierItem::Crystal)==2;
        }
        Hero->SetActorLocation(FVector(-450,0,90));
        auto Before=Hero->Inventory->GetStacks();
        Pass &= !Hero->CraftRecipe(TEXT("ScrapResonator")) && Before.OrderIndependentCompareEqual(Hero->Inventory->GetStacks());
        Pass &= Hero->CraftRecipe(TEXT("WeaveSupplies")) && Hero->CraftRecipe(TEXT("OreTools")) && Hero->CraftRecipe(TEXT("StudyCrystal")) && Hero->CraftRecipe(TEXT("ScrapResonator"));
        Pass &= Hero->SaveProgress(TEXT("MaterialsCross")) && Hero->LoadProgress(TEXT("MaterialsCross")) && Snapshot(World,Hero);
        if(Mode==0)
        {
            auto* Live=Node(World,9);
            for(int32 Case=0;Case<4;++Case)
            {
                auto* Bad=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("MaterialsCross"),0));
                if(Case==0) Bad->InventoryStacks.Add(EFrontierItem::Ore,1000);
                if(Case==1) Bad->InventoryStacks.Add(EFrontierItem::Fiber,-1);
                if(Case==2) Bad->InventoryStacks.Add(EFrontierItem::Count,1);
                if(Case==3) Bad->FormatVersion=22;
                Pass &= UGameplayStatics::SaveGameToSlot(Bad,TEXT("MaterialsBad"),0) && !Hero->LoadProgress(TEXT("MaterialsBad")) && Node(World,9)==Live && Snapshot(World,Hero);
            }
            Hero->Inventory->Restore({{EFrontierItem::Fiber,2},{EFrontierItem::Material,997}});
            Before=Hero->Inventory->GetStacks(); Pass &= !Hero->CraftRecipe(TEXT("WeaveSupplies")) && Before.OrderIndependentCompareEqual(Hero->Inventory->GetStacks());
            Hero->Inventory->Consume(EFrontierItem::Material,1);
            Pass &= Hero->CraftRecipe(TEXT("WeaveSupplies")) && Hero->Inventory->Count(EFrontierItem::Material)==999 && Hero->Inventory->Count(EFrontierItem::Fiber)==0;
            Hero->Inventory->Restore({{EFrontierItem::Ore,2},{EFrontierItem::Material,0}});
            Before=Hero->Inventory->GetStacks(); Pass &= !Hero->CraftRecipe(TEXT("OreTools")) && Before.OrderIndependentCompareEqual(Hero->Inventory->GetStacks());
            Pass &= Hero->LoadProgress(TEXT("MaterialsCross"));
            auto* Legacy=Cast<UFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("MaterialsCross"),0));
            Legacy->FormatVersion=22;
            for(auto Item:{EFrontierItem::Fiber,EFrontierItem::Ore,EFrontierItem::Scrap}) Legacy->InventoryStacks.Remove(Item);
            Legacy->ResourceNodes.RemoveAll([](const FResourceSaveRecord& R){return R.Id==FGuid(0xCF170000,0,0,8) || R.Id==FGuid(0xCF170000,0,0,9) || R.Id==FGuid(0xCF170000,0,0,10);});
            Pass &= UGameplayStatics::SaveGameToSlot(Legacy,TEXT("MaterialsLegacy22"),0) && Hero->LoadProgress(TEXT("MaterialsLegacy22")) &&
                Node(World,8)->Remaining==18 && Node(World,9)->Remaining==16 && Node(World,10)->Remaining==12;
            Node(World,9)->Remaining=5;
            Pass &= Hero->SaveProgress(TEXT("MaterialsMigrated")) && Hero->LoadProgress(TEXT("MaterialsMigrated")) && Node(World,9)->Remaining==5 &&
                Hero->LoadProgress(TEXT("MaterialsMigrated")) && Node(World,9)->Remaining==5;
            Node(World,10)->Destroy(); Pass &= Hero->SaveProgress(TEXT("MaterialsDeleted")) && Hero->LoadProgress(TEXT("MaterialsDeleted")) && !Node(World,10);
            Pass &= Hero->LoadProgress(TEXT("MaterialsCross")) && Snapshot(World,Hero);
        }
    }
    UE_LOG(LogTemp,Display,TEXT("CF_MATERIALS%s %s"),Mode==1?TEXT("_WRITE"):Mode==2?TEXT("_READ"):TEXT(""),Pass?TEXT("PASS"):TEXT("FAIL"));
    if(Pass && Mode==0 && FParse::Param(FCommandLine::Get(),TEXT("CFMaterialsPreview")))
    {
        Hero->ToggleBackpack(); for(int32 Index=0;Index<7;++Index) Hero->StepBackpackRecipe(1);
        FTimerHandle Shot,Exit;
        World->GetTimerManager().SetTimer(Shot,[](){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("MaterialsPreview.png"),false,false);},1.f,false);
        World->GetTimerManager().SetTimer(Exit,[](){FPlatformMisc::RequestExitWithStatus(false,0);},3.f,false);
        return;
    }
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
