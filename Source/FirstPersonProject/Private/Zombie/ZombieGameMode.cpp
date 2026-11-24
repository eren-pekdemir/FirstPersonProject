#include "Zombie/ZombieGameMode.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/ShooterHUD.h"
#include "Zombie/ZombieCoordinator.h"
#include "Zombie/ZombieWaveDirector.h"

AZombieGameMode::AZombieGameMode()
{
	DirectorClass = AZombieWaveDirector::StaticClass();
	HUDClass = AShooterHUD::StaticClass();
}

void AZombieGameMode::StartPlay()
{
	Super::StartPlay();

	bool bHasDirector = false;
	for (TActorIterator<AZombieWaveDirector> It(GetWorld()); It; ++It)
	{
		bHasDirector = true;
		break;
	}

	if (!bHasDirector && DirectorClass)
	{
		GetWorld()->SpawnActor<AZombieWaveDirector>(DirectorClass, FTransform::Identity);
	}
}

void AZombieGameMode::OnPlayerDied(AController* PlayerController)
{
	if (const UZombieCoordinator* Coordinator = GetWorld()->GetSubsystem<UZombieCoordinator>())
	{
		if (AZombieWaveDirector* Director = Coordinator->GetDirector())
		{
			Director->EndGame();
		}
	}

	GetWorldTimerManager().SetTimer(RestartTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
	}), RestartDelay, false);
}
