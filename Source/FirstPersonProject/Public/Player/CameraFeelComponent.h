#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CameraFeelComponent.generated.h"

class UCameraComponent;

/** Per-frame movement state the owner feeds into the camera */
struct FCameraFeelState
{
	/** 0..1 how much the owner is sprinting */
	float SprintAlpha = 0.f;

	/** Horizontal speed, used for head bob */
	float Speed = 0.f;

	/** -1..1 strafe input, used for camera roll */
	float StrafeInput = 0.f;

	bool bGrounded = true;
	bool bSliding = false;
	bool bAiming = false;
	float AimFOV = 65.f;
	float AimSpeed = 14.f;
};

/** Critically damped spring used for procedural camera offsets */
struct FCameraSpring
{
	float Value = 0.f;
	float Velocity = 0.f;

	void AddImpulse(float Impulse) { Velocity += Impulse; }

	void Update(float DeltaTime, float Stiffness, float Damping)
	{
		// Sub-step to keep the spring stable on frame spikes
		constexpr float MaxStep = 1.f / 120.f;
		while (DeltaTime > 0.f)
		{
			const float Step = FMath::Min(DeltaTime, MaxStep);
			Velocity += (-Stiffness * Value - Damping * Velocity) * Step;
			Value += Velocity * Step;
			DeltaTime -= Step;
		}
	}
};

/**
 *  Procedural first person camera feel: FOV (sprint, slide, ADS, shot punch), roll (strafe, slide),
 *  recoil kick, landing / mantle dips and a subtle head bob. Works on the owner's camera component.
 */
UCLASS(ClassGroup = (Camera), meta = (BlueprintSpawnableComponent))
class FIRSTPERSONPROJECT_API UCameraFeelComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UCameraFeelComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void SetState(const FCameraFeelState& NewState) { State = NewState; }

	/** Rotational kick that springs back (pitch up, yaw, roll) plus a FOV punch */
	void AddRecoilKick(const FRotator& Kick, float FOVKick);

	/** Vertical camera dip in cm/s, positive pushes the camera down (landing, mantle) */
	void AddDip(float Impulse);

	/** Roll impulse in deg/s (mantle, vault) */
	void AddRollImpulse(float Impulse);

	/** Current FOV divided by the base FOV, used to scale look sensitivity while aiming */
	float GetFOVRatio() const;

protected:

	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, Category = "FOV", meta = (Units = "deg"))
	float SprintFOVBonus = 8.f;

	UPROPERTY(EditAnywhere, Category = "FOV", meta = (Units = "deg"))
	float SlideFOVBonus = 14.f;

	UPROPERTY(EditAnywhere, Category = "FOV", meta = (ClampMin = "0"))
	float FOVInterpSpeed = 8.f;

	UPROPERTY(EditAnywhere, Category = "Roll", meta = (Units = "deg"))
	float StrafeRoll = 1.2f;

	UPROPERTY(EditAnywhere, Category = "Roll", meta = (Units = "deg"))
	float SlideRoll = 4.f;

	UPROPERTY(EditAnywhere, Category = "Roll", meta = (ClampMin = "0"))
	float RollInterpSpeed = 7.f;

	UPROPERTY(EditAnywhere, Category = "Bob", meta = (Units = "cm"))
	float BobVerticalAmplitude = 0.45f;

	UPROPERTY(EditAnywhere, Category = "Bob", meta = (Units = "cm"))
	float BobHorizontalAmplitude = 0.3f;

	/** Bob cycles per 100 cm travelled */
	UPROPERTY(EditAnywhere, Category = "Bob", meta = (ClampMin = "0"))
	float BobFrequency = 0.55f;

	UPROPERTY(EditAnywhere, Category = "Springs", meta = (ClampMin = "0"))
	float KickStiffness = 260.f;

	UPROPERTY(EditAnywhere, Category = "Springs", meta = (ClampMin = "0"))
	float KickDamping = 26.f;

	UPROPERTY(EditAnywhere, Category = "Springs", meta = (ClampMin = "0"))
	float DipStiffness = 140.f;

	UPROPERTY(EditAnywhere, Category = "Springs", meta = (ClampMin = "0"))
	float DipDamping = 18.f;

private:

	void UpdateFOV(float DeltaTime);
	void UpdateTransform(float DeltaTime);

	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;

	FCameraFeelState State;

	FVector BaseLocation = FVector::ZeroVector;
	FRotator BaseRotation = FRotator::ZeroRotator;
	float BaseFOV = 90.f;
	float CurrentFOV = 90.f;
	float CurrentRoll = 0.f;
	float BobPhase = 0.f;
	float BobWeight = 0.f;

	FCameraSpring KickPitch;
	FCameraSpring KickYaw;
	FCameraSpring KickRoll;
	FCameraSpring FOVPunch;
	FCameraSpring Dip;
};
