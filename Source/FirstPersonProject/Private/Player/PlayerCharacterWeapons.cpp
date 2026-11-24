// Weapon half of APlayerCharacter: inventory, equipping, firing, aiming, reloading and hit feedback.

#include "Player/PlayerCharacter.h"

#include "Blueprint/UserWidget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Gun/GunBase.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "Player/CameraFeelComponent.h"
#include "Player/ViewmodelComponent.h"
#include "TimerManager.h"

namespace PlayerWeapons
{
	/** Speed tolerance above WalkSpeed under which the character counts as no longer sprinting */
	constexpr float FireSpeedTolerance = 50.f;

	/** Hits older than this are dropped from the HUD list */
	constexpr float HitEventLifetime = 1.5f;

	constexpr int32 MaxHitEvents = 24;
}

// -------------------------------------------------------------------------------------------------
// Inventory

void APlayerCharacter::GiveStartingWeapons()
{
	TArray<TSubclassOf<AGunBase>> Classes = StartingWeapons;
	if (Classes.IsEmpty() && DefaultGun)
	{
		Classes.Add(DefaultGun);
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (const TSubclassOf<AGunBase>& GunClass : Classes)
	{
		if (!GunClass || Weapons.Num() >= MaxWeapons)
		{
			continue;
		}

		if (AGunBase* Gun = GetWorld()->SpawnActor<AGunBase>(GunClass, GetActorTransform(), SpawnParams))
		{
			PickUpGun(Gun);
		}
	}

	if (!Weapons.IsEmpty())
	{
		EquipSlot(0);
	}
}

void APlayerCharacter::PickUpGun(AGunBase* NewGun)
{
	if (!NewGun || Weapons.Contains(NewGun))
	{
		return;
	}

	SetFocusedGun(nullptr);

	int32 SlotIndex = INDEX_NONE;
	if (Weapons.Num() < MaxWeapons)
	{
		SlotIndex = Weapons.Add(NewGun);
	}
	else
	{
		// Full: the held weapon is swapped with the one on the ground
		SlotIndex = FMath::Max(Weapons.Find(CurrentGun), 0);
		AGunBase* Replaced = Weapons[SlotIndex];
		const FTransform DropTransform = NewGun->GetActorTransform();

		if (Replaced == CurrentGun)
		{
			CurrentGun = nullptr;
			Viewmodel->ClearWeapon();
		}

		DropGun(Replaced, DropTransform);
		Weapons[SlotIndex] = NewGun;
	}

	// Recoil and spread read the owner, so it must be set before the gun is used
	NewGun->SetOwner(this);
	NewGun->SetInstigator(this);
	NewGun->SetHeld(true);
	NewGun->SetHolstered(true);
	AttachGunToHand(NewGun);
	BindGunEvents(NewGun);

	EquipSlot(SlotIndex);
}

void APlayerCharacter::EquipSlot(int32 SlotIndex)
{
	if (!Weapons.IsValidIndex(SlotIndex) || Weapons[SlotIndex] == CurrentGun)
	{
		return;
	}

	StopFiring();
	GetWorldTimerManager().ClearTimer(AutoReloadTimer);

	if (CurrentGun)
	{
		CurrentGun->SetHolstered(true);

		if (const UWeaponDataAsset* OldData = CurrentGun->GetWeaponData(); OldData && OldData->AnimLayerClass)
		{
			GetMesh()->UnlinkAnimClassLayers(OldData->AnimLayerClass);
		}
	}

	CurrentGun = Weapons[SlotIndex];
	CurrentGun->SetHolstered(false);
	CurrentGun->OnEquipped();
	Viewmodel->SetWeapon(CurrentGun);

	if (const UWeaponDataAsset* WeaponData = CurrentGun->GetWeaponData())
	{
		if (WeaponData->AnimLayerClass)
		{
			GetMesh()->LinkAnimClassLayers(WeaponData->AnimLayerClass);
		}

		if (WeaponData->EquipMontage)
		{
			// Fit the draw animation into the equip time
			const float PlayRate = WeaponData->EquipTime > 0.f
				? FMath::Max(WeaponData->EquipMontage->GetPlayLength() / WeaponData->EquipTime, 1.f)
				: 1.f;
			PlayAnimMontage(WeaponData->EquipMontage, PlayRate);
		}
	}
}

void APlayerCharacter::AttachGunToHand(AGunBase* Gun)
{
	const UWeaponDataAsset* WeaponData = Gun->GetWeaponData();
	const FName AttachSocket = UViewmodelComponent::GetWeaponSocket(WeaponData);

	// The owner sees the weapon in the first person arms, rendered with the viewmodel FOV
	USkeletalMeshComponent* Hands = FirstPersonArms ? FirstPersonArms.Get() : GetMesh();
	Gun->AttachToComponent(Hands, FAttachmentTransformRules::SnapToTargetIncludingScale, AttachSocket);
	Gun->SetFirstPersonView(Hands == FirstPersonArms);

	if (WeaponData && !WeaponData->AttachOffset.Equals(FTransform::Identity))
	{
		Gun->SetActorRelativeTransform(WeaponData->AttachOffset);
	}
}

void APlayerCharacter::DropGun(AGunBase* Gun, const FTransform& DropTransform)
{
	if (!Gun)
	{
		return;
	}

	UnbindGunEvents(Gun);

	if (const UWeaponDataAsset* WeaponData = Gun->GetWeaponData(); WeaponData && WeaponData->AnimLayerClass)
	{
		GetMesh()->UnlinkAnimClassLayers(WeaponData->AnimLayerClass);
	}

	Gun->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Gun->SetFirstPersonView(false);
	Gun->SetActorTransform(DropTransform);
	Gun->SetHeld(false);
	Gun->SetOwner(nullptr);
	Gun->SetInstigator(nullptr);
}

void APlayerCharacter::OnSwitchWeapon(const FInputActionValue& Value)
{
	if (Weapons.Num() < 2 || IsTraversing())
	{
		return;
	}

	const float Direction = Value.Get<float>();
	if (FMath::IsNearlyZero(Direction))
	{
		return;
	}

	const int32 CurrentIndex = FMath::Max(Weapons.Find(CurrentGun), 0);
	const int32 Step = Direction > 0.f ? 1 : -1;
	EquipSlot((CurrentIndex + Step + Weapons.Num()) % Weapons.Num());
}

void APlayerCharacter::OnSelectWeapon(int32 SlotIndex)
{
	if (!IsTraversing())
	{
		EquipSlot(SlotIndex);
	}
}

// -------------------------------------------------------------------------------------------------
// Gun events

void APlayerCharacter::BindGunEvents(AGunBase* Gun)
{
	UnbindGunEvents(Gun);
	Gun->OnFired.AddUObject(this, &APlayerCharacter::HandleGunFired);
	Gun->OnHit.AddUObject(this, &APlayerCharacter::HandleGunHit);
	Gun->OnEmpty.AddUObject(this, &APlayerCharacter::HandleGunEmpty);
}

void APlayerCharacter::UnbindGunEvents(AGunBase* Gun)
{
	if (Gun)
	{
		Gun->OnFired.RemoveAll(this);
		Gun->OnHit.RemoveAll(this);
		Gun->OnEmpty.RemoveAll(this);
	}
}

void APlayerCharacter::HandleGunFired(const AGunBase& Gun)
{
	const UWeaponDataAsset* WeaponData = Gun.GetWeaponData();
	if (!CameraFeel || !WeaponData)
	{
		return;
	}

	Viewmodel->OnFired(bIsAiming);

	const float Multiplier = bIsAiming ? WeaponData->AimRecoilMultiplier : 1.f;
	CameraFeel->AddRecoilKick(WeaponData->CameraKick * Multiplier, WeaponData->FOVKick * Multiplier);
}

void APlayerCharacter::HandleGunHit(const FGunShotResult& Result)
{
	const float Now = GetWorld()->GetTimeSeconds();

	RecentHits.RemoveAll([Now](const FPlayerHitEvent& Event)
	{
		return Now - Event.Time > PlayerWeapons::HitEventLifetime;
	});

	if (RecentHits.Num() >= PlayerWeapons::MaxHitEvents)
	{
		RecentHits.RemoveAt(0);
	}

	FPlayerHitEvent& Event = RecentHits.AddDefaulted_GetRef();
	Event.Location = Result.HitLocation;
	Event.Damage = Result.Damage;
	Event.Time = Now;
	Event.bHeadshot = Result.bHeadshot;
	Event.bKilled = Result.bKilled;

	USoundBase* Sound = HitMarkerSound;
	if (Result.bKilled && KillSound)
	{
		Sound = KillSound;
	}
	else if (Result.bHeadshot && HeadshotSound)
	{
		Sound = HeadshotSound;
	}

	if (Sound)
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}
}

