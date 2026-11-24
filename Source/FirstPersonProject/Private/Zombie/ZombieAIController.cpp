#include "Zombie/ZombieAIController.h"

#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "TimerManager.h"
#include "Zombie/ZombieCharacter.h"
#include "Zombie/ZombieCoordinator.h"
#include "Zombie/ZombieWaveDirector.h"
#include "GameFramework/PlayerController.h"

namespace ZombieAI
{
	constexpr float RepathDistance = 120.f;
	constexpr float RepathInterval = 0.5f;
	constexpr float StuckSpeed = 20.f;
	constexpr float StuckTime = 1.4f;
	constexpr float InvestigateLookTime = 3.f;
	constexpr float SearchTime = 8.f;
	constexpr float SurgeDuration = 1.2f;
	/** Zombies that do not see the player but track him (hunt mode) move slower */
	constexpr float UnseenSpeedScale = 0.8f;
	/** Heard noises are placed with some error, a zombie does not know exactly where a shot came from */
	constexpr float NoiseError = 250.f;

	constexpr float WakeDistance = 380.f;
	constexpr float WakeNoiseDistance = 900.f;

	constexpr float ChargeMinDistance = 450.f;
	constexpr float ChargeMaxDistance = 1300.f;
	constexpr float ChargeSpeed = 640.f;
	constexpr float ChargeDuration = 1.8f;
	constexpr float ChargeHitDistance = 170.f;
	constexpr float ChargeDamage = 30.f;
	constexpr float ChargeKnockback = 900.f;
	constexpr float ChargeCooldown = 9.f;

	constexpr float ScreamerKeepMin = 850.f;
	constexpr float ScreamerKeepMax = 1500.f;
	constexpr float ShriekRadius = 1800.f;
	constexpr float FrenzyDuration = 7.f;
	constexpr int32 ShriekCalls = 2;

	/** Stalkers freeze while looked at within this view cone (cosine) */
	constexpr float WatchedCos = 0.82f;
	constexpr float WatchedRange = 3000.f;
	constexpr float WatchedRetreatTime = 1.4f;
	constexpr float BehindDistance = 160.f;
}

AZombieAIController::AZombieAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
{
	PrimaryActorTick.bCanEverTick = true;

	UAIPerceptionComponent* Senses = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));
	SetPerceptionComponent(*Senses);

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 2600.f;
	SightConfig->LoseSightRadius = 3200.f;
	SightConfig->PeripheralVisionAngleDegrees = 75.f;
	SightConfig->SetMaxAge(5.f);
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
	Senses->ConfigureSense(*SightConfig);

	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->HearingRange = 5000.f;
	HearingConfig->SetMaxAge(4.f);
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;
	Senses->ConfigureSense(*HearingConfig);
	Senses->SetDominantSense(UAISense_Sight::StaticClass());
}

AZombieCharacter* AZombieAIController::GetZombie() const
{
	return Cast<AZombieCharacter>(GetPawn());
}

UZombieCoordinator* AZombieAIController::GetCoordinator() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UZombieCoordinator>() : nullptr;
}

bool AZombieAIController::IsAlive() const
{
	const AZombieCharacter* Zombie = GetZombie();
	return Zombie && Zombie->IsAlive() && State != EZombieState::Dead;
}

void AZombieAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	GetPerceptionComponent()->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &AZombieAIController::OnTargetPerceptionUpdated);

	if (UCrowdFollowingComponent* Crowd = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		Crowd->SetCrowdSeparation(true);
		Crowd->SetCrowdSeparationWeight(3.f);
		Crowd->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::Medium);
		Crowd->SetCrowdCollisionQueryRange(500.f);
	}

	FlankAngle = FMath::FRandRange(-75.f, 75.f);
	FlankRadius = FMath::FRandRange(250.f, 550.f);
	SpeedJitter = FMath::FRandRange(0.9f, 1.12f);
	ThinkTimer = FMath::FRandRange(0.f, ThinkInterval);

	const float Now = GetWorld()->GetTimeSeconds();
	NextGroanTime = Now + FMath::FRandRange(1.f, 6.f);
	NextSurgeTime = Now + FMath::FRandRange(3.f, 7.f);

	if (UZombieCoordinator* Coordinator = GetCoordinator())
	{
		Coordinator->Register(this);

		// In wave mode new zombies already know where to go
		if (APawn* Hunted = Coordinator->GetHuntTarget())
		{
			Target = Hunted;
			LastKnownLocation = Hunted->GetActorLocation();
			bKnowsTarget = true;
			StartChase();
			return;
		}
	}

	SetState(EZombieState::Wander);
}

