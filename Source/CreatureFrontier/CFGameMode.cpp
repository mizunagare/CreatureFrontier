#include "CFGameMode.h"
#include "CFPlayerCharacter.h"

ACFGameMode::ACFGameMode()
{
    DefaultPawnClass = ACFPlayerCharacter::StaticClass();
}

