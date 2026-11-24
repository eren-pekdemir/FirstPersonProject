#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "ZombieAIController.generated.h"

class AZombieCharacter;
class UAISenseConfig_Hearing;
class UAISenseConfig_Sight;
class UZombieCoordinator;

UENUM(BlueprintType)
enum class EZombieState : uint8
{
	/** Lies on the ground like a corpse until the player comes close */
	Dormant,
	Wander,
	/** Walks to a noise it heard */
	Investigate,
	/** Spotted the player: screams and calls the others */
	Alert,
	Chase,
	/** Lost the player: searches around where he was last seen */
	Search,
	Dead
};

/**
 *  Zombie brain.
 *  Senses: sight (cone in front) and hearing (gunshots, other zombies' screams). Remembers where the player
 *  was last seen and searches there when it loses him.
 *  Horde tactics: zombies approach on spread out flanking paths to surround the player, only a few
 *  attack at a time (attack slots from UZombieCoordinator) while the others circle and wait, they lunge
 *  when close, and crowd avoidance keeps them from stacking up. Stuck zombies find a way around.
 */
UCLASS()
class FIRSTPERSONPROJECT_API AZombieAIController : public AAIController
{
	GENERATED_BODY()

public:

	AZombieAIController(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaTime) override;

	/** Called by the zombie when it is hurt: it turns on its attacker */
	void OnDamaged(APawn* Attacker);

	void OnPawnDied();

	/** Another zombie screamed nearby */
	void HearAlert(APawn* Target, const FVector& Location, float Delay);

	bool IsAlive() const;

	/** Starts lying on the ground until woken */
	void MakeDormant();

	EZombieState GetState() const { return State; }

protected:

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, Category = "Zombie")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY(VisibleAnywhere, Category = "Zombie")
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	/** Seconds between two decisions */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie", meta = (Units = "s"))
	float ThinkInterval = 0.15f;

	/** Distance at which it starts its attack */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie", meta = (Units = "cm"))
	float AttackRange = 140.f;

	/** Distance it keeps while waiting for an attack slot */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie", meta = (Units = "cm"))
	float WaitRingRadius = 230.f;

	/** Beyond this distance it takes its flanking path instead of coming straight */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie", meta = (Units = "cm"))
	float FlankDistance = 800.f;

	/** Distance at which it lunges */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie", meta = (Units = "cm"))
	float LungeDistance = 420.f;

	/** Radius in which its scream alerts other zombies */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie", meta = (Units = "cm"))
	float AlertRadius = 2200.f;

	/** Seconds without seeing the player before it gives up the chase (when not hunting) */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie", meta = (Units = "s"))
	float LoseTargetTime = 7.f;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie", meta = (Units = "cm"))
	float WanderRadius = 900.f;

private:

	void Think();
	void UpdateKnowledge();

	void ThinkWander();
	void ThinkInvestigate();
	void ThinkChase();
	void ThinkSearch();
	void ThinkDormant();
	void WakeUp();

	/** Type tactics inside the chase. Return true when they took over this think. */
	bool ThinkBruteCharge(AZombieCharacter& Zombie, APawn& Victim, float Distance);
	bool ThinkScreamer(AZombieCharacter& Zombie, APawn& Victim, float Distance);
	bool ThinkStalker(AZombieCharacter& Zombie, APawn& Victim, float Distance);

	/** True when the player looks toward this zombie with nothing in between */
	bool IsWatchedBy(const APawn& Victim) const;

	void SetState(EZombieState NewState);
	void StartChase();

	/** Moves toward Goal, only asking for a new path when the goal moved enough */
	void MoveTo(const FVector& Goal, float Acceptance = 40.f);
	void MoveToTarget();
	bool IsMoving() const;
	void CheckStuck(float DeltaTime);

	/** Where to go to reach the target from our side: spreads the horde around him */
	FVector GetApproachPoint(const FVector& TargetLocation, float Distance) const;

	bool GetRandomReachablePoint(const FVector& Origin, float Radius, FVector& OutPoint) const;

	UFUNCTION()
	void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	AZombieCharacter* GetZombie() const;
	UZombieCoordinator* GetCoordinator() const;

	EZombieState State = EZombieState::Wander;
	float StateTime = 0.f;
	float ThinkTimer = 0.f;

	TWeakObjectPtr<APawn> Target;
	FVector LastKnownLocation = FVector::ZeroVector;
	float LastSeenTime = -100.f;
	bool bSeesTarget = false;
	bool bKnowsTarget = false;

	FVector NoiseLocation = FVector::ZeroVector;
	FVector LastMoveGoal = FVector(TNumericLimits<float>::Max());
	bool bMovingToTarget = false;
	float LastMoveRequestTime = -100.f;

	/** Personal approach angle and distance, so every zombie comes from its own side */
	float FlankAngle = 0.f;
	float FlankRadius = 400.f;
	float SpeedJitter = 1.f;

	float NextGroanTime = 0.f;
	float NextSurgeTime = 0.f;
	float SurgeUntil = 0.f;
	float WanderWaitUntil = 0.f;
	float StuckTime = 0.f;

	FTimerHandle AlertTimer;

	// Brute charge
	bool bCharging = false;
	float ChargeEndTime = 0.f;
	float NextChargeTime = 0.f;
	FVector ChargeTarget = FVector::ZeroVector;

	// Screamer
	float NextShriekTime = 0.f;

	// Stalker
	float WatchedTime = 0.f;
	float RetreatUntil = 0.f;
};
