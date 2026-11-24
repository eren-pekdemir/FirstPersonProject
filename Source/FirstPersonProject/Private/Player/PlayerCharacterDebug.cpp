// Development helper: run the game with -ViewmodelCapture to get screenshots of every weapon pose
// (hip, aim, looking down, firing, reloading) without having to play. Used to tune the viewmodel.

#include "Player/PlayerCharacter.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "FirstPersonProject.h"
#include "GameFramework/PlayerController.h"
#include "Data/WeaponDataAsset.h"
#include "Gun/GunBase.h"
#include "Misc/CommandLine.h"
#include "Player/ViewmodelComponent.h"
#include "TimerManager.h"
#include "Zombie/ZombieAIController.h"
#include "Zombie/ZombieCharacter.h"
#include "Zombie/ZombieCoordinator.h"
#include "Zombie/ZombieWaveDirector.h"

namespace ViewmodelCapture
{
	/** Each step changes the state, waits, then takes a screenshot in a frame where nothing changes */
	enum class EStep : uint8 { Equip, Hip, Outside, OutsideTop, Aim, LookDown, Fire, Reload, Count };

	const TCHAR* StepNames[] = { TEXT("equip"), TEXT("hip"), TEXT("outside"), TEXT("outsidetop"), TEXT("aim"), TEXT("lookdown"), TEXT("fire"), TEXT("reload") };

	/** Time between a step's change and its screenshot */
	constexpr float SettleTimes[] = { 0.25f, 1.4f, 0.2f, 0.2f, 0.8f, 0.6f, 0.05f, 0.9f };

	/** Time between a screenshot and the next step's change */
	constexpr float ShotDelay = 0.15f;
}

void APlayerCharacter::StartViewmodelCapture()
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("ViewmodelCapture")))
	{
		return;
	}

	// Take every weapon lying in the level so all of them get captured
	TArray<AGunBase*> LooseGuns;
	for (TActorIterator<AGunBase> It(GetWorld()); It; ++It)
	{
		if (!Weapons.Contains(*It))
		{
			LooseGuns.Add(*It);
		}
	}

	MaxWeapons = FMath::Max(MaxWeapons, Weapons.Num() + LooseGuns.Num());
	for (AGunBase* Gun : LooseGuns)
	{
		PickUpGun(Gun);
	}

	ViewmodelCaptureStep = 0;
	GetWorldTimerManager().SetTimer(ViewmodelCaptureTimer, this, &APlayerCharacter::RunViewmodelCaptureStep, 1.5f, false);
#endif
}

