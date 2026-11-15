#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponDataAsset.generated.h"

class UAnimInstance;
class UAnimMontage;
class UCameraShakeBase;
class UNiagaraSystem;
class UParticleSystem;
class USoundBase;

UENUM(BlueprintType)
enum class EWeaponFireMode : uint8
{
	Single		UMETA(DisplayName = "Single Fire"),
	Automatic	UMETA(DisplayName = "Automatic Fire")
};

UENUM(BlueprintType)
enum class EWeaponReloadType : uint8
{
	/** Whole magazine is swapped at once */
	Magazine	UMETA(DisplayName = "Magazine"),
	/** Rounds are inserted one by one (shotguns). Firing interrupts the reload. */
	PerRound	UMETA(DisplayName = "Per Round")
};

/** A sound played at a fixed time into an action (reload, equip...) */
USTRUCT(BlueprintType)
struct FWeaponTimedSound
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0", Units = "s"))
	float Time = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<USoundBase> Sound;
};

/**
 *  All tuning data for a single weapon type.
 *  Adding a new weapon only requires a child Blueprint of AGunBase and one of these assets.
 */
UCLASS(BlueprintType)
class FIRSTPERSONPROJECT_API UWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	/** Seconds between two consecutive shots, derived from RoundsPerMinute */
	float GetTimeBetweenShots() const { return 60.f / FMath::Max(RoundsPerMinute, 1.f); }

	/** Name shown on the HUD */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "General")
	FText DisplayName = NSLOCTEXT("Weapon", "DefaultName", "Weapon");

	// ---------------------------------------------------------------------------------------------
	// Firing

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Firing")
	EWeaponFireMode FireMode = EWeaponFireMode::Single;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Firing", meta = (ClampMin = "1"))
	float RoundsPerMinute = 60.f;

	/** Maximum distance of the hitscan trace, in cm */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Firing", meta = (ClampMin = "0", Units = "cm"))
	float MaxRange = 10000.f;

	/** Traces per shot. Greater than 1 for shotguns. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Firing", meta = (ClampMin = "1"))
	int32 PelletsPerShot = 1;

	/** Fixed cone (degrees) pellets are spread in, on top of the crosshair spread */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Firing", meta = (ClampMin = "0", Units = "deg"))
	float PelletConeAngle = 0.f;

	/** Converts the crosshair spread (pixels) into a bullet deviation cone (degrees) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Firing", meta = (ClampMin = "0"))
	float SpreadToAngle = 0.035f;

	// ---------------------------------------------------------------------------------------------
	// Damage

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float Damage = 20.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "1"))
	float HeadshotMultiplier = 2.f;

	/** Impulse applied to simulating physics bodies that are hit */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0"))
	float HitImpulse = 3000.f;

	// ---------------------------------------------------------------------------------------------
	// Ammo

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = "1"))
	int32 MagazineSize = 12;

	/** Reserve ammo the weapon spawns with */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = "0"))
	int32 StartingReserveAmmo = 60;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = "0"))
	int32 MaxReserveAmmo = 120;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	EWeaponReloadType ReloadType = EWeaponReloadType::Magazine;

	/** Magazine reload: total time. Per round reload: time before the first round goes in. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = "0.05", Units = "s"))
	float ReloadTime = 1.6f;

	/** Reload time when the magazine is completely empty (needs chambering). 0 = same as ReloadTime. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = "0", Units = "s"))
	float EmptyReloadTime = 0.f;

	/** Per round reload only: time to insert each round */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = "0.05", Units = "s", EditCondition = "ReloadType == EWeaponReloadType::PerRound"))
	float TimePerRound = 0.5f;

	// ---------------------------------------------------------------------------------------------
	// Handling

	/** Time after equipping before the weapon can be fired */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0", Units = "s"))
	float EquipTime = 0.35f;

	/** Camera field of view while aiming down sights */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "10", ClampMax = "120", Units = "deg"))
	float AimFOV = 65.f;

	/** How fast the camera zooms in and out of ADS */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0"))
	float AimSpeed = 14.f;

	/** Movement speed multiplier while aiming */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0", ClampMax = "1"))
	float AimMoveSpeedMultiplier = 0.6f;

	/** Spread multiplier while aiming */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0", ClampMax = "1"))
	float AimSpreadMultiplier = 0.2f;

	/** Recoil multiplier while aiming */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = "0", ClampMax = "2"))
	float AimRecoilMultiplier = 0.75f;

	// ---------------------------------------------------------------------------------------------
	// Animation

	/** Anim layer linked to the character mesh while this weapon is held */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TSubclassOf<UAnimInstance> AnimLayerClass;

	/** Character mesh socket the weapon is attached to */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	FName AttachSocketName = TEXT("WeaponSocket");

	/** Extra offset applied after attaching, for meshes whose pivot doesn't match the socket */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	FTransform AttachOffset = FTransform::Identity;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> CharacterFireMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> EquipMontage;

	/** Played scaled to the reload time */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> ReloadMontage;

	// ---------------------------------------------------------------------------------------------
	// Sound

	/** One is picked at random per shot */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TArray<TObjectPtr<USoundBase>> FireSounds;

	/** Legacy single fire sound, used when FireSounds is empty */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundBase> FireSound;

	/** Echo played when the trigger is released / after a single shot. One is picked at random. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TArray<TObjectPtr<USoundBase>> TailSounds;

	/** Played some time after each shot (pump, bolt...) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundBase> CycleSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = "0", Units = "s"))
	float CycleSoundDelay = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundBase> DryFireSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundBase> EquipSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundBase> AimInSound;

	/** Magazine reload: timeline relative to the start. Per round: played before the first round. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TArray<FWeaponTimedSound> ReloadSounds;

	/** Per round reload: played each time a round is inserted */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundBase> RoundInsertSound;

	/** Per round reload: played when the reload finishes */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundBase> ReloadEndSound;

	// ---------------------------------------------------------------------------------------------
	// Feedback

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback")
	TSubclassOf<UCameraShakeBase> FireCameraShake;

	/** Procedural camera kick per shot (pitch up, random yaw, roll), recovers by itself */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback")
	FRotator CameraKick = FRotator(0.6f, 0.25f, 0.4f);

	/** FOV punch per shot, in degrees */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback", meta = (Units = "deg"))
	float FOVKick = 0.8f;

	/** Muzzle flash spawned on the weapon's muzzle socket */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<UParticleSystem> ShootParticle;

	/** Tracer travelling from the muzzle to the impact point */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<UNiagaraSystem> TracerSystem;

	/** Actor spawned at the impact point (decal, debris, sound...) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback")
	TSubclassOf<AActor> ImpactActorClass;

	/** Smoke puff played once the weapon stops firing */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<UNiagaraSystem> MuzzleSmokeSystem;

	/** Time after the last shot before the muzzle smoke is played */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback", meta = (ClampMin = "0", Units = "s"))
	float SmokeDelay = 0.4f;

	// ---------------------------------------------------------------------------------------------
	// Recoil

	/** Per-shot camera kick. X = yaw (right), Y = pitch (up). The last entry repeats once the pattern runs out. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	TArray<FVector2D> RecoilPattern;

	/** How fast the recoil kick is applied to the view. Higher feels snappier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "0"))
	float RecoilApplySpeed = 18.f;

	/** Time without firing after which the recoil pattern restarts from the first entry */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "0", Units = "s"))
	float RecoilResetDelay = 0.3f;

	/** Fraction of the accumulated vertical recoil pulled back once the player stops firing */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "0", ClampMax = "1"))
	float RecoilRecoveryFraction = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "0"))
	float RecoilRecoverySpeed = 8.f;

	// ---------------------------------------------------------------------------------------------
	// Crosshair spread (screen space, in pixels)

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = "0"))
	float BaseSpread = 8.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = "0"))
	float SpreadPerShot = 14.f;

	/** Extra spread added while the owner moves at full sprint speed */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = "0"))
	float MovementSpread = 25.f;

	/** Extra spread added while airborne */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = "0"))
	float AirSpread = 30.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = "0"))
	float MaxSpread = 70.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = "0"))
	float SpreadRecoverySpeed = 6.f;
};
