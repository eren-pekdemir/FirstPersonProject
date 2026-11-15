// Copyright Epic Games, Inc. All Rights Reserved.

#include "FirstPersonProjectGameMode.h"

#include "UI/ShooterHUD.h"

AFirstPersonProjectGameMode::AFirstPersonProjectGameMode()
{
	HUDClass = AShooterHUD::StaticClass();
}