void APlayerCharacter::RunViewmodelCaptureStep()
{
#if !UE_BUILD_SHIPPING
	using namespace ViewmodelCapture;

	APlayerController* PlayerController = GetController<APlayerController>();
	const int32 StepCount = static_cast<int32>(EStep::Count);

	// Even counters change the state, odd counters take the screenshot of that state
	const int32 StepIndex = ViewmodelCaptureStep / 2;
	const bool bShotPhase = ViewmodelCaptureStep % 2 == 1;
	const int32 Slot = StepIndex / StepCount;
	const EStep Step = static_cast<EStep>(StepIndex % StepCount);

	if (!PlayerController || !Weapons.IsValidIndex(Slot))
	{
		UE_LOG(LogFirstPersonProject, Display, TEXT("ViewmodelCapture: done"));
		if (PlayerController)
		{
			PlayerController->ConsoleCommand(TEXT("quit"));
		}
		return;
	}

	++ViewmodelCaptureStep;

	if (bShotPhase)
	{
		const TCHAR* Label = StepNames[static_cast<int32>(Step)];

		// Where the weapon's sockets are in camera space (X forward, Y right, Z up)
		const USkeletalMeshComponent* WeaponMesh = CurrentGun ? CurrentGun->GetViewMesh() : nullptr;
		const UCameraComponent* Camera = FindComponentByClass<UCameraComponent>();
		if (Step == EStep::Hip && WeaponMesh && Camera)
		{
			for (const TCHAR* Socket : { TEXT("Muzzle"), TEXT("Sight") })
			{
				if (WeaponMesh->DoesSocketExist(Socket))
				{
					const FTransform T = WeaponMesh->GetSocketTransform(Socket).GetRelativeTransform(Camera->GetComponentTransform());
					UE_LOG(LogFirstPersonProject, Display, TEXT("ViewmodelCapture: slot%d %s at %s X=%s Z=%s"), Slot, Socket,
						*T.GetLocation().ToString(), *T.GetUnitAxis(EAxis::X).ToString(), *T.GetUnitAxis(EAxis::Z).ToString());
				}
			}
			UE_LOG(LogFirstPersonProject, Display, TEXT("ViewmodelCapture: slot%d hand_r at %s hand_l at %s"), Slot,
				*Camera->GetComponentTransform().InverseTransformPosition(FirstPersonArms->GetSocketLocation(TEXT("hand_r"))).ToString(),
				*Camera->GetComponentTransform().InverseTransformPosition(FirstPersonArms->GetSocketLocation(TEXT("hand_l"))).ToString());
		}
		UE_LOG(LogFirstPersonProject, Display, TEXT("ViewmodelCapture: shot slot%d_%s"), Slot, Label);
		PlayerController->ConsoleCommand(FString::Printf(TEXT("HighResShot 1280x720 filename=VM_slot%d_%s"), Slot, Label));
		GetWorldTimerManager().SetTimer(ViewmodelCaptureTimer, this, &APlayerCharacter::RunViewmodelCaptureStep, ShotDelay, false);
		return;
	}

	SetOutsideView(Step == EStep::Outside || Step == EStep::OutsideTop, Step == EStep::OutsideTop ? 0.f : -110.f);

	const float Yaw = PlayerController->GetControlRotation().Yaw;
	bWantsToAim = Step == EStep::Aim;
	PlayerController->SetControlRotation(FRotator(Step == EStep::LookDown ? -35.f : 0.f, Yaw, 0.f));

	switch (Step)
	{
	case EStep::Equip:
		EquipSlot(Slot);
		break;
	case EStep::Fire:
		if (CurrentGun)
		{
			CurrentGun->StartFire();
			CurrentGun->StopFire();
		}
		break;
	case EStep::Reload:
		if (CurrentGun)
		{
			CurrentGun->StartReload();
		}
		break;
	default:
		break;
	}

	GetWorldTimerManager().SetTimer(ViewmodelCaptureTimer, this, &APlayerCharacter::RunViewmodelCaptureStep, SettleTimes[static_cast<int32>(Step)], false);
#endif
}

// -------------------------------------------------------------------------------------------------
// Feel capture

namespace FeelCapture
{
	struct FSegment
	{
		const TCHAR* Name;
		float Duration;
		const TCHAR* Weapon;	// class name part of the weapon to hold
		float YawRate;			// deg/s of view rotation
		FVector2D Move;			// X strafe, Y forward
		bool bSprint;
		bool bFire;
		float FireDuration;		// how long the trigger is held, 0 = whole segment
		bool bAim;
		float Shots[6];			// screenshot times into the segment, negative = unused
	};