void AZombieAIController::OnUnPossess()
{
	if (UZombieCoordinator* Coordinator = GetCoordinator())
	{
		Coordinator->Unregister(this);
	}
	Super::OnUnPossess();
}

void AZombieAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UZombieCoordinator* Coordinator = GetCoordinator())
	{
		Coordinator->Unregister(this);
	}
	GetWorldTimerManager().ClearTimer(AlertTimer);
	Super::EndPlay(EndPlayReason);
}

void AZombieAIController::OnPawnDied()
{
	SetState(EZombieState::Dead);
	StopMovement();
	if (UZombieCoordinator* Coordinator = GetCoordinator())
	{
		Coordinator->Unregister(this);
	}
	GetWorldTimerManager().ClearTimer(AlertTimer);
}

void AZombieAIController::SetState(EZombieState NewState)
{
	if (State == NewState)
	{
		return;
	}

	if (State == EZombieState::Chase)
	{
		if (UZombieCoordinator* Coordinator = GetCoordinator())
		{
			Coordinator->ReleaseAttackSlot(this);
		}
	}

	State = NewState;
	StateTime = 0.f;
	bMovingToTarget = false;
	LastMoveGoal = FVector(TNumericLimits<float>::Max());

	if (AZombieCharacter* Zombie = GetZombie())
	{
		const FZombieTypeSettings& Settings = Zombie->GetSettings();
		switch (NewState)
		{
		case EZombieState::Wander:
		case EZombieState::Search:
		case EZombieState::Investigate:
			Zombie->SetMoveSpeed(Settings.WanderSpeed * SpeedJitter * (NewState == EZombieState::Wander ? 1.f : 1.4f));
			break;
		case EZombieState::Chase:
			Zombie->SetMoveSpeed(Settings.ChaseSpeed * SpeedJitter);
			break;
		default:
			break;
		}
	}
}

void AZombieAIController::StartChase()
{
	SetState(EZombieState::Chase);
}

void AZombieAIController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsAlive())
	{
		return;
	}

	StateTime += DeltaTime;
	CheckStuck(DeltaTime);

	ThinkTimer -= DeltaTime;
	if (ThinkTimer <= 0.f)
	{
		ThinkTimer += ThinkInterval;
		Think();
	}
}

void AZombieAIController::UpdateKnowledge()
{
	const float Now = GetWorld()->GetTimeSeconds();

	bSeesTarget = false;
	if (APawn* Seen = Target.Get())
	{
		FActorPerceptionBlueprintInfo Info;
		if (GetPerceptionComponent()->GetActorsPerception(Seen, Info))
		{
			for (const FAIStimulus& Stimulus : Info.LastSensedStimuli)
			{
				if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>() && Stimulus.WasSuccessfullySensed() && !Stimulus.IsExpired())
				{
					bSeesTarget = true;
				}
			}
		}
	}

	// Hunt mode: the horde always smells the player
	if (UZombieCoordinator* Coordinator = GetCoordinator())
	{
		if (APawn* Hunted = Coordinator->GetHuntTarget())
		{
			Target = Hunted;
			bKnowsTarget = true;
			LastKnownLocation = Hunted->GetActorLocation();
		}
	}

	if (bSeesTarget && Target.IsValid())
	{
		LastKnownLocation = Target->GetActorLocation();
		LastSeenTime = Now;
		bKnowsTarget = true;
	}
}

void AZombieAIController::Think()
{
	AZombieCharacter* Zombie = GetZombie();
	if (!Zombie)
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();

	if (Now >= NextGroanTime)
	{
		Zombie->Groan();
		NextGroanTime = Now + (State == EZombieState::Chase ? FMath::FRandRange(2.5f, 6.f) : FMath::FRandRange(5.f, 12.f));
	}

	if (Zombie->IsStaggered())
	{
		StopMovement();
		bMovingToTarget = false;
		return;
	}

	if (Zombie->IsBusy())
	{
		return;
	}

	UpdateKnowledge();

	switch (State)
	{
	case EZombieState::Wander:		ThinkWander(); break;
	case EZombieState::Investigate:	ThinkInvestigate(); break;
	case EZombieState::Alert:		StartChase(); break;
	case EZombieState::Chase:		ThinkChase(); break;
	case EZombieState::Search:		ThinkSearch(); break;
	case EZombieState::Dormant:		ThinkDormant(); break;
	default: break;
	}
}

