#include "Player/CameraFeelComponent.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/Actor.h"

namespace CameraFeel
{
	/** Kicks are given in degrees but springs work better with an impulse scale */
	constexpr float KickImpulseScale = 30.f;
	constexpr float FOVPunchImpulseScale = 30.f;
	constexpr float BobBlendSpeed = 6.f;
	constexpr float MinBobSpeed = 50.f;
}

UCameraFeelComponent::UCameraFeelComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UCameraFeelComponent::BeginPlay()
{
	Super::BeginPlay();

	Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
	if (!Camera)
	{
		SetComponentTickEnabled(false);
		return;
	}

	BaseLocation = Camera->GetRelativeLocation();
	BaseRotation = Camera->GetRelativeRotation();
	BaseFOV = Camera->FieldOfView;
	CurrentFOV = BaseFOV;
}

void UCameraFeelComponent::AddRecoilKick(const FRotator& Kick, float FOVKick)
{
	KickPitch.AddImpulse(Kick.Pitch * CameraFeel::KickImpulseScale);
	KickYaw.AddImpulse(FMath::FRandRange(-Kick.Yaw, Kick.Yaw) * CameraFeel::KickImpulseScale);
	KickRoll.AddImpulse(FMath::FRandRange(-Kick.Roll, Kick.Roll) * CameraFeel::KickImpulseScale);
	FOVPunch.AddImpulse(FOVKick * CameraFeel::FOVPunchImpulseScale);
}

void UCameraFeelComponent::AddDip(float Impulse)
{
	Dip.AddImpulse(Impulse);
}

void UCameraFeelComponent::AddRollImpulse(float Impulse)
{
	KickRoll.AddImpulse(Impulse);
}

float UCameraFeelComponent::GetFOVRatio() const
{
	return BaseFOV > 0.f ? CurrentFOV / BaseFOV : 1.f;
}

void UCameraFeelComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Camera)
	{
		return;
	}

	KickPitch.Update(DeltaTime, KickStiffness, KickDamping);
	KickYaw.Update(DeltaTime, KickStiffness, KickDamping);
	KickRoll.Update(DeltaTime, KickStiffness, KickDamping);
	FOVPunch.Update(DeltaTime, KickStiffness, KickDamping);
	Dip.Update(DeltaTime, DipStiffness, DipDamping);

	UpdateFOV(DeltaTime);
	UpdateTransform(DeltaTime);
}

void UCameraFeelComponent::UpdateFOV(float DeltaTime)
{
	float TargetFOV = BaseFOV;

	if (State.bAiming)
	{
		TargetFOV = State.AimFOV;
	}
	else if (State.bSliding)
	{
		TargetFOV += SlideFOVBonus;
	}
	else
	{
		TargetFOV += SprintFOVBonus * State.SprintAlpha;
	}

	const float Speed = State.bAiming ? State.AimSpeed : FOVInterpSpeed;
	CurrentFOV = FMath::FInterpTo(CurrentFOV, TargetFOV, DeltaTime, Speed);

	Camera->SetFieldOfView(CurrentFOV + FOVPunch.Value);
}

void UCameraFeelComponent::UpdateTransform(float DeltaTime)
{
	// Roll into strafes and slides
	const float TargetRoll = State.bSliding ? -SlideRoll : State.StrafeInput * StrafeRoll * (State.bAiming ? 0.3f : 1.f);
	CurrentRoll = FMath::FInterpTo(CurrentRoll, TargetRoll, DeltaTime, RollInterpSpeed);

	// Head bob only while running on the ground, faded so it never pops
	const bool bShouldBob = State.bGrounded && !State.bSliding && State.Speed > CameraFeel::MinBobSpeed;
	BobWeight = FMath::FInterpTo(BobWeight, bShouldBob ? (State.bAiming ? 0.25f : 1.f) : 0.f, DeltaTime, CameraFeel::BobBlendSpeed);
	BobPhase += State.Speed * DeltaTime * BobFrequency / 100.f * UE_TWO_PI;
	BobPhase = FMath::Fmod(BobPhase, 2.f * UE_TWO_PI);

	const float SpeedScale = FMath::Clamp(State.Speed / 600.f, 0.f, 1.5f);
	const FVector Bob(
		0.f,
		FMath::Sin(BobPhase * 0.5f) * BobHorizontalAmplitude * SpeedScale,
		-FMath::Abs(FMath::Sin(BobPhase * 0.5f)) * BobVerticalAmplitude * SpeedScale);

	Camera->SetRelativeLocation(BaseLocation + Bob * BobWeight - FVector(0.f, 0.f, Dip.Value));

	const FRotator Offset(KickPitch.Value, KickYaw.Value, CurrentRoll + KickRoll.Value);
	Camera->SetRelativeRotation(BaseRotation + Offset);
}
