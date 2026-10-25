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

	// ---------------------------------------------------------------------------------------------
	// Firing

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Firing")
	EWeaponFireMode FireMode = EWeaponFireMode::Single;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Firing", meta = (ClampMin = "1"))
	float RoundsPerMinute = 60.f;

	/** Maximum distance of the hitscan trace, in cm */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Firing", meta = (ClampMin = "0", Units = "cm"))
	float MaxRange = 3000.f;

	// ---------------------------------------------------------------------------------------------
	// Animation

	/** Anim layer linked to the character mesh while this weapon is held */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TSubclassOf<UAnimInstance> AnimLayerClass;

	/** Character mesh socket the weapon is attached to */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	FName AttachSocketName = TEXT("WeaponSocket");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> CharacterFireMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> EquipMontage;

	// ---------------------------------------------------------------------------------------------
	// Feedback

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<USoundBase> FireSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback")
	TSubclassOf<UCameraShakeBase> FireCameraShake;

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

	// ---------------------------------------------------------------------------------------------
	// Crosshair spread (screen space, in pixels)

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = "0"))
	float BaseSpread = 8.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = "0"))
	float SpreadPerShot = 14.f;

	/** Extra spread added while the owner moves at full sprint speed */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = "0"))
	float MovementSpread = 25.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = "0"))
	float MaxSpread = 70.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = "0"))
	float SpreadRecoverySpeed = 6.f;
};
