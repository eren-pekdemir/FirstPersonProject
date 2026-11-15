#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GunBase.generated.h"

class APlayerController;
class USoundBase;
class UStaticMeshComponent;
class UWeaponDataAsset;

/** Result of one trigger pull, aggregated over all pellets */
struct FGunShotResult
{
	/** Total damage dealt to damageable actors */
	float Damage = 0.f;

	/** World location of the first damaging hit, used for floating damage numbers */
	FVector HitLocation = FVector::ZeroVector;

	bool bHitDamageable = false;
	bool bHeadshot = false;
	bool bKilled = false;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnGunFired, const AGunBase& /*Gun*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnGunHit, const FGunShotResult& /*Result*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnGunEmpty, AGunBase& /*Gun*/);

/**
 *  Hitscan weapon that can lie in the world, be picked up and be fired by a player.
 *  Handles fire rate, ammo, reloading, recoil, spread, damage and all firing FX.
 *  Per-weapon tuning lives in UWeaponDataAsset.
 */
UCLASS()
class FIRSTPERSONPROJECT_API AGunBase : public AActor
{
	GENERATED_BODY()

public:

	AGunBase();

	virtual void Tick(float DeltaTime) override;

	/** Starts firing. Automatic weapons keep firing until StopFire is called. */
	void StartFire();

	void StopFire();

	/** True if a shot could be fired right now (ammo, not reloading or equipping) */
	bool CanFire() const;

	/** Starts a reload. Returns false if the weapon is full, has no reserve ammo or is already reloading. */
	bool StartReload();

	void CancelReload();

	/** Called by the owner every time the weapon is drawn */
	void OnEquipped();

	/** Hides the weapon while it is carried but not in the hands */
	void SetHolstered(bool bNewHolstered);

	/** Switches between being held by a character and lying in the world (collision, ticking, firing state) */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void SetHeld(bool bNewHeld);

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsHeld() const { return bIsHeld; }

	void SetAiming(bool bNewAiming) { bIsAiming = bNewAiming; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsAiming() const { return bIsAiming; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsReloading() const { return bIsReloading; }

	/** 0..1 progress of the current reload */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetReloadProgress() const;

	UFUNCTION(BlueprintPure, Category = "Weapon")
	int32 GetAmmoInMagazine() const { return AmmoInMagazine; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	int32 GetReserveAmmo() const { return ReserveAmmo; }

	int32 GetMagazineSize() const;

	/** Adds reserve ammo, clamped to the weapon maximum. Returns the amount actually added. */
	int32 AddReserveAmmo(int32 Amount);

	/** Current crosshair gap in pixels */
	UFUNCTION(BlueprintPure, Category = "Crosshair")
	float GetCurrentSpread() const;

	const UWeaponDataAsset* GetWeaponData() const { return WeaponDataAsset; }

	FText GetDisplayName() const;

	const FText& GetPickupPromptText() const { return PickupPromptText; }

	FOnGunFired OnFired;
	FOnGunHit OnHit;
	FOnGunEmpty OnEmpty;

	/** Main weapon mesh. Also provides the muzzle and left hand IK sockets. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

protected:

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Moving part kicked back on every shot (pistol slide, rifle bolt...) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SlideMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UWeaponDataAsset> WeaponDataAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction")
	FText PickupPromptText = NSLOCTEXT("Weapon", "PickupPrompt", "Pick Up");

	/** How far the slide is pushed back along -X on each shot, in cm */
	UPROPERTY(EditDefaultsOnly, Category = "Slide", meta = (Units = "cm"))
	float SlideKickBack = 3.f;

	UPROPERTY(EditDefaultsOnly, Category = "Slide", meta = (ClampMin = "0"))
	float SlideReturnSpeed = 40.f;

	UPROPERTY(EditDefaultsOnly, Category = "Feedback", meta = (ClampMin = "0"))
	float ShootParticleScale = 1.f;

private:

	/** Fires a single shot: traces, damage, FX, recoil and spread */
	void Fire();

	void DryFire();

	/** Traces every pellet from the owner's view and applies damage */
	FGunShotResult FireTraces(const APlayerController& PlayerController, TArray<FVector>& OutShotEnds);

	/** Applies damage for one pellet hit and merges it into the shot result */
	void ApplyHitDamage(const FHitResult& Hit, const FVector& ShotDirection, FGunShotResult& InOutResult);

	void PlayFireFeedback(APlayerController& PlayerController);
	void SpawnImpact(const FHitResult& Hit) const;
	void SpawnTracer(const TArray<FVector>& ShotEnds) const;

	void PlayWeaponSound(USoundBase* Sound) const;
	void PlayRandomSound(const TArray<TObjectPtr<USoundBase>>& Sounds) const;

	/** Schedules Sound to play after Delay. The sound is cancelled with the reload. */
	void ScheduleReloadSound(USoundBase* Sound, float Delay);

	void FinishMagazineReload();
	void InsertRound();
	void EndReload();

	void ApplyRecoil();
	void UpdateRecoil(float DeltaTime);
	void ResetRecoil();

	void UpdateSpread(float DeltaTime);

	void KickSlide();
	void UpdateSlide(float DeltaTime);

	void UpdateMuzzleSmoke(float DeltaTime);

	FTimerHandle AutoFireTimer;
	FTimerHandle ReloadTimer;
	FTimerHandle CycleSoundTimer;
	TArray<FTimerHandle> ReloadSoundTimers;

	bool bIsHeld = false;
	bool bIsAiming = false;
	bool bIsReloading = false;
	bool bHasAmmoState = false;

	int32 AmmoInMagazine = 0;
	int32 ReserveAmmo = 0;

	float ReloadStartTime = 0.f;
	float ReloadDuration = 0.f;

	/** World time after which the weapon can fire once it has been equipped */
	float EquipReadyTime = 0.f;

	/** World time of the last shot, used to enforce the fire rate between trigger presses */
	float LastFireTime = TNumericLimits<float>::Lowest();

	float CurrentSpread = 0.f;

	FRotator RecoilTarget = FRotator::ZeroRotator;
	FRotator RecoilCurrent = FRotator::ZeroRotator;
	int32 RecoilIndex = 0;
	float TimeSinceLastShot = 0.f;

	/** Vertical recoil still to be pulled back after the player stopped firing */
	float RecoilRecoveryRemaining = 0.f;

	bool bMuzzleSmokePending = false;
	float MuzzleSmokeTimer = 0.f;

	FVector SlideRestLocation = FVector::ZeroVector;
};
