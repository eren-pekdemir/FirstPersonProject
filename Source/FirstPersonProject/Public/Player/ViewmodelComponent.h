#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ViewmodelComponent.generated.h"

class AGunBase;
class UAnimInstance;
class UAnimMontage;
class UCameraComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UWeaponDataAsset;

/** Damped spring on three axes, used for the procedural layer on top of the arm animations */
struct FViewSpring
{
	FVector Value = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;

	void AddImpulse(const FVector& Impulse) { Velocity += Impulse; }

	void Update(float DeltaTime, float Stiffness, float Damping, const FVector& Target = FVector::ZeroVector)
	{
		// Sub-step so the spring stays stable at low frame rates
		constexpr float MaxStep = 1.f / 120.f;
		while (DeltaTime > 0.f)
		{
			const float Step = FMath::Min(DeltaTime, MaxStep);
			const FVector Acceleration = (Target - Value) * Stiffness - Velocity * Damping;
			Velocity += Acceleration * Step;
			Value += Velocity * Step;
			DeltaTime -= Step;
		}
	}

	void Reset() { Value = Velocity = FVector::ZeroVector; }
};

/** What the owner is doing this frame, fed every tick by the character */
struct FViewmodelState
{
	float Speed = 0.f;
	FVector2D MoveInput = FVector2D::ZeroVector;
	bool bGrounded = true;
	bool bSliding = false;
	bool bAiming = false;
	/** Weapon lowered out of the way (mantling, vaulting) */
	bool bLowered = false;
	float AimSpeed = 8.f;
};

/** Arms meshes and animation used when no weapon overrides them */
USTRUCT(BlueprintType)
struct FFirstPersonArmsSetup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<USkeletalMesh> ArmsMesh;

	/** Optional second mesh following the arms pose (gloves, sleeves) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<USkeletalMesh> GlovesMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<UAnimInstance> AnimClass;

	/** Arms transform relative to the camera, where the animations expect the eye to be */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform CameraOffset = FTransform(FRotator(0.f, -90.f, 0.f), FVector(7.f, 0.f, -162.6f));

	/** Bool variable of the arms Anim Blueprint switched on while aiming down sights */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName AimVariableName = TEXT("ADS");
};

/**
 *  Drives the first person arms.
 *  The arms are animated by an Anim Blueprint (locomotion, aim, jump) and play the weapon's montages
 *  (draw, fire, reload). On top of the animation a light procedural layer adds weight: the weapon lags
 *  behind the view, settles after landings and kicks slightly on every shot.
 *  First person rendering (own FOV and depth scale) keeps the weapon from clipping into walls.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class FIRSTPERSONPROJECT_API UViewmodelComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UViewmodelComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Sets up the arms on the camera. Call once from the owner's BeginPlay. */
	void Initialize(USkeletalMeshComponent* InArms, USkeletalMeshComponent* InGloves, UCameraComponent* InCamera,
		USkeletalMeshComponent* BodyMesh, const FFirstPersonArmsSetup& InSetup);

	/** Shows the arms holding Gun and plays its draw animation */
	void SetWeapon(AGunBase* Gun);

	/** Hides the arms (unarmed) */
	void ClearWeapon();

	void SetState(const FViewmodelState& NewState) { State = NewState; }

	/** Fire animation and kick for one shot */
	void OnFired(bool bAiming);

	/** Downward bounce, for landings and mantles */
	void AddDip(float Strength);

	USkeletalMeshComponent* GetArms() const { return Arms; }

	/** Spring and pose values, for the feel test log */
	FString GetDebugString() const;

	/** Bone or socket of the arms the weapon is attached to */
	static FName GetWeaponSocket(const UWeaponDataAsset* WeaponData);