void AZombieAIController::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!IsAlive() || !Stimulus.WasSuccessfullySensed())
	{
		return;
	}

	APawn* SensedPawn = Cast<APawn>(Actor);
	const bool bIsPlayer = SensedPawn && SensedPawn->IsPlayerControlled();

	if (State == EZombieState::Dormant)
	{
		if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>()
			&& FVector::Dist(Stimulus.StimulusLocation, GetPawn()->GetActorLocation()) < ZombieAI::WakeNoiseDistance)
		{
			WakeUp();
		}
		return;
	}

	if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>())
	{
		if (!bIsPlayer)
		{
			return;
		}

		Target = SensedPawn;
		LastKnownLocation = SensedPawn->GetActorLocation();
		LastSeenTime = GetWorld()->GetTimeSeconds();
		bKnowsTarget = true;

		// First sight: stop, scream and wake the horde
		if (State != EZombieState::Chase && State != EZombieState::Alert)
		{
			SetState(EZombieState::Alert);
			StopMovement();
			if (AZombieCharacter* Zombie = GetZombie())
			{
				SetFocus(SensedPawn);
				Zombie->Scream();
			}
			if (UZombieCoordinator* Coordinator = GetCoordinator())
			{
				Coordinator->BroadcastAlert(GetPawn()->GetActorLocation(), SensedPawn, AlertRadius, this);
			}
		}
		return;
	}

	if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>())
	{
		// A gunshot: go and look, unless already busy with the player
		if (State == EZombieState::Wander || State == EZombieState::Search || State == EZombieState::Investigate)
		{
			NoiseLocation = Stimulus.StimulusLocation + FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f) * ZombieAI::NoiseError;
			SetState(EZombieState::Investigate);
		}
	}
}

void AZombieAIController::HearAlert(APawn* InTarget, const FVector& Location, float Delay)
{
	if (!IsAlive() || State == EZombieState::Chase || State == EZombieState::Alert || State == EZombieState::Dormant)
	{
		return;
	}

	TWeakObjectPtr<APawn> WeakTarget = InTarget;
	GetWorldTimerManager().SetTimer(AlertTimer, FTimerDelegate::CreateWeakLambda(this, [this, WeakTarget, Location]()
	{
		if (!IsAlive() || State == EZombieState::Chase)
		{
			return;
		}

		Target = WeakTarget;
		bKnowsTarget = WeakTarget.IsValid();
		LastKnownLocation = WeakTarget.IsValid() ? WeakTarget->GetActorLocation() : Location;
		LastSeenTime = GetWorld()->GetTimeSeconds();

		// Some answer the call with their own scream
		if (FMath::FRand() < 0.35f)
		{
			if (AZombieCharacter* Zombie = GetZombie())
			{
				SetState(EZombieState::Alert);
				StopMovement();
				Zombie->Scream();
				return;
			}
		}
		StartChase();
	}), FMath::Max(Delay, 0.01f), false);
}

void AZombieAIController::OnDamaged(APawn* Attacker)
{
	if (!IsAlive() || !Attacker)
	{
		return;
	}

	Target = Attacker;
	LastKnownLocation = Attacker->GetActorLocation();
	LastSeenTime = GetWorld()->GetTimeSeconds();
	bKnowsTarget = true;

	if (State == EZombieState::Dormant)
	{
		WakeUp();
		return;
	}

	if (State != EZombieState::Chase)
	{
		StartChase();
		if (UZombieCoordinator* Coordinator = GetCoordinator())
		{
			Coordinator->BroadcastAlert(GetPawn()->GetActorLocation(), Attacker, AlertRadius * 0.5f, this);
		}
	}
}

// -------------------------------------------------------------------------------------------------
// States