	const FSegment Segments[] =
	{
		{ TEXT("rifle_idle"),    1.2f, TEXT("Rifle"),     0.f, FVector2D(0, 0), false, false, 0.f,   false, { 1.0f, -1, -1, -1, -1, -1 } },
		{ TEXT("turn_right"),    0.5f, TEXT("Rifle"),   240.f, FVector2D(0, 0), false, false, 0.f,   false, { 0.1f, 0.3f, 0.48f, -1, -1, -1 } },
		{ TEXT("turn_stop"),     0.6f, TEXT("Rifle"),     0.f, FVector2D(0, 0), false, false, 0.f,   false, { 0.05f, 0.15f, 0.3f, 0.55f, -1, -1 } },
		{ TEXT("walk_strafe"),   1.0f, TEXT("Rifle"),     0.f, FVector2D(1, 0), false, false, 0.f,   false, { 0.5f, 0.7f, 0.9f, -1, -1, -1 } },
		{ TEXT("sprint"),        1.2f, TEXT("Rifle"),     0.f, FVector2D(0, 1), true,  false, 0.f,   false, { 0.6f, 0.8f, 1.0f, 1.15f, -1, -1 } },
		{ TEXT("settle"),        0.8f, TEXT("Rifle"),     0.f, FVector2D(0, 0), false, false, 0.f,   false, { -1, -1, -1, -1, -1, -1 } },
		{ TEXT("rifle_burst"),   1.0f, TEXT("Rifle"),     0.f, FVector2D(0, 0), false, true,  0.f,   false, { 0.03f, 0.08f, 0.2f, 0.5f, 0.95f, -1 } },
		{ TEXT("rifle_recover"), 0.8f, TEXT("Rifle"),     0.f, FVector2D(0, 0), false, false, 0.f,   false, { 0.1f, 0.3f, 0.7f, -1, -1, -1 } },
		{ TEXT("aim_in"),        0.6f, TEXT("Rifle"),     0.f, FVector2D(0, 0), false, false, 0.f,   true,  { 0.55f, -1, -1, -1, -1, -1 } },
		{ TEXT("aim_burst"),     0.8f, TEXT("Rifle"),     0.f, FVector2D(0, 0), false, true,  0.f,   true,  { 0.04f, 0.3f, 0.75f, -1, -1, -1 } },
		{ TEXT("pistol_idle"),   1.2f, TEXT("Pistol"),    0.f, FVector2D(0, 0), false, false, 0.f,   false, { 1.1f, -1, -1, -1, -1, -1 } },
		{ TEXT("pistol_walk"),   1.6f, TEXT("Pistol"),    0.f, FVector2D(0, 1), false, false, 0.f,   false, { 0.4f, 0.8f, 1.2f, 1.55f, -1, -1 } },
		{ TEXT("pistol_settle"), 0.6f, TEXT("Pistol"),    0.f, FVector2D(0, 0), false, false, 0.f,   false, { -1, -1, -1, -1, -1, -1 } },
		{ TEXT("pistol_tap"),    0.5f, TEXT("Pistol"),    0.f, FVector2D(0, 0), false, true,  0.05f, false, { 0.01f, 0.025f, 0.05f, 0.1f, 0.4f, -1 } },
		{ TEXT("shotgun_idle"),  1.2f, TEXT("Shotgun"),   0.f, FVector2D(0, 0), false, false, 0.f,   false, { 1.1f, -1, -1, -1, -1, -1 } },
		{ TEXT("shotgun_shot"),  0.8f, TEXT("Shotgun"),   0.f, FVector2D(0, 0), false, true,  0.05f, false, { 0.03f, 0.08f, 0.3f, 0.42f, 0.48f, 0.75f } },
		{ TEXT("shotgun_aim_in"),   0.7f, TEXT("Shotgun"), 0.f, FVector2D(0, 0), false, false, 0.f,   true,  { 0.65f, -1, -1, -1, -1, -1 } },
		{ TEXT("shotgun_aim_shot"), 0.6f, TEXT("Shotgun"), 0.f, FVector2D(0, 0), false, true,  0.05f, true,  { 0.03f, 0.08f, 0.15f, 0.3f, -1, -1 } },
		{ TEXT("pistol_aim_in"),    0.8f, TEXT("Pistol"),  0.f, FVector2D(0, 0), false, false, 0.f,   true,  { 0.75f, -1, -1, -1, -1, -1 } },
		{ TEXT("pistol_aim_tap"),   0.5f, TEXT("Pistol"),  0.f, FVector2D(0, 0), false, true,  0.05f, true,  { 0.02f, 0.05f, 0.1f, 0.2f, -1, -1 } },
	};
}

int32 APlayerCharacter::FindWeaponSlot(const TCHAR* ClassNamePart) const
{
	for (int32 Index = 0; Index < Weapons.Num(); ++Index)
	{
		if (Weapons[Index] && Weapons[Index]->GetClass()->GetName().Contains(ClassNamePart))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void APlayerCharacter::StartFeelCapture()
{
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("FeelCapture")))
	{
		return;
	}

	// One weapon of each class: pick up the classes the inventory does not have yet
	TArray<AGunBase*> LooseGuns;
	for (TActorIterator<AGunBase> It(GetWorld()); It; ++It)
	{
		const bool bClassKnown = Weapons.ContainsByPredicate([&](const AGunBase* Held) { return Held && Held->GetClass() == It->GetClass(); })
			|| LooseGuns.ContainsByPredicate([&](const AGunBase* Loose) { return Loose->GetClass() == It->GetClass(); });
		if (!Weapons.Contains(*It) && !bClassKnown)
		{
			LooseGuns.Add(*It);
		}
	}

	MaxWeapons = FMath::Max(MaxWeapons, Weapons.Num() + LooseGuns.Num());
	for (AGunBase* Gun : LooseGuns)
	{
		PickUpGun(Gun);
	}

	bFeelCapture = true;
	FeelSegment = 0;
	FeelShot = 0;
	FeelTime = 0.f;
	FeelTotalTime = 0.f;
#endif
}

