#include "Zombie/ZombieWaveDirector.h"

#include "Components/AudioComponent.h"
#include "Components/LightComponent.h"
#include "Zombie/ZombieAIController.h"
#include "Data/WeaponDataAsset.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "FirstPersonProject.h"
#include "GameFramework/PlayerController.h"
#include "Gun/GunBase.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Player/PlayerCharacter.h"
#include "Zombie/ZombieCoordinator.h"

namespace WaveDirector
{
	constexpr float MusicFadeTime = 2.5f;
}

AZombieWaveDirector::AZombieWaveDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	ZombieClass = AZombieCharacter::StaticClass();
	PickupClass = AZombiePickup::StaticClass();

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	MusicComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("Music"));
	MusicComponent->SetupAttachment(Root);
	MusicComponent->bAutoActivate = false;
	MusicComponent->bIsUISound = true;

	AmbienceComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("Ambience"));
	AmbienceComponent->SetupAttachment(Root);
	AmbienceComponent->bAutoActivate = false;
	AmbienceComponent->bIsUISound = true;
}

void AZombieWaveDirector::BeginPlay()
{
	Super::BeginPlay();

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(SpawnPointTag))
		{
			SpawnPoints.Add(*It);
		}
	}

	// Level lights (lampposts, spots) for flickering scares, never the player's flashlight
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (Cast<APawn>(*It))
		{
			continue;
		}
		TArray<ULightComponent*> Lights;
		It->GetComponents<ULightComponent>(Lights);
		for (ULightComponent* Light : Lights)
		{
			if (Light && Light->GetLightType() != LightType_Directional && Light->Intensity > 0.f)
			{
				LevelLights.Add(Light);
			}
		}
	}
	NextScareTime = GetWorld()->GetTimeSeconds() + FMath::FRandRange(ScareInterval.X, ScareInterval.Y);

	if (SpawnPoints.IsEmpty())
	{
		UE_LOG(LogFirstPersonProject, Warning, TEXT("ZombieWaveDirector: no actors tagged %s, zombies spawn around the player"), *SpawnPointTag.ToString());
	}

	if (UZombieCoordinator* Coordinator = GetWorld()->GetSubsystem<UZombieCoordinator>())
	{
		Coordinator->SetDirector(this);
	}

	if (Ambience)
	{
		AmbienceComponent->SetSound(Ambience);
		AmbienceComponent->FadeIn(4.f, 0.6f);
	}

	Phase = EWavePhase::Prepare;
	PhaseEndTime = GetWorld()->GetTimeSeconds() + PrepareTime;

#if !UE_BUILD_SHIPPING
	// Testing: -ZombieStartWave=5 begins at that wave
	int32 StartWave = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("ZombieStartWave="), StartWave) && StartWave > 1)
	{
		Wave = StartWave - 1;
	}

	// Testing: -ZombieShowcase lines up one zombie of each type in front of the player, standing still
	if (FParse::Param(FCommandLine::Get(), TEXT("ZombieShowcase")))
	{
		PhaseEndTime = TNumericLimits<float>::Max();
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const APawn* Player = GetPlayerPawn();
			if (!Player)
			{
				return;
			}
			const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
			const EZombieType Types[] = { EZombieType::Walker, EZombieType::Runner, EZombieType::Brute, EZombieType::Crawler, EZombieType::Screamer, EZombieType::Stalker, EZombieType::Walker };
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Types); ++Index)
			{
				const float Angle = -36.f + 12.f * Index;
				FVector Location = Player->GetActorLocation() + Forward.RotateAngleAxis(Angle, FVector::UpVector) * 420.f;
				FHitResult Hit;
				if (GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0.f, 0.f, 300.f), Location - FVector(0.f, 0.f, 600.f), ECC_Visibility))
				{
					Location = Hit.ImpactPoint + FVector(0.f, 0.f, 95.f);
				}
				const FTransform SpawnTransform((Player->GetActorLocation() - Location).GetSafeNormal2D().Rotation(), Location);
				AZombieCharacter* Zombie = GetWorld()->SpawnActorDeferred<AZombieCharacter>(ZombieClass, SpawnTransform, this, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Zombie)
				{
					continue;
				}
				Zombie->FinishSpawning(SpawnTransform);
				Zombie->SetupType(Types[Index]);
				if (Index == UE_ARRAY_COUNT(Types) - 1)
				{
					Zombie->SetDormant();
				}
				Zombie->DetachFromControllerPendingDestroy();
			}
		}), 1.f, false);
	}

	// Testing: -ZombieSupplies lays out the supplies of a wave end, one of each item in front of the player and an ammo box under him
	if (FParse::Param(FCommandLine::Get(), TEXT("ZombieSupplies")))
	{
		PhaseEndTime = TNumericLimits<float>::Max();
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const APawn* Player = GetPlayerPawn();
			if (!Player)
			{
				return;
			}
			SpawnSupplies();
			const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
			const FVector Right = Player->GetActorRightVector().GetSafeNormal2D();
			const EZombiePickupType Types[] = { EZombiePickupType::Ammo, EZombiePickupType::Health, EZombiePickupType::Battery };
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Types); ++Index)
			{
				SpawnPickup(Types[Index], Player->GetActorLocation() + Forward * 260.f + Right * (Index - 1) * 70.f, 0.f);
			}
			SpawnPickup(EZombiePickupType::Ammo, Player->GetActorLocation(), 0.f);
		}), 1.f, false);
	}