void AZombieAIController::ThinkWander()
{
	if (bKnowsTarget && Target.IsValid())
	{
		StartChase();
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (IsMoving() || Now < WanderWaitUntil)
	{
		return;
	}

	// Shamble to a random spot, stand there a moment, pick another
	FVector Point;
	if (GetRandomReachablePoint(GetPawn()->GetActorLocation(), WanderRadius, Point))
	{
		MoveTo(Point, 60.f);
	}
	WanderWaitUntil = Now + FMath::FRandRange(3.f, 8.f);
}

void AZombieAIController::ThinkInvestigate()
{
	if (bSeesTarget)
	{
		StartChase();
		return;
	}

	const float Distance = FVector::Dist2D(GetPawn()->GetActorLocation(), NoiseLocation);
	if (Distance > 120.f)
	{
		MoveTo(NoiseLocation, 80.f);
		return;
	}

	// Arrived: look around, then give up
	StopMovement();
	if (StateTime > ZombieAI::InvestigateLookTime + 10.f)
	{
		SetState(EZombieState::Wander);
	}
}

void AZombieAIController::ThinkSearch()
{
	if (bSeesTarget)
	{
		StartChase();
		return;
	}

	if (StateTime > ZombieAI::SearchTime)
	{
		bKnowsTarget = false;
		SetState(EZombieState::Wander);
		return;
	}

	if (!IsMoving())
	{
		FVector Point;
		if (FVector::Dist2D(GetPawn()->GetActorLocation(), LastKnownLocation) > 150.f)
		{
			MoveTo(LastKnownLocation, 80.f);
		}
		else if (GetRandomReachablePoint(LastKnownLocation, 500.f, Point))
		{
			MoveTo(Point, 60.f);
		}
	}
}

void AZombieAIController::ThinkChase()
{
	AZombieCharacter* Zombie = GetZombie();
	APawn* Victim = Target.Get();
	UZombieCoordinator* Coordinator = GetCoordinator();
	if (!Zombie || !Victim || !Coordinator)
	{
		SetState(EZombieState::Search);
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	const bool bHunting = Coordinator->GetHuntTarget() == Victim;

	if (!bSeesTarget && !bHunting && Now - LastSeenTime > LoseTargetTime)
	{
		ClearFocus(EAIFocusPriority::Gameplay);
		SetState(EZombieState::Search);
		return;
	}

	const FZombieTypeSettings& Settings = Zombie->GetSettings();
	const FVector TargetLocation = bSeesTarget || bHunting ? Victim->GetActorLocation() : LastKnownLocation;
	const float Distance = FVector::Dist2D(Zombie->GetActorLocation(), TargetLocation);
	const float Reach = AttackRange * Zombie->GetActorScale3D().X;

	// Type tactics
	switch (Zombie->GetZombieType())
	{
	case EZombieType::Brute:
		if (ThinkBruteCharge(*Zombie, *Victim, Distance)) { return; }
		break;
	case EZombieType::Screamer:
		if (ThinkScreamer(*Zombie, *Victim, Distance)) { return; }
		break;
	case EZombieType::Stalker:
		if (ThinkStalker(*Zombie, *Victim, Distance)) { return; }
		break;
	default:
		break;
	}

	// Speed: lunges when close, surges now and then, slower when only following the scent
	float Speed = Settings.ChaseSpeed * SpeedJitter;
	if (Distance < LungeDistance && bSeesTarget)
	{
		Speed = Settings.LungeSpeed * SpeedJitter;
	}
	else if (Now < SurgeUntil)
	{
		Speed = FMath::Max(Speed, Settings.LungeSpeed * 0.8f);
	}
	else if (Now >= NextSurgeTime)
	{
		SurgeUntil = Now + ZombieAI::SurgeDuration;
		NextSurgeTime = Now + FMath::FRandRange(4.f, 9.f);
	}
	if (!bSeesTarget)
	{
		Speed *= ZombieAI::UnseenSpeedScale;
	}
	Zombie->SetMoveSpeed(Speed);

	// In reach: attack if a slot is free, otherwise wait on the ring around the player
	if (Distance <= Reach && bSeesTarget)
	{
		SetFocus(Victim);
		if (Coordinator->RequestAttackSlot(this))
		{
			StopMovement();
			bMovingToTarget = false;
			Zombie->StartAttack(Victim);
		}
		return;
	}

	if (Distance <= WaitRingRadius * 1.3f && !Coordinator->HasAttackSlot(this) && !Coordinator->RequestAttackSlot(this))
	{
		// Circle slowly around the player waiting for an opening
		SetFocus(Victim);
		Zombie->SetMoveSpeed(Settings.WanderSpeed);
		const FVector Side = (Zombie->GetActorLocation() - TargetLocation).GetSafeNormal2D().RotateAngleAxis(FlankAngle > 0.f ? 25.f : -25.f, FVector::UpVector);
		MoveTo(TargetLocation + Side * WaitRingRadius, 30.f);
		return;
	}

	if (Distance > Reach * 1.6f)
	{
		Coordinator->ReleaseAttackSlot(this);
	}

	if (Distance > FlankDistance)
	{
		ClearFocus(EAIFocusPriority::Gameplay);
		MoveTo(GetApproachPoint(TargetLocation, Distance), 80.f);
	}
	else if (bSeesTarget || bHunting)
	{
		SetFocus(Victim);
		MoveToTarget();
	}
	else
	{
		MoveTo(LastKnownLocation, 60.f);
	}
}

// -------------------------------------------------------------------------------------------------
// Movement

FVector AZombieAIController::GetApproachPoint(const FVector& TargetLocation, float Distance) const
{
	const FVector FromTarget = (GetPawn()->GetActorLocation() - TargetLocation).GetSafeNormal2D();
	const FVector Side = FromTarget.RotateAngleAxis(FlankAngle, FVector::UpVector);
	const FVector Point = TargetLocation + Side * FMath::Min(FlankRadius, Distance * 0.5f);

	const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	if (NavSystem && NavSystem->ProjectPointToNavigation(Point, Projected, FVector(200.f, 200.f, 300.f)))
	{
		return Projected.Location;
	}
	return TargetLocation;
}

bool AZombieAIController::GetRandomReachablePoint(const FVector& Origin, float Radius, FVector& OutPoint) const
{
	const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Result;
	if (NavSystem && NavSystem->GetRandomReachablePointInRadius(Origin, Radius, Result))
	{
		OutPoint = Result.Location;
		return true;
	}
	return false;
}

bool AZombieAIController::IsMoving() const
{
	const UPathFollowingComponent* PathFollowing = GetPathFollowingComponent();
	return PathFollowing && PathFollowing->GetStatus() == EPathFollowingStatus::Moving;
}

void AZombieAIController::MoveTo(const FVector& Goal, float Acceptance)
{
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bGoalMoved = FVector::DistSquared(Goal, LastMoveGoal) > FMath::Square(ZombieAI::RepathDistance);
	if (!bGoalMoved && IsMoving() && !bMovingToTarget)
	{
		return;
	}
	if (!bGoalMoved && Now - LastMoveRequestTime < ZombieAI::RepathInterval && IsMoving())
	{
		return;
	}

	LastMoveGoal = Goal;
	LastMoveRequestTime = Now;
	bMovingToTarget = false;
	MoveToLocation(Goal, Acceptance, true, true, true, false, nullptr, true);
}

void AZombieAIController::MoveToTarget()
{
	if (bMovingToTarget && IsMoving())
	{
		return;
	}

	if (APawn* Victim = Target.Get())
	{
		bMovingToTarget = true;
		LastMoveGoal = FVector(TNumericLimits<float>::Max());
		MoveToActor(Victim, AttackRange * 0.5f, true, true, false, nullptr, true);
	}
}

void AZombieAIController::CheckStuck(float DeltaTime)
{
	const APawn* Zombie = GetPawn();
	if (!Zombie || !IsMoving() || Zombie->GetVelocity().Size2D() > ZombieAI::StuckSpeed)
	{
		StuckTime = 0.f;
		return;
	}

	StuckTime += DeltaTime;
	if (StuckTime < ZombieAI::StuckTime)
	{
		return;
	}

	// Blocked (by others or the level): step aside to a nearby point, the next think resumes the chase
	StuckTime = 0.f;
	FVector Point;
	if (GetRandomReachablePoint(Zombie->GetActorLocation(), 300.f, Point))
	{
		bMovingToTarget = false;
		LastMoveGoal = Point;
		LastMoveRequestTime = GetWorld()->GetTimeSeconds();
		MoveToLocation(Point, 30.f, true, true, true, false, nullptr, true);
	}
}

// -------------------------------------------------------------------------------------------------
// Playing dead

void AZombieAIController::MakeDormant()
{
	if (AZombieCharacter* Zombie = GetZombie())
	{
		StopMovement();
		Zombie->SetDormant();
		SetState(EZombieState::Dormant);
	}
}

void AZombieAIController::ThinkDormant()
{
	// Gets up when the player walks close by
	const APawn* Player = Target.IsValid() ? Target.Get() : nullptr;
	if (!Player)
	{
		if (const UZombieCoordinator* Coordinator = GetCoordinator())
		{
			Player = Coordinator->GetHuntTarget();
		}
	}

	if (Player && FVector::Dist(Player->GetActorLocation(), GetPawn()->GetActorLocation()) < ZombieAI::WakeDistance)
	{
		Target = const_cast<APawn*>(Player);
		WakeUp();
	}
}

void AZombieAIController::WakeUp()
{
	AZombieCharacter* Zombie = GetZombie();
	if (!Zombie || State != EZombieState::Dormant)
	{
		return;
	}

	Zombie->WakeUp();
	bKnowsTarget = Target.IsValid();
	if (Target.IsValid())
	{
		LastKnownLocation = Target->GetActorLocation();
		LastSeenTime = GetWorld()->GetTimeSeconds();
	}
	StartChase();
}

// -------------------------------------------------------------------------------------------------
// Type tactics

bool AZombieAIController::IsWatchedBy(const APawn& Victim) const
{
	const APlayerController* PlayerController = Victim.GetController<APlayerController>();
	if (!PlayerController)
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector Chest = GetPawn()->GetActorLocation() + FVector(0.f, 0.f, 40.f);
	const FVector ToZombie = Chest - ViewLocation;
	if (ToZombie.Size() > ZombieAI::WatchedRange || FVector::DotProduct(ViewRotation.Vector(), ToZombie.GetSafeNormal()) < ZombieAI::WatchedCos)
	{
		return false;
	}

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(StalkerWatched), false, &Victim);
	Params.AddIgnoredActor(GetPawn());
	return !GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, Chest, ECC_Visibility, Params);
}