void APlayerCharacter::TickFeelCapture(float DeltaTime)
{
#if !UE_BUILD_SHIPPING
	using namespace FeelCapture;

	APlayerController* PlayerController = GetController<APlayerController>();
	if (!PlayerController)
	{
		return;
	}

	if (FeelSegment >= UE_ARRAY_COUNT(Segments))
	{
		bFeelCapture = false;
		bWantsToFire = bWantsToAim = bWantsToSprint = false;
		UE_LOG(LogFirstPersonProject, Display, TEXT("FeelCapture: done"));
		PlayerController->ConsoleCommand(TEXT("quit"));
		return;
	}

	const FSegment& Segment = Segments[FeelSegment];

	const int32 Slot = FindWeaponSlot(Segment.Weapon);
	if (Slot != INDEX_NONE && Weapons[Slot] != CurrentGun)
	{
		EquipSlot(Slot);
	}

	FRotator ControlRotation = PlayerController->GetControlRotation();
	if (FeelSegment == 0 && FeelTime == 0.f)
	{
		ControlRotation.Pitch = 0.f;
	}
	ControlRotation.Yaw += Segment.YawRate * DeltaTime;
	PlayerController->SetControlRotation(ControlRotation);

	LastMoveInput = Segment.Move;
	if (!Segment.Move.IsZero())
	{
		const FRotationMatrix YawMatrix(FRotator(0.f, ControlRotation.Yaw, 0.f));
		AddMovementInput(YawMatrix.GetUnitAxis(EAxis::X), Segment.Move.Y);
		AddMovementInput(YawMatrix.GetUnitAxis(EAxis::Y), Segment.Move.X);
	}

	bWantsToSprint = Segment.bSprint;
	bWantsToAim = Segment.bAim;

	const bool bTrigger = Segment.bFire && (Segment.FireDuration <= 0.f || FeelTime < Segment.FireDuration);
	if (bTrigger != bWantsToFire)
	{
		if (bTrigger)
		{
			OnFirePressed();
		}
		else
		{
			OnFireReleased();
		}
	}

	UE_LOG(LogFirstPersonProject, Display, TEXT("FEEL t=%.3f seg=%s ammo=%d pitch=%.2f speed=%.0f %s"),
		FeelTotalTime, Segment.Name, CurrentGun ? CurrentGun->GetAmmoInMagazine() : -1, ControlRotation.Pitch,
		GetVelocity().Size2D(), *Viewmodel->GetDebugString());

	while (FeelShot < UE_ARRAY_COUNT(Segment.Shots) && Segment.Shots[FeelShot] >= 0.f && FeelTime >= Segment.Shots[FeelShot])
	{
		const FString Name = FString::Printf(TEXT("FEEL_%02d_%s_%03d"), FeelSegment, Segment.Name, FMath::RoundToInt(Segment.Shots[FeelShot] * 100.f));
		UE_LOG(LogFirstPersonProject, Display, TEXT("FeelCapture: shot %s"), *Name);
		PlayerController->ConsoleCommand(FString::Printf(TEXT("HighResShot 960x540 filename=%s"), *Name));
		++FeelShot;
	}

	FeelTime += DeltaTime;
	FeelTotalTime += DeltaTime;

	if (FeelTime >= Segment.Duration)
	{
		++FeelSegment;
		FeelShot = 0;
		FeelTime = 0.f;
	}
#endif
}