#endif
	PlayMusic(CalmMusic);
}

APawn* AZombieWaveDirector::GetPlayerPawn() const
{
	const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	return PlayerController ? PlayerController->GetPawn() : nullptr;
}

void AZombieWaveDirector::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const float Now = GetWorld()->GetTimeSeconds();

	if (Phase != EWavePhase::GameOver)
	{
		UpdateScares(DeltaTime);
	}

	switch (Phase)
	{
	case EWavePhase::Prepare:
	case EWavePhase::Intermission:
		if (Now >= PhaseEndTime)
		{
			StartWave();
		}
		break;

	case EWavePhase::Active:
	{
		AliveZombies.RemoveAll([](const AZombieCharacter* Zombie) { return !IsValid(Zombie) || !Zombie->IsAlive(); });

		const int32 MaxAlive = FMath::Min(FirstWaveMaxAlive + MaxAliveGrowth * (Wave - 1), MaxAliveCap);
		if (ToSpawn > 0 && AliveZombies.Num() < MaxAlive && Now >= NextSpawnTime)
		{
			SpawnZombie();
			// Later waves spawn faster
			const float Speedup = FMath::Clamp(1.f - (Wave - 1) * 0.06f, 0.45f, 1.f);
			NextSpawnTime = Now + FMath::FRandRange(SpawnInterval.X, SpawnInterval.Y) * Speedup;
		}

		if (ToSpawn <= 0 && AliveZombies.IsEmpty())
		{
			EndWave();
		}
		break;
	}

	default:
		break;
	}
}

void AZombieWaveDirector::StartWave()
{
	++Wave;
	Phase = EWavePhase::Active;
	ToSpawn = FirstWaveCount + ExtraPerWave * (Wave - 1);
	NextSpawnTime = GetWorld()->GetTimeSeconds() + 1.f;

	// The horde knows where the player is: waves come for him
	if (UZombieCoordinator* Coordinator = GetWorld()->GetSubsystem<UZombieCoordinator>())
	{
		Coordinator->SetHuntTarget(GetPlayerPawn());
		Coordinator->MaxAttackers = FMath::Min(2 + Wave / 3, 5);
	}

	if (WaveStartSound)
	{
		UGameplayStatics::PlaySound2D(this, WaveStartSound);
	}
	PlayMusic(CombatMusic);
	SpawnDormantZombies();

	UE_LOG(LogFirstPersonProject, Display, TEXT("ZombieWaveDirector: wave %d, %d zombies"), Wave, ToSpawn);
}

void AZombieWaveDirector::EndWave()
{
	Phase = EWavePhase::Intermission;
	PhaseEndTime = GetWorld()->GetTimeSeconds() + IntermissionTime;

	if (WaveEndSound)
	{
		UGameplayStatics::PlaySound2D(this, WaveEndSound);
	}
	PlayMusic(CalmMusic);
	SpawnSupplies();
}

void AZombieWaveDirector::EndGame()
{
	if (Phase == EWavePhase::GameOver)
	{
		return;
	}

	Phase = EWavePhase::GameOver;
	ToSpawn = 0;
	if (UZombieCoordinator* Coordinator = GetWorld()->GetSubsystem<UZombieCoordinator>())
	{
		Coordinator->SetHuntTarget(nullptr);
	}
	MusicComponent->FadeOut(WaveDirector::MusicFadeTime, 0.f);
}

