#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ZombieGameMode.generated.h"

class AZombieWaveDirector;

/** Zombie survival mode: makes sure a wave director runs and restarts the level after the player dies */
UCLASS()
class FIRSTPERSONPROJECT_API AZombieGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	AZombieGameMode();

	virtual void StartPlay() override;

	/** Called by the player when he dies */
	void OnPlayerDied(AController* PlayerController);

protected:

	/** Director spawned when the level has none */
	UPROPERTY(EditDefaultsOnly, Category = "Zombies")
	TSubclassOf<AZombieWaveDirector> DirectorClass;

	UPROPERTY(EditDefaultsOnly, Category = "Zombies", meta = (Units = "s"))
	float RestartDelay = 7.f;

private:

	FTimerHandle RestartTimer;
};
