#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponDataAsset.generated.h"

class UAnimInstance;
class UAnimMontage;
class UAnimSequence;
class USkeletalMesh;
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

/** A part of the view mesh thrown back by each shot (pistol slide, shotgun pump) */
USTRUCT(BlueprintType)
struct FWeaponPartKick
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Bone;

	/** Full travel of the part, in the view mesh's own axes */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (Units = "cm"))
	FVector Offset = FVector::ZeroVector;

	/** Time after the shot before the part moves (working a pump) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0", Units = "s"))
	float Delay = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0", Units = "s"))
	float BackTime = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0", Units = "s"))
	float HoldTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0", Units = "s"))
	float ReturnTime = 0.08f;

	/** Stays back while the magazine is empty (slide lock) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bLockOnEmpty = false;
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

	/** Full damage up to this distance, then it drops toward MinDamageScale at DamageFalloffEnd. Zero: no falloff. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0", Units = "cm"))
	float DamageFalloffStart = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0", Units = "cm"))
	float DamageFalloffEnd = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0", ClampMax = "1"))
	float MinDamageScale = 1.f;

	/** Within this distance damage is multiplied by PointBlankMultiplier (shotgun blasts at close range). Zero: off. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0", Units = "cm"))
	float PointBlankRange = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "1"))
	float PointBlankMultiplier = 1.f;

	/** Damage multiplier at Distance from the shooter */
	float GetRangeDamageScale(float Distance) const
	{
		if (PointBlankRange > 0.f && Distance <= PointBlankRange)
		{
			return PointBlankMultiplier;
		}
		if (DamageFalloffEnd <= DamageFalloffStart || DamageFalloffStart <= 0.f)
		{
			return 1.f;
		}
		const float Alpha = FMath::Clamp((Distance - DamageFalloffStart) / (DamageFalloffEnd - DamageFalloffStart), 0.f, 1.f);
		return FMath::Lerp(1.f, MinDamageScale, Alpha);
	}

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
	// First person view (arms and weapon as the player sees them, see UViewmodelComponent)

	/** Animated weapon shown in the hands and in the world. Without it the static meshes of the weapon Blueprint are used. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person")
	TObjectPtr<USkeletalMesh> ViewMesh;

	/** Anim Blueprint of the view mesh, playing the weapon montages (bolt, magazine, pump) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person")
	TSubclassOf<UAnimInstance> ViewAnimClass;

	/** View mesh transform inside the weapon actor: turns and moves a model so its grip sits where the arms expect it */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person")
	FTransform ViewMeshOffset = FTransform::Identity;

	/** View mesh rotation (in its own axes) that lays the weapon on its side when it lies on the ground */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person")
	FRotator WorldRestRotation = FRotator(0.f, 0.f, 90.f);

	/** Bones of the view mesh that are hidden, for parts of a model that are not part of the weapon (loose shells) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person")
	TArray<FName> ViewHiddenBones;

	/** Pickup box half size around the view mesh. Zero fits the mesh bounds (which include hidden bones). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person", meta = (Units = "cm"))
	FVector PickupExtent = FVector::ZeroVector;

	/** Turn of the first person muzzle flash relative to the view, for flash effects authored along another axis */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person")
	FRotator ViewFlashRotation = FRotator::ZeroRotator;

	/** Socket of the view mesh the muzzle flash and tracers start from */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person")
	FName ViewMuzzleSocket = TEXT("Muzzle");

	/** Parts of the view mesh moved by each shot, for view meshes without an Anim Blueprint */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person")
	TArray<FWeaponPartKick> ViewPartKicks;

	/** Arms Anim Blueprint for this weapon. Empty uses the character's default arms animation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person|Arms")
	TSubclassOf<UAnimInstance> ArmsAnimClass;

	/** The arms Anim Blueprint has an aim pose for this weapon. Off: aiming lines up the weapon's Sight socket in code. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person|Arms")
	bool bArmsAimAnimation = true;

	/**
	 *  When the view mesh has a Sight socket, aiming centers it on the screen (X axis = line of sight, Z = up).
	 *  This is the eye to sight distance then; zero keeps the distance the aim animation gives.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person|Arms", meta = (Units = "cm"))
	float AimSightDistance = 26.f;

	/** Camera space correction of the arms for this weapon, when its animations were made for another eye height */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person|Arms", meta = (Units = "cm"))
	FVector ArmsOffset = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person|Arms")
	TObjectPtr<UAnimMontage> ArmsDrawMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person|Arms")
	TObjectPtr<UAnimMontage> ArmsFireMontage;

	/** Fire animation while aiming down sights. Empty uses ArmsFireMontage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person|Arms")
	TObjectPtr<UAnimMontage> ArmsAimFireMontage;

	/** Magazine reload of the arms. Its length sets the reload time. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person|Arms")
	TObjectPtr<UAnimMontage> ArmsReloadMontage;

	/** Reload from an empty magazine (chambering a round). Empty uses ArmsReloadMontage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person|Arms")
	TObjectPtr<UAnimMontage> ArmsReloadEmptyMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person|Weapon Mesh")
	TObjectPtr<UAnimMontage> WeaponFireMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person|Weapon Mesh")
	TObjectPtr<UAnimMontage> WeaponReloadMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person|Weapon Mesh")
	TObjectPtr<UAnimMontage> WeaponReloadEmptyMontage;

	/** Small procedural kick added to the fire animation: X pushes the weapon back, Z up */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person")
	FVector ViewKickLocation = FVector(-1.2f, 0.f, 0.2f);

	/** Procedural kick rotation: pitch climbs, yaw and roll are random in [-value, value] */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person")
	FRotator ViewKickRotation = FRotator(1.2f, 0.4f, 0.8f);

	/**
	 *  Spring of the procedural kick. Low stiffness lets the weapon flip and settle back slowly (heavy, punchy);
	 *  high stiffness snaps it back at once. Zero uses the viewmodel defaults.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person", meta = (ClampMin = "0"))
	float ViewKickStiffness = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person", meta = (ClampMin = "0"))
	float ViewKickDamping = 0.f;

	/** Kick played CycleSoundDelay after each shot, for working the action (pump, bolt). Zero for none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person")
	FVector ViewCycleKickLocation = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person")
	FRotator ViewCycleKickRotation = FRotator::ZeroRotator;

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

	/** Strength of FireCameraShake for this weapon */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback", meta = (ClampMin = "0"))
	float FireCameraShakeScale = 1.f;

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