void AZombieWaveDirector::SpawnSupplies() const
{
	const APawn* Player = GetPlayerPawn();
	const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Player || !NavSystem || !PickupClass)
	{
		return;
	}

	TArray<EZombiePickupType> Supplies;
	Supplies.Init(EZombiePickupType::Ammo, FMath::Max(AmmoSuppliesPerWave, 0));
	for (int32 Index = 0; Index < HealthSuppliesPerWave; ++Index)
	{
		Supplies.Add(EZombiePickupType::Health);
	}
	for (int32 Index = 0; Index < BatterySuppliesPerWave; ++Index)
	{
		Supplies.Add(EZombiePickupType::Battery);
	}

	// Scattered around the player on reachable ground, not on top of each other
	const FVector PlayerLocation = Player->GetActorLocation();
	TArray<FVector> Placed;
	for (const EZombiePickupType SupplyType : Supplies)
	{
		for (int32 Attempt = 0; Attempt < 16; ++Attempt)
		{
			FNavLocation Point;
			if (!NavSystem->GetRandomReachablePointInRadius(PlayerLocation, SupplyDistance.Y, Point))
			{
				continue;
			}
			const float Distance = FVector::Dist2D(Point.Location, PlayerLocation);
			const bool bCrowded = Placed.ContainsByPredicate([&Point](const FVector& Other) { return FVector::Dist2D(Other, Point.Location) < 250.f; });
			if (Distance < SupplyDistance.X || (bCrowded && Attempt < 12))
			{
				continue;
			}
			if (SpawnPickup(SupplyType, Point.Location, SupplyLifeSpan))
			{
				Placed.Add(Point.Location);
			}
			break;
		}
	}
	UE_LOG(LogFirstPersonProject, Display, TEXT("ZombieWaveDirector: %d supplies left after wave %d"), Placed.Num(), Wave);
}

AZombiePickup* AZombieWaveDirector::SpawnPickup(EZombiePickupType SupplyType, const FVector& Location, float LifeSpan) const
{
	// Put it on the ground below the point
	FVector Ground = Location;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ZombiePickupGround), false);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0.f, 0.f, 150.f), Location - FVector(0.f, 0.f, 400.f), ECC_WorldStatic, Params))
	{
		Ground = Hit.ImpactPoint;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AZombiePickup* Pickup = GetWorld()->SpawnActor<AZombiePickup>(PickupClass, Ground, FRotator::ZeroRotator, SpawnParams);
	if (Pickup)
	{
		Pickup->SetPickupType(SupplyType);
		Pickup->SetLifeSpan(LifeSpan);
	}
	return Pickup;
}

void AZombieWaveDirector::DropLoot(const AZombieCharacter& Zombie) const
{
	if (!PickupClass)
	{
		return;
	}

	const float Roll = FMath::FRand();
	EZombiePickupType DropType;
	if (Roll < AmmoDropChance)
	{
		DropType = EZombiePickupType::Ammo;
	}
	else if (Roll < AmmoDropChance + HealthDropChance)
	{
		DropType = EZombiePickupType::Health;
	}
	else if (Roll < AmmoDropChance + HealthDropChance + BatteryDropChance)
	{
		DropType = EZombiePickupType::Battery;
	}
	else
	{
		return;
	}

	UE_LOG(LogFirstPersonProject, Verbose, TEXT("ZombieDrop type=%d"), static_cast<int32>(DropType));
	SpawnPickup(DropType, Zombie.GetActorLocation() + FVector(FMath::FRandRange(-40.f, 40.f), FMath::FRandRange(-40.f, 40.f), 0.f), DropLifeSpan);
}

EZombieType AZombieWaveDirector::PickType() const
{
	const UZombieCoordinator* Coordinator = GetWorld()->GetSubsystem<UZombieCoordinator>();
	auto Count = [Coordinator](EZombieType InType) { return Coordinator ? Coordinator->CountType(static_cast<uint8>(InType)) : 0; };

	// Specials are rare and capped so each one is an event
	if (Wave >= StalkerFirstWave && Count(EZombieType::Stalker) < 1 + (Wave >= 8 ? 1 : 0) && FMath::FRand() < StalkerChance)
	{
		return EZombieType::Stalker;
	}
	if (Wave >= ScreamerFirstWave && Count(EZombieType::Screamer) < 1 + (Wave >= 7 ? 1 : 0) && FMath::FRand() < ScreamerChance)
	{
		return EZombieType::Screamer;
	}
	if (Wave >= BruteFirstWave && Count(EZombieType::Brute) < 2 + Wave / 4 && FMath::FRand() < BruteChance)
	{
		return EZombieType::Brute;
	}
	if (Wave >= CrawlerFirstWave && FMath::FRand() < CrawlerChance)
	{
		return EZombieType::Crawler;
	}

	const float RunnerChance = FMath::Min(RunnerChancePerWave * (Wave - 1), MaxRunnerChance);
	return FMath::FRand() < RunnerChance ? EZombieType::Runner : EZombieType::Walker;
}