void APlayerCharacter::HandleGunEmpty(AGunBase& Gun)
{
	StopFiring();

	if (&Gun == CurrentGun && Gun.GetReserveAmmo() > 0)
	{
		GetWorldTimerManager().SetTimer(AutoReloadTimer, this, &APlayerCharacter::TryReload, AutoReloadDelay, false);
	}
}

// -------------------------------------------------------------------------------------------------
// Firing, aiming and reloading

bool APlayerCharacter::CanUseWeapon() const
{
	return CurrentGun && !IsTraversing();
}

void APlayerCharacter::OnFirePressed()
{
	bWantsToFire = true;
}

void APlayerCharacter::OnFireReleased()
{
	bWantsToFire = false;
	StopFiring();
}

void APlayerCharacter::UpdateFiring()
{
	if (bIsFiring && !CanUseWeapon())
	{
		StopFiring();
		return;
	}

	if (!bWantsToFire || bIsFiring || !CanUseWeapon())
	{
		return;
	}

	// Weapons can't be fired at sprint speed on foot: wait until the character has slowed down.
	// Sliding and airborne shots are allowed.
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const bool bRunningTooFast = Movement->IsMovingOnGround() && !bIsSliding
		&& GetVelocity().Size2D() > WalkSpeed + PlayerWeapons::FireSpeedTolerance;

	if (bRunningTooFast)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(AutoReloadTimer);
	CurrentGun->StartFire();
	bIsFiring = true;
}