bool AZombieAIController::ThinkBruteCharge(AZombieCharacter& Zombie, APawn& Victim, float Distance)
{
	const float Now = GetWorld()->GetTimeSeconds();

	if (bCharging)
	{
		// Rams through: hits whatever stands near its path, then has to catch its breath
		const float ToVictim = FVector::Dist2D(Zombie.GetActorLocation(), Victim.GetActorLocation());
		if (ToVictim < ZombieAI::ChargeHitDistance)
		{
			Zombie.HitTarget(&Victim, ZombieAI::ChargeDamage, ZombieAI::ChargeKnockback);
			bCharging = false;
		}
		else if (Now >= ChargeEndTime || FVector::Dist2D(Zombie.GetActorLocation(), ChargeTarget) < 80.f)
		{
			bCharging = false;
		}

		if (!bCharging)
		{
			StopMovement();
			Zombie.SetMoveSpeed(Zombie.GetSettings().WanderSpeed);
			NextChargeTime = Now + ZombieAI::ChargeCooldown;
		}
		return true;
	}

	if (Now < NextChargeTime || !bSeesTarget || Distance < ZombieAI::ChargeMinDistance || Distance > ZombieAI::ChargeMaxDistance)
	{
		return false;
	}

	// Roars, lowers its head and charges in a straight line past where the player stands
	bCharging = true;
	ChargeEndTime = Now + ZombieAI::ChargeDuration;
	const FVector Direction = (Victim.GetActorLocation() - Zombie.GetActorLocation()).GetSafeNormal2D();
	ChargeTarget = Victim.GetActorLocation() + Direction * 300.f;
	Zombie.Scream();
	Zombie.SetMoveSpeed(ZombieAI::ChargeSpeed);
	bMovingToTarget = false;
	LastMoveGoal = ChargeTarget;
	MoveToLocation(ChargeTarget, 40.f, false, true, true, false, nullptr, true);
	return true;
}