protected:

	virtual void BeginPlay() override;

	/** Field of view of the arms and weapon, independent from the camera FOV so zooming does not distort them */
	UPROPERTY(EditAnywhere, Category = "Viewmodel", meta = (Units = "deg", ClampMin = "30", ClampMax = "120"))
	float ViewmodelFOV = 80.f;

	/** Depth scale of first person geometry. Lower values push it closer to the camera so it never clips into walls. */
	UPROPERTY(EditAnywhere, Category = "Viewmodel", meta = (ClampMin = "0.05", ClampMax = "1"))
	float ViewmodelDepthScale = 0.6f;

	/** Pivot of the procedural rotations in camera space, roughly where the weapon is held */
	UPROPERTY(EditAnywhere, Category = "Viewmodel", meta = (Units = "cm"))
	FVector ProceduralPivot = FVector(30.f, 8.f, -14.f);

	// Weight: the weapon trails the view and the body ------------------------------------------------

	/** Degrees of weapon lag per 100 deg/s of view rotation */
	UPROPERTY(EditAnywhere, Category = "Weight")
	float LookSwayRotation = 2.4f;

	/** Centimeters of weapon lag per 100 deg/s of view rotation */
	UPROPERTY(EditAnywhere, Category = "Weight", meta = (Units = "cm"))
	float LookSwayLocation = 0.6f;

	UPROPERTY(EditAnywhere, Category = "Weight", meta = (Units = "deg"))
	float MaxSwayRotation = 6.f;

	/** Weapon offset per unit of strafe / forward input */
	UPROPERTY(EditAnywhere, Category = "Weight", meta = (Units = "cm"))
	float MoveSwayLocation = 0.7f;

	UPROPERTY(EditAnywhere, Category = "Weight", meta = (Units = "deg"))
	float MoveSwayRoll = 2.5f;

	/** Low stiffness and damping make the weapon feel heavy: it follows the view late and settles slowly */
	UPROPERTY(EditAnywhere, Category = "Weight")
	float SwayStiffness = 60.f;

	UPROPERTY(EditAnywhere, Category = "Weight")
	float SwayDamping = 12.f;

	/** Share of the sway kept while aiming down sights */
	UPROPERTY(EditAnywhere, Category = "Weight", meta = (ClampMin = "0", ClampMax = "1"))
	float AimSwayScale = 0.3f;

	// Kick ------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, Category = "Kick")
	float KickStiffness = 420.f;

	UPROPERTY(EditAnywhere, Category = "Kick")
	float KickDamping = 26.f;

	/** Kicks are given in cm / degrees, springs need an impulse */
	UPROPERTY(EditAnywhere, Category = "Kick")
	float KickImpulseScale = 30.f;

	/** Share of the kick kept while aiming down sights */
	UPROPERTY(EditAnywhere, Category = "Kick", meta = (ClampMin = "0", ClampMax = "1"))
	float AimKickScale = 0.7f;

	/** Roll and drop of the weapon during a reload that has no arm animation (shell by shell) */
	UPROPERTY(EditAnywhere, Category = "Reload")
	FRotator ProceduralReloadTilt = FRotator(10.f, -6.f, -18.f);

	UPROPERTY(EditAnywhere, Category = "Reload", meta = (Units = "cm"))
	FVector ProceduralReloadOffset = FVector(-2.f, -2.f, -4.f);

private:

	/** Delayed kick of working the action after a shot (pump, bolt) */
	void PlayCycleKick();

	FTimerHandle CycleKickTimer;

	/** Last sight alignment, kept while a montage plays */
	FTransform SightAlignment = FTransform::Identity;
	bool bHasSightAlignment = false;

	void UpdateSway(float DeltaTime);
	void UpdateReload(float DeltaTime);
	void UpdateAnimVariables();
	void ApplyTransform();

	/** Camera space correction that brings the weapon's Sight socket onto the view axis (aiming without an aim animation) */
	FTransform ComputeSightAlignment(const FTransform& ArmsToCamera) const;

	/** Plays a montage on the arms. Rate scales it, or fits it into FitDuration when that is positive. */
	void PlayArmsMontage(UAnimMontage* Montage, float FitDuration = 0.f) const;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Arms;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Gloves;

	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY()
	TObjectPtr<AGunBase> Gun;

	FFirstPersonArmsSetup Setup;
	FViewmodelState State;

	FViewSpring KickLocation;
	FViewSpring KickRotation;
	FViewSpring SwayLocation;
	FViewSpring SwayRotation;
	FViewSpring DipSpring;

	FRotator LastViewRotation = FRotator::ZeroRotator;

	float AimAlpha = 0.f;
	float LoweredPose = 0.f;
	float ProceduralReloadPose = 0.f;
	int32 LastAmmoInMagazine = 0;
	bool bWasReloading = false;
	bool bReloadUsesMontage = false;
};