bool AZombieWaveDirector::PickSpawnTransform(FTransform& OutTransform) const
{
	const APawn* Player = GetPlayerPawn();
	const FVector PlayerLocation = Player ? Player->GetActorLocation() : GetActorLocation();

	FVector ViewLocation = PlayerLocation;
	FRotator ViewRotation = FRotator::ZeroRotator;
	if (const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
	{
		PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}

	// Prefer points the player cannot see and that are not too close: zombies come out of the dark
	TArray<const AActor*> Hidden;
	TArray<const AActor*> Far;
	for (const AActor* Point : SpawnPoints)
	{
		if (!IsValid(Point))
		{
			continue;
		}

		const FVector Location = Point->GetActorLocation();
		if (FVector::Dist(Location, PlayerLocation) < MinSpawnDistance)
		{
			continue;
		}
		Far.Add(Point);

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ZombieSpawnSight), false, Player);
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, Location + FVector(0.f, 0.f, 80.f), ECC_Visibility, Params);
		const bool bBehind = FVector::DotProduct((Location - ViewLocation).GetSafeNormal(), ViewRotation.Vector()) < 0.3f;
		if (bBlocked || bBehind)
		{
			Hidden.Add(Point);
		}
	}

	const TArray<const AActor*>& Candidates = !Hidden.IsEmpty() ? Hidden : Far;
	FVector SpawnLocation;
	if (!Candidates.IsEmpty())
	{
		SpawnLocation = Candidates[FMath::RandRange(0, Candidates.Num() - 1)]->GetActorLocation();
	}
	else
	{
		// No spawn points: a reachable point in a ring around the player
		const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		FNavLocation NavPoint;
		const FVector Offset = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f).GetSafeNormal() * MinSpawnDistance;
		if (!NavSystem || !NavSystem->ProjectPointToNavigation(PlayerLocation + Offset, NavPoint, FVector(600.f, 600.f, 500.f)))
		{
			return false;
		}
		SpawnLocation = NavPoint.Location;
	}

	// Spread zombies that use the same point
	SpawnLocation += FVector(FMath::FRandRange(-120.f, 120.f), FMath::FRandRange(-120.f, 120.f), 100.f);
	const FRotator Facing = (PlayerLocation - SpawnLocation).GetSafeNormal2D().Rotation();
	OutTransform = FTransform(Facing, SpawnLocation);
	return true;
}

AZombieCharacter* AZombieWaveDirector::SpawnZombieAt(const FTransform& SpawnTransform, EZombieType ZombieType)
{
	AZombieCharacter* Zombie = GetWorld()->SpawnActorDeferred<AZombieCharacter>(ZombieClass, SpawnTransform, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
	if (!Zombie)
	{
		return nullptr;
	}

	Zombie->FinishSpawning(SpawnTransform);
	if (!IsValid(Zombie))
	{
		return nullptr;
	}

	Zombie->SetupType(ZombieType, 1.f + HealthPerWave * (Wave - 1));
	Zombie->OnDied.AddUObject(this, &AZombieWaveDirector::HandleZombieDied);
	AliveZombies.Add(Zombie);
	return Zombie;
}

void AZombieWaveDirector::SpawnZombie()
{
	FTransform SpawnTransform;
	if (!ZombieClass || !PickSpawnTransform(SpawnTransform))
	{
		return;
	}

	if (SpawnZombieAt(SpawnTransform, PickType()))
	{
		--ToSpawn;
	}
}

void AZombieWaveDirector::SpawnDormantZombies()
{
	const APawn* Player = GetPlayerPawn();
	const UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (Wave < DormantFirstWave || !Player || !NavSystem || !ZombieClass)
	{
		return;
	}

	// Some of the bodies lying around the camp are not dead
	const int32 Count = FMath::Min(1 + Wave / 2, FMath::Min(MaxDormant, ToSpawn));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FNavLocation Point;
		bool bFound = false;
		for (int32 Attempt = 0; Attempt < 12 && !bFound; ++Attempt)
		{
			bFound = NavSystem->GetRandomReachablePointInRadius(Player->GetActorLocation(), 1800.f, Point)
				&& FVector::Dist2D(Point.Location, Player->GetActorLocation()) >= 700.f;
		}
		if (!bFound)
		{
			continue;
		}

		const FTransform SpawnTransform(FRotator::ZeroRotator, Point.Location + FVector(0.f, 0.f, 95.f));
		if (AZombieCharacter* Zombie = SpawnZombieAt(SpawnTransform, EZombieType::Walker))
		{
			if (AZombieAIController* AI = Cast<AZombieAIController>(Zombie->GetController()))
			{
				AI->MakeDormant();
			}
			--ToSpawn;
		}
	}
}

