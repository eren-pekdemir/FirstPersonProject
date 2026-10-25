#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GunBase.generated.h"

class APlayerController;
class UStaticMeshComponent;
class UWeaponDataAsset;

/**
 *  Hitscan weapon that can lie in the world, be picked up and be fired by a player.
 *  Handles fire rate, recoil, crosshair spread, slide kick and all firing FX.
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

	/** Switches between being held by a character and lying in the world (collision, ticking, firing state) */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void SetHeld(bool bNewHeld);

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsHeld() const { return bIsHeld; }

	/** Current crosshair gap in pixels */
	UFUNCTION(BlueprintPure, Category = "Crosshair")
	float GetCurrentSpread() const { return CurrentSpread; }

	const UWeaponDataAsset* GetWeaponData() const { return WeaponDataAsset; }

	const FText& GetPickupPromptText() const { return PickupPromptText; }

	/** Main weapon mesh. Also provides the muzzle and left hand IK sockets. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

protected:

	virtual void BeginPlay() override;

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

	/** Fires a single shot: trace, FX, recoil and spread */
	void Fire();

	/** Traces from the owner's view through the crosshair. Returns the end point of the shot. */
	FVector TraceFromView(const APlayerController& PlayerController, FHitResult& OutHit) const;

	void PlayFireFeedback(APlayerController& PlayerController);
	void SpawnImpact(const FHitResult& Hit) const;
	void SpawnTracer(const FVector& ShotEnd) const;

	void ApplyRecoil();
	void UpdateRecoil(float DeltaTime);
	void ResetRecoil();

	void UpdateSpread(float DeltaTime);

	void KickSlide();
	void UpdateSlide(float DeltaTime);

	void UpdateMuzzleSmoke(float DeltaTime);

	FTimerHandle AutoFireTimer;

	bool bIsHeld = false;

	/** World time of the last shot, used to enforce the fire rate between trigger presses */
	float LastFireTime = TNumericLimits<float>::Lowest();

	float CurrentSpread = 0.f;

	FRotator RecoilTarget = FRotator::ZeroRotator;
	FRotator RecoilCurrent = FRotator::ZeroRotator;
	int32 RecoilIndex = 0;
	float TimeSinceLastShot = 0.f;

	bool bMuzzleSmokePending = false;
	float MuzzleSmokeTimer = 0.f;

	FVector SlideRestLocation = FVector::ZeroVector;
};