bool AZombieAIController::ThinkScreamer(AZombieCharacter& Zombie, APawn& Victim, float Distance)
{
	const float Now = GetWorld()->GetTimeSeconds();
	UZombieCoordinator* Coordinator = GetCoordinator();

	// Shrieks when it sees the player: the horde around it goes wild and more come
	if (bSeesTarget && Now >= NextShriekTime && Distance < ZombieAI::ScreamerKeepMax * 1.4f)
	{
		NextShriekTime = Now + FMath::FRandRange(9.f, 14.f);
		StopMovement();
		SetFocus(&Victim);
		Zombie.Scream();
		if (Coordinator)
		{
			Coordinator->BroadcastFrenzy(Zombie.GetActorLocation(), ZombieAI::ShriekRadius, ZombieAI::FrenzyDuration);
			Coordinator->BroadcastAlert(Zombie.GetActorLocation(), &Victim, ZombieAI::ShriekRadius, this);
			if (AZombieWaveDirector* Director = Coordinator->GetDirector())
			{
				Director->AddBonusZombies(ZombieAI::ShriekCalls);
			}
		}
		return true;
	}

	// Cornered: fights
	if (Distance < AttackRange * 1.2f)
	{
		return false;
	}

	// Keeps away from the player, in the edge of the light
	SetFocus(&Victim);
	const FVector Away = (Zombie.GetActorLocation() - Victim.GetActorLocation()).GetSafeNormal2D();
	if (Distance < ZombieAI::ScreamerKeepMin)
	{
		Zombie.SetMoveSpeed(Zombie.GetSettings().ChaseSpeed);
		MoveTo(Victim.GetActorLocation() + Away.RotateAngleAxis(FlankAngle * 0.5f, FVector::UpVector) * ZombieAI::ScreamerKeepMax, 80.f);
	}
	else if (Distance > ZombieAI::ScreamerKeepMax)
	{
		Zombie.SetMoveSpeed(Zombie.GetSettings().ChaseSpeed * 0.7f);
		MoveTo(GetApproachPoint(Victim.GetActorLocation(), Distance), 80.f);
	}
	else
	{
		// Paces sideways
		Zombie.SetMoveSpeed(Zombie.GetSettings().WanderSpeed);
		MoveTo(Victim.GetActorLocation() + Away.RotateAngleAxis(FlankAngle > 0.f ? 20.f : -20.f, FVector::UpVector) * Distance, 60.f);
	}
	return true;
}