void AZombieWaveDirector::AddBonusZombies(int32 Count)
{
	if (Phase == EWavePhase::Active)
	{
		ToSpawn += FMath::Max(Count, 0);
		NextSpawnTime = FMath::Min(NextSpawnTime, GetWorld()->GetTimeSeconds() + 0.5f);
	}
}

void AZombieWaveDirector::UpdateScares(float DeltaTime)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextScareTime)
	{
		return;
	}

	// Scares come more often between waves, when it is quiet
	const float Scale = Phase == EWavePhase::Active ? 1.f : 0.7f;
	NextScareTime = Now + FMath::FRandRange(ScareInterval.X, ScareInterval.Y) * Scale;
	PlayScare();
}

void AZombieWaveDirector::PlayScare()
{
	const APawn* Player = GetPlayerPawn();
	if (!Player)
	{
		return;
	}

	const int32 Kind = FMath::RandRange(0, 2);

	// A scream somewhere out in the trees
	if (Kind == 0 && !DistantScreams.IsEmpty())
	{
		const FVector Direction = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f).GetSafeNormal();
		const FVector Location = Player->GetActorLocation() + Direction * FMath::FRandRange(2000.f, 3500.f);
		UGameplayStatics::PlaySoundAtLocation(this, DistantScreams[FMath::RandRange(0, DistantScreams.Num() - 1)], Location,
			FMath::FRandRange(0.7f, 1.f), FMath::FRandRange(0.8f, 1.1f), 0.f, DistantAttenuation);
		return;
	}

	// The nearest lamp dies for a moment
	if (Kind == 1 && !LevelLights.IsEmpty())
	{
		ULightComponent* Nearest = nullptr;
		float NearestDistance = 3000.f;
		for (ULightComponent* Light : LevelLights)
		{
			const float Distance = IsValid(Light) ? FVector::Dist(Light->GetComponentLocation(), Player->GetActorLocation()) : TNumericLimits<float>::Max();
			if (Distance < NearestDistance)
			{
				Nearest = Light;
				NearestDistance = Distance;
			}
		}
		if (Nearest)
		{
			FlickerLight(Nearest);
			return;
		}
	}

	// A whisper right behind you
	if (!Whispers.IsEmpty())
	{
		const FVector Behind = Player->GetActorLocation() - Player->GetActorForwardVector() * 250.f;
		UGameplayStatics::PlaySoundAtLocation(this, Whispers[FMath::RandRange(0, Whispers.Num() - 1)], Behind, 0.6f);
	}
}

void AZombieWaveDirector::FlickerLight(ULightComponent* Light)
{
	if (!IsValid(Light))
	{
		return;
	}

	if (LightFlickerSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, LightFlickerSound, Light->GetComponentLocation(), 0.7f);
	}

	// A few quick off and on steps, then back to normal
	const float BaseIntensity = Light->Intensity;
	TWeakObjectPtr<ULightComponent> WeakLight = Light;
	const int32 Steps = FMath::RandRange(5, 9);
	float Time = 0.f;
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		Time += FMath::FRandRange(0.05f, 0.18f);
		const float Intensity = Step == Steps ? BaseIntensity : (Step % 2 == 0 ? BaseIntensity * FMath::FRandRange(0.f, 0.15f) : BaseIntensity);
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [WeakLight, Intensity]()
		{
			if (WeakLight.IsValid())
			{
				WeakLight->SetIntensity(Intensity);
			}
		}), Time, false);
	}
}

void AZombieWaveDirector::HandleZombieDied(AZombieCharacter* Zombie, bool bHeadshot)
{
	AliveZombies.Remove(Zombie);
	++Kills;
	if (Zombie)
	{
		DropLoot(*Zombie);
	}
	Headshots += bHeadshot ? 1 : 0;
}

void AZombieWaveDirector::PlayMusic(USoundBase* Music)
{
	if (!Music || MusicComponent->GetSound() == Music)
	{
		return;
	}

	MusicComponent->FadeOut(WaveDirector::MusicFadeTime * 0.5f, 0.f);
	MusicComponent->SetSound(Music);
	MusicComponent->FadeIn(WaveDirector::MusicFadeTime, MusicVolume);
}
