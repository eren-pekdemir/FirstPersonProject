#include "Player/PlayerCharacter.h"

#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Gun/GunBase.h"
#include "Kismet/GameplayStatics.h"
#include "Player/CameraFeelComponent.h"
#include "Zombie/ZombieGameMode.h"
#include "Zombie/ZombieCoordinator.h"

namespace PlayerHealth
{
	constexpr float DamageFlashTime = 0.6f;
	/** Health fraction under which the heartbeat plays */
	constexpr float LowHealthFraction = 0.35f;
	/** Camera kick per point of damage */
	constexpr float KickPerDamage = 0.18f;
	constexpr float MaxKick = 6.f;
	constexpr float DeathCameraDrop = 900.f;
}

float APlayerCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (bDead || DamageAmount <= 0.f)
	{
		return 0.f;
	}

	const float Damage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (Damage <= 0.f)
	{
		return 0.f;
	}

	Health = FMath::Max(0.f, Health - Damage);
	LastDamageTime = GetWorld()->GetTimeSeconds();
	LastDamageOrigin = DamageCauser ? DamageCauser->GetActorLocation() : GetActorLocation() - GetActorForwardVector() * 100.f;

	// The blow knocks the view: toward the side it came from
	if (CameraFeel)
	{
		const FVector ToCauser = (LastDamageOrigin - GetActorLocation()).GetSafeNormal2D();
		const float Side = FVector::DotProduct(ToCauser, GetActorRightVector());
		const float Kick = FMath::Min(Damage * PlayerHealth::KickPerDamage, PlayerHealth::MaxKick);
		CameraFeel->AddRecoilKick(FRotator(-Kick * 0.6f, -Side * Kick, Side * Kick * 1.5f), Kick);
		CameraFeel->AddDip(Damage * 6.f);
	}

	if (!HurtSounds.IsEmpty())
	{
		UGameplayStatics::PlaySound2D(this, HurtSounds[FMath::RandRange(0, HurtSounds.Num() - 1)]);
	}

	if (Health <= 0.f)
	{
		Die();
	}

	return Damage;
}

void APlayerCharacter::RestoreHealth()
{
	if (!bDead)
	{
		Health = MaxHealth;
	}
}

bool APlayerCharacter::AddHealth(float Amount)
{
	if (bDead || Health >= MaxHealth)
	{
		return false;
	}
	Health = FMath::Min(MaxHealth, Health + Amount);
	return true;
}

bool APlayerCharacter::AddBattery(float Fraction)
{
	if (bDead || Battery >= 0.999f)
	{
		return false;
	}
	Battery = FMath::Min(1.f, Battery + Fraction);
	return true;
}

bool APlayerCharacter::IsFlashlightOn() const
{
	return Flashlight && Flashlight->IsVisible();
}

void APlayerCharacter::ShowPickupMessage(const FString& Text, const FLinearColor& Color)
{
	PickupMessage = Text;
	PickupMessageColor = Color;
	PickupMessageTime = GetWorld()->GetTimeSeconds();
}

float APlayerCharacter::GetTimeSincePickupMessage() const
{
	return GetWorld()->GetTimeSeconds() - PickupMessageTime;
}

float APlayerCharacter::GetDamageFlash() const
{
	return FMath::Clamp(1.f - GetTimeSinceDamaged() / PlayerHealth::DamageFlashTime, 0.f, 1.f);
}

float APlayerCharacter::GetTimeSinceDamaged() const
{
	return GetWorld()->GetTimeSeconds() - LastDamageTime;
}

float APlayerCharacter::GetDamageDirectionYaw() const
{
	FVector ViewLocation;
	FRotator ViewRotation;
	GetActorEyesViewPoint(ViewLocation, ViewRotation);

	const FRotator ToOrigin = (LastDamageOrigin - ViewLocation).GetSafeNormal2D().Rotation();
	return FRotator::NormalizeAxis(ToOrigin.Yaw - ViewRotation.Yaw);
}