void APlayerCharacter::SetOutsideView(bool bOutside, float SideOffset)
{
#if !UE_BUILD_SHIPPING
	APlayerController* PlayerController = GetController<APlayerController>();
	UCameraComponent* Camera = FindComponentByClass<UCameraComponent>();
	if (!PlayerController || !Camera)
	{
		return;
	}

	if (OutsideViewCamera.IsValid())
	{
		OutsideViewCamera->Destroy();
		OutsideViewCamera.Reset();
	}

	// Arms and weapon drawn as normal geometry so a camera beside the head can see them
	for (USkeletalMeshComponent* ArmsPart : { FirstPersonArms.Get(), FirstPersonGloves.Get() })
	{
		if (ArmsPart)
		{
			ArmsPart->SetOnlyOwnerSee(!bOutside);
			ArmsPart->SetFirstPersonPrimitiveType(bOutside ? EFirstPersonPrimitiveType::None : EFirstPersonPrimitiveType::FirstPerson);
		}
	}
	GetMesh()->SetVisibility(!bOutside);
	if (CurrentGun)
	{
		CurrentGun->SetFirstPersonView(!bOutside);
	}

	if (!bOutside)
	{
		PlayerController->SetViewTarget(this);
		return;
	}

	const FTransform View = Camera->GetComponentTransform();
	const FVector Location = SideOffset != 0.f
		? View.TransformPosition(FVector(35.f, SideOffset, 0.f))
		: View.TransformPosition(FVector(35.f, 0.f, 95.f));
	const FVector LookAt = View.TransformPosition(FVector(35.f, 5.f, -10.f));

	ACameraActor* CameraActor = GetWorld()->SpawnActor<ACameraActor>(Location, (LookAt - Location).Rotation());
	if (CameraActor)
	{
		CameraActor->GetCameraComponent()->SetFieldOfView(60.f);
		OutsideViewCamera = CameraActor;
		PlayerController->SetViewTarget(CameraActor);
	}
#endif
}

// -------------------------------------------------------------------------------------------------
// Zombie capture

namespace ZombieCapture
{
	const float ShotTimes[] = { 2.2f, 2.6f, 3.0f, 3.4f, 3.9f, 4.6f, 6.f, 9.f, 14.f, 19.f, 24.f, 30.f, 38.f, 46.f, 58.f };
	constexpr float Duration = 62.f;
	constexpr float EngageDistance = 2200.f;
}

