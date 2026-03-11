// Copyright Epic Games, Inc. All Rights Reserved.

#include "TSPCaptureGameMode.h"
#include "TSPCaptureCharacter.h"
#include "UObject/ConstructorHelpers.h"

ATSPCaptureGameMode::ATSPCaptureGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