bool AZombieAIController::ThinkStalker(AZombieCharacter& Zombie, APawn& Victim, float Distance)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (UZombieCoordinator* Coordinator = GetCoordinator())
	{
		Coordinator->ReportStalkerDistance(Distance);
	}

	// Backing off into the dark after being seen
	if (Now < RetreatUntil)
	{
		return true;
	}

	const bool bWatched = IsWatchedBy(Victim);
	if (bWatched && Distance > AttackRange * 1.5f)
	{
		// Freezes the moment it is looked at, and slips away if stared at
		StopMovement();
		bMovingToTarget = false;
		SetFocus(&Victim);
		WatchedTime += ThinkInterval;
		if (WatchedTime > ZombieAI::WatchedRetreatTime)
		{
			WatchedTime = 0.f;
			FVector Hide;
			const FVector Away = (Zombie.GetActorLocation() - Victim.GetActorLocation()).GetSafeNormal2D();
			if (GetRandomReachablePoint(Zombie.GetActorLocation() + Away * 700.f, 400.f, Hide))
			{
				Zombie.SetMoveSpeed(Zombie.GetSettings().ChaseSpeed);
				LastMoveGoal = Hide;
				MoveToLocation(Hide, 60.f, false, true, true, false, nullptr, true);
				RetreatUntil = Now + 2.5f;
			}
		}
		return true;
	}

	WatchedTime = FMath::Max(0.f, WatchedTime - ThinkInterval);

	// Close behind the player: strikes, ignoring the attack slots
	if (Distance <= AttackRange * Zombie.GetActorScale3D().X)
	{
		SetFocus(&Victim);
		StopMovement();
		Zombie.StartAttack(&Victim);
		return true;
	}

	// Not looked at: rushes to a spot right behind the player
	ClearFocus(EAIFocusPriority::Gameplay);
	Zombie.SetMoveSpeed(Zombie.GetSettings().ChaseSpeed);
	const FVector Behind = Victim.GetActorLocation() - Victim.GetActorForwardVector() * ZombieAI::BehindDistance;
	MoveTo(Distance > 600.f ? Behind : Victim.GetActorLocation(), 40.f);
	return true;
}