void APlayerCharacter::TickZombieCapture(float DeltaTime)
{
#if !UE_BUILD_SHIPPING
	using namespace ZombieCapture;

	APlayerController* PlayerController = GetController<APlayerController>();
	if (!PlayerController)
	{
		return;
	}

	// -ZombieWeapon=2 fights with that slot (1 rifle, 2 shotgun, 3 pistol)
	static int32 WeaponSlot = 0;
	if (ZombieCaptureTime == 0.f)
	{
		FParse::Value(FCommandLine::Get(), TEXT("ZombieWeapon="), WeaponSlot);
	}
	// -ZombieBattery=0.1 starts with that flashlight charge
	if (ZombieCaptureTime == 0.f)
	{
		FParse::Value(FCommandLine::Get(), TEXT("ZombieBattery="), Battery);
	}

	static bool bEquipped = false;
	if (ZombieCaptureTime == 0.f)
	{
		bEquipped = false;
	}
	if (!bEquipped && WeaponSlot > 0 && Weapons.IsValidIndex(WeaponSlot - 1))
	{
		bEquipped = true;
		if (CurrentGun != Weapons[WeaponSlot - 1])
		{
			EquipSlot(WeaponSlot - 1);
		}
	}

	ZombieCaptureTime += DeltaTime;

	// Nearest visible living zombie
	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	AZombieCharacter* Nearest = nullptr;
	float NearestDistance = EngageDistance;
	TMap<EZombieState, int32> States;
	int32 Alive = 0;
	int32 TypeCounts[6] = { 0, 0, 0, 0, 0, 0 };
	for (TActorIterator<AZombieCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsAlive())
		{
			continue;
		}
		++Alive;
		TypeCounts[static_cast<int32>(It->GetZombieType())]++;
		if (const AZombieAIController* AI = Cast<AZombieAIController>(It->GetController()))
		{
			States.FindOrAdd(AI->GetState())++;
		}

		const FVector Chest = It->GetMesh()->GetSocketLocation(TEXT("spine_04"));
		const float Distance = FVector::Dist(ViewLocation, Chest);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ZombieCaptureSight), false, this);
		const bool bVisible = !GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, Chest, ECC_Visibility, Params) || Hit.GetActor() == *It;
		if (bVisible && Distance < NearestDistance)
		{
			Nearest = *It;
			NearestDistance = Distance;
		}
	}

	bool bShoot = false;
	if (Nearest)
	{
		// Aims at the head when close, the chest further away
		const FName Bone = NearestDistance < 900.f ? FName(TEXT("head")) : FName(TEXT("spine_04"));
		const FRotator Desired = (Nearest->GetMesh()->GetSocketLocation(Bone) - ViewLocation).Rotation();
		const FRotator NewRotation = FMath::RInterpTo(PlayerController->GetControlRotation(), Desired, DeltaTime, 7.f);
		PlayerController->SetControlRotation(NewRotation);
		bShoot = FMath::Abs(FRotator::NormalizeAxis(NewRotation.Yaw - Desired.Yaw)) < 4.f && FMath::Abs(NewRotation.Pitch - Desired.Pitch) < 4.f;
	}
	else
	{
		// Look around slowly when nothing is in sight
		FRotator Rotation = PlayerController->GetControlRotation();
		Rotation.Yaw += 25.f * DeltaTime;
		Rotation.Pitch = FMath::FInterpTo(FRotator::NormalizeAxis(Rotation.Pitch), -3.f, DeltaTime, 2.f);
		PlayerController->SetControlRotation(Rotation);
	}

	if (CurrentGun && CurrentGun->GetAmmoInMagazine() <= 0 && !CurrentGun->IsReloading())
	{
		OnReloadPressed();
	}

	// -ZombiePassive: only watches, to see the zombies attack
	static const bool bPassive = FParse::Param(FCommandLine::Get(), TEXT("ZombiePassive"));
	if (bPassive)
	{
		bShoot = false;
	}

	// Taps the trigger so single shot weapons keep firing
	if (bShoot && bWantsToFire && CurrentGun && CurrentGun->GetWeaponData() && CurrentGun->GetWeaponData()->FireMode == EWeaponFireMode::Single)
	{
		bShoot = (GFrameCounter % 8) < 4;
	}

	if (bShoot != bWantsToFire && !IsDead())
	{
		if (bShoot)
		{
			OnFirePressed();
		}
		else
		{
			OnFireReleased();
		}
	}

	ZombieCaptureLogTimer -= DeltaTime;
	if (ZombieCaptureLogTimer <= 0.f)
	{
		ZombieCaptureLogTimer = 2.f;
		const UZombieCoordinator* Coordinator = GetWorld()->GetSubsystem<UZombieCoordinator>();
		const AZombieWaveDirector* Director = Coordinator ? Coordinator->GetDirector() : nullptr;
		UE_LOG(LogFirstPersonProject, Display, TEXT("ZOMBIE t=%.1f wave=%d phase=%d remaining=%d kills=%d alive=%d wander=%d investigate=%d alert=%d chase=%d search=%d dormant=%d types W%d R%d B%d C%d Sc%d St%d health=%.0f nearest=%.0f battery=%.2f light=%d dilation=%.2f"),
			ZombieCaptureTime, Director ? Director->GetWave() : -1, Director ? static_cast<int32>(Director->GetPhase()) : -1,
			Director ? Director->GetZombiesRemaining() : -1, Director ? Director->GetKills() : -1, Alive,
			States.FindRef(EZombieState::Wander), States.FindRef(EZombieState::Investigate), States.FindRef(EZombieState::Alert),
			States.FindRef(EZombieState::Chase), States.FindRef(EZombieState::Search), States.FindRef(EZombieState::Dormant),
			TypeCounts[0], TypeCounts[1], TypeCounts[2], TypeCounts[3], TypeCounts[4], TypeCounts[5],
			GetHealthPercent() * 100.f, Nearest ? NearestDistance : -1.f,
			GetBatteryPercent(), IsFlashlightOn() ? 1 : 0, GetWorldSettings()->GetEffectiveTimeDilation());
	}

	// -ZombieShotInterval=0.8 takes a screenshot that often instead of at the fixed times
	static float ShotInterval = 0.f;
	static const bool bHasInterval = FParse::Value(FCommandLine::Get(), TEXT("ZombieShotInterval="), ShotInterval) && ShotInterval > 0.f;
	const bool bShotDue = bHasInterval
		? ZombieCaptureTime >= ShotInterval * (ZombieCaptureShot + 1)
		: ZombieCaptureShot < UE_ARRAY_COUNT(ShotTimes) && ZombieCaptureTime >= ShotTimes[ZombieCaptureShot];
	if (bShotDue)
	{
		PlayerController->ConsoleCommand(FString::Printf(TEXT("HighResShot %s filename=ZC_%02d"), bHasInterval ? TEXT("1920x1080") : TEXT("1280x720"), ZombieCaptureShot));
		++ZombieCaptureShot;
	}

	if (ZombieCaptureTime >= Duration)
	{
		bZombieCapture = false;
		PlayerController->ConsoleCommand(TEXT("quit"));
	}
#endif
}
