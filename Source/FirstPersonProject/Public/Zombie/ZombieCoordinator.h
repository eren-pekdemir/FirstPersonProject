#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ZombieCoordinator.generated.h"

class AZombieAIController;
class AZombieWaveDirector;
class APawn;

/**
 *  Shared knowledge of the horde.
 *  Hands out attack slots so only a few zombies swing at the player at once while the rest surround him,
 *  spreads alerts (a screaming zombie wakes the ones around it) and keeps the player the director hunts.
 */
UCLASS()
class FIRSTPERSONPROJECT_API UZombieCoordinator : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	void Register(AZombieAIController* Zombie);
	void Unregister(AZombieAIController* Zombie);

	/** True when Zombie may attack now. Keep calling while in range; the slot is held until released. */
	bool RequestAttackSlot(AZombieAIController* Zombie);
	void ReleaseAttackSlot(AZombieAIController* Zombie);
	bool HasAttackSlot(const AZombieAIController* Zombie) const;

	/** Wakes the zombies within Radius of Location and points them at Target */
	void BroadcastAlert(const FVector& Location, APawn* Target, float Radius, const AZombieAIController* Source);

	/** Player every zombie tracks even without seeing him (wave mode). Null to turn off. */
	void SetHuntTarget(APawn* Target) { HuntTarget = Target; }
	APawn* GetHuntTarget() const { return HuntTarget.Get(); }

	void SetDirector(AZombieWaveDirector* InDirector) { Director = InDirector; }
	AZombieWaveDirector* GetDirector() const { return Director.Get(); }

	int32 GetZombieCount() const { return Zombies.Num(); }

	/** A screamer's call: zombies around Location get faster for Duration */
	void BroadcastFrenzy(const FVector& Location, float Radius, float Duration);

	/** Stalkers report how close they are, the player's flashlight reacts to it */
	void ReportStalkerDistance(float Distance);

	/** Distance of the nearest stalker reported lately, or a huge value */
	float GetStalkerDistance() const;

	/** Counts living zombies of a type (to cap screamers and stalkers) */
	int32 CountType(uint8 Type) const;

	/** How many zombies may attack the player at the same time */
	int32 MaxAttackers = 3;

private:

	void Prune();

	TArray<TWeakObjectPtr<AZombieAIController>> Zombies;
	TArray<TWeakObjectPtr<AZombieAIController>> Attackers;
	TWeakObjectPtr<APawn> HuntTarget;
	TWeakObjectPtr<AZombieWaveDirector> Director;

	float StalkerDistance = TNumericLimits<float>::Max();
	float StalkerReportTime = -100.f;
};