void APlayerCharacter::UpdateHealth(float DeltaTime)
{
	if (bDead)
	{
		return;
	}

	if (GetTimeSinceDamaged() >= RegenDelay && Health < MaxHealth)
	{
		Health = FMath::Min(MaxHealth, Health + RegenRate * DeltaTime);
	}

	// Heartbeat while badly hurt
	const bool bLow = Health / MaxHealth < PlayerHealth::LowHealthFraction;
	if (bLow && LowHealthSound && !LowHealthAudio)
	{
		LowHealthAudio = UGameplayStatics::SpawnSound2D(this, LowHealthSound, 0.8f, 1.f, 0.f, nullptr, false, false);
	}
	else if (!bLow && LowHealthAudio)
	{
		LowHealthAudio->FadeOut(1.f, 0.f);
		LowHealthAudio = nullptr;
	}
}

void APlayerCharacter::Die()
{
	bDead = true;
	bWantsToFire = bWantsToAim = bWantsToSprint = false;

	if (CurrentGun)
	{
		CurrentGun->StopFire();
	}

	if (DeathSound)
	{
		UGameplayStatics::PlaySound2D(this, DeathSound);
	}

	if (LowHealthAudio)
	{
		LowHealthAudio->FadeOut(2.f, 0.f);
		LowHealthAudio = nullptr;
	}

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();

	// Falls to the ground
	if (CameraFeel)
	{
		CameraFeel->AddDip(PlayerHealth::DeathCameraDrop);
		CameraFeel->AddRollImpulse(220.f);
	}

	if (APlayerController* PlayerController = GetController<APlayerController>())
	{
		PlayerController->SetIgnoreMoveInput(true);
		PlayerController->SetIgnoreLookInput(true);
	}

	if (AZombieGameMode* GameMode = GetWorld()->GetAuthGameMode<AZombieGameMode>())
	{
		GameMode->OnPlayerDied(GetController());
	}
}

void APlayerCharacter::ToggleFlashlight()
{
	if (!Flashlight || bDead)
	{
		return;
	}

	// A dead battery only clicks
	Flashlight->SetVisibility(!Flashlight->IsVisible() && Battery > 0.f);
	if (FlashlightClickSound)
	{
		UGameplayStatics::PlaySound2D(this, FlashlightClickSound);
	}
}

void APlayerCharacter::UpdateFlashlight(float DeltaTime)
{
	if (!Flashlight || !Flashlight->IsVisible())
	{
		return;
	}

	Battery = FMath::Max(0.f, Battery - DeltaTime / BatteryLife);
	if (Battery <= 0.f)
	{
		// Dies with a last flicker
		Flashlight->SetVisibility(false);
		if (BatteryDeadSound)
		{
			UGameplayStatics::PlaySound2D(this, BatteryDeadSound);
		}
		else if (FlashlightClickSound)
		{
			UGameplayStatics::PlaySound2D(this, FlashlightClickSound, 0.6f, 0.8f);
		}
		ShowPickupMessage(TEXT("BATTERY DEAD"), FLinearColor(0.9f, 0.2f, 0.1f));
		return;
	}

	// Old battery: rare short flickers, more often when hurt or running low
	FlashlightFlickerTime -= DeltaTime;
	float FlickerChance = (Health / MaxHealth < PlayerHealth::LowHealthFraction ? 0.02f : 0.004f);
	const float LowBattery = LowBatteryFraction > 0.f ? FMath::Clamp(1.f - Battery / LowBatteryFraction, 0.f, 1.f) : 0.f;
	FlickerChance = FMath::Max(FlickerChance, LowBattery * 0.08f);

	// Something is close: the light starts failing
	if (const UZombieCoordinator* Coordinator = GetWorld()->GetSubsystem<UZombieCoordinator>())
	{
		const float StalkerDistance = Coordinator->GetStalkerDistance();
		if (StalkerDistance < 1200.f)
		{
			FlickerChance = FMath::Max(FlickerChance, FMath::GetMappedRangeValueClamped(FVector2f(1200.f, 300.f), FVector2f(0.02f, 0.12f), StalkerDistance));
		}
	}
	if (FlashlightFlickerTime <= 0.f && FMath::FRand() < FlickerChance)
	{
		FlashlightFlickerTime = FMath::FRandRange(0.05f, 0.25f);
	}

	// A weak battery gives a dimmer, warmer light
	const float Charge = FMath::Lerp(1.f, 0.3f, LowBattery);
	const float Intensity = (FlashlightFlickerTime > 0.f ? FlashlightBaseIntensity * FMath::FRandRange(0.05f, 0.5f) : FlashlightBaseIntensity) * Charge;
	Flashlight->SetIntensity(Intensity);
}