void APlayerCharacter::StopFiring()
{
	if (CurrentGun && bIsFiring)
	{
		CurrentGun->StopFire();
	}

	bIsFiring = false;
}

void APlayerCharacter::OnAimPressed()
{
	bWantsToAim = true;
}

void APlayerCharacter::OnAimReleased()
{
	bWantsToAim = false;
}

void APlayerCharacter::UpdateAiming()
{
	const bool bNewAiming = bWantsToAim && CanUseWeapon();

	if (bNewAiming && !bIsAiming && CurrentGun->GetWeaponData())
	{
		if (USoundBase* AimSound = CurrentGun->GetWeaponData()->AimInSound)
		{
			UGameplayStatics::PlaySound2D(this, AimSound, 0.6f);
		}
	}

	if (CrosshairWidget && bNewAiming != bIsAiming)
	{
		// Aiming down sights uses the weapon's own sights
		CrosshairWidget->SetVisibility(bNewAiming ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	}

	bIsAiming = bNewAiming;

	if (CurrentGun)
	{
		CurrentGun->SetAiming(bIsAiming);
	}
}

void APlayerCharacter::OnReloadPressed()
{
	TryReload();
}

void APlayerCharacter::TryReload()
{
	if (!CanUseWeapon())
	{
		return;
	}

	if (CurrentGun->StartReload())
	{
		StopFiring();
	}
}
