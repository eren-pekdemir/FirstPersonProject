// Movement half of APlayerCharacter: sprint, jump (coyote time + buffering), crouch / slide,
// traversal hooks, landing feedback, footsteps and the camera feel state.

#include "Player/PlayerCharacter.h"

#include "Camera/CameraShakeBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Gun/GunBase.h"
#include "Kismet/GameplayStatics.h"
#include "Player/CameraFeelComponent.h"

namespace PlayerMovement
{
	/** Sprint only works while pushing mostly forward, like in Apex */
	constexpr float MinSprintForwardInput = 0.3f;

	/** Below this speed a slide turns into a crouch */
	constexpr float SlideEndSpeedMargin = 40.f;

	constexpr float CrouchOffsetInterpSpeed = 12.f;
	constexpr float SprintAlphaInterpSpeed = 6.f;

	constexpr float MinFootstepSpeed = 120.f;
	constexpr float SprintStrideMultiplier = 1.3f;

	/** Falls slower than this give no landing feedback */
	constexpr float MinLandingFeedbackSpeed = 350.f;

	constexpr float LandingDipStrength = 260.f;
	constexpr float SlideStartDip = 90.f;
	constexpr float MantleDip = 110.f;
	constexpr float MantleRollImpulse = 35.f;
	constexpr float VaultLandingDip = 70.f;
}

// -------------------------------------------------------------------------------------------------
// Per frame update

void APlayerCharacter::UpdateMovement(float DeltaTime)
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();

	if (Movement->IsMovingOnGround())
	{
		LastGroundedTime = GetWorld()->GetTimeSeconds();
		bJumpedSinceGrounded = false;
	}

	UpdateSprint(DeltaTime);
	UpdateSlide(DeltaTime);
	UpdateCrouchOffset(DeltaTime);
	UpdateFootsteps(DeltaTime);
}

void APlayerCharacter::UpdateSprint(float DeltaTime)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();

	// Holding the trigger or aiming cancels the sprint, as does moving backwards or sideways only
	const bool bMovingForward = LastMoveInput.Y > PlayerMovement::MinSprintForwardInput;
	const bool bTriggerBlocksSprint = bWantsToFire && CurrentGun;
	bIsSprinting = bWantsToSprint && bMovingForward && !bTriggerBlocksSprint && !bWantsToAim && !bIsCrouched && !bIsSliding;

	float TargetSpeed = bIsSprinting ? SprintSpeed : WalkSpeed;
	if (bIsAiming && CurrentGun && CurrentGun->GetWeaponData())
	{
		TargetSpeed = WalkSpeed * CurrentGun->GetWeaponData()->AimMoveSpeedMultiplier;
	}

	// Don't drag the speed down in the air, it would kill the momentum of slide jumps
	if (Movement->IsMovingOnGround())
	{
		Movement->MaxWalkSpeed = FMath::FInterpTo(Movement->MaxWalkSpeed, TargetSpeed, DeltaTime, SprintInterpSpeed);
	}

	const bool bVisiblySprinting = bIsSprinting && GetVelocity().Size2D() > WalkSpeed;
	SprintAlpha = FMath::FInterpTo(SprintAlpha, bVisiblySprinting ? 1.f : 0.f, DeltaTime, PlayerMovement::SprintAlphaInterpSpeed);
}

void APlayerCharacter::UpdateCameraFeel()
{
	if (!CameraFeel)
	{
		return;
	}

	FCameraFeelState State;
	State.SprintAlpha = SprintAlpha;
	State.Speed = GetVelocity().Size2D();
	State.StrafeInput = LastMoveInput.X;
	State.bGrounded = GetCharacterMovement()->IsMovingOnGround();
	State.bSliding = bIsSliding;
	State.bAiming = bIsAiming;

	if (CurrentGun && CurrentGun->GetWeaponData())
	{
		State.AimFOV = CurrentGun->GetWeaponData()->AimFOV;
		State.AimSpeed = CurrentGun->GetWeaponData()->AimSpeed;
	}

	CameraFeel->SetState(State);
}

// -------------------------------------------------------------------------------------------------
// Jumping

void APlayerCharacter::OnJumpPressed()
{
	LastJumpPressTime = GetWorld()->GetTimeSeconds();

	if (IsTraversing())
	{
		return;
	}

	// Jumping into a ledge climbs it
	if (Traversal->TryTraversal(true))
	{
		LastJumpPressTime = -1.f;
		return;
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();

	if (bIsSliding)
	{
		// Slide jump: keeps all the slide speed
		StopSlide();
	}

	if (bIsCrouched)
	{
		Movement->UnCrouch(false);
	}

	if (Movement->IsMovingOnGround())
	{
		Jump();
		bJumpedSinceGrounded = true;
		LastJumpPressTime = -1.f;
		PlaySoundAtFeet(JumpSound, 0.6f);
		return;
	}

	// Coyote time: just walked off a ledge, still allow the jump
	const bool bInCoyoteTime = GetWorld()->GetTimeSeconds() - LastGroundedTime <= CoyoteTime;
	if (Movement->IsFalling() && !bJumpedSinceGrounded && bInCoyoteTime)
	{
		LaunchCharacter(FVector(0.f, 0.f, Movement->JumpZVelocity), false, true);
		bJumpedSinceGrounded = true;
		LastJumpPressTime = -1.f;
		PlaySoundAtFeet(JumpSound, 0.6f);
	}
}

void APlayerCharacter::OnJumpReleased()
{
	StopJumping();
}

bool APlayerCharacter::CanJumpInternal_Implementation() const
{
	return !IsTraversing() && Super::CanJumpInternal_Implementation();
}

void APlayerCharacter::Landed(const FHitResult& Hit)
{
	const float FallSpeed = -GetVelocity().Z;

	Super::Landed(Hit);

	bJumpedSinceGrounded = false;

	if (FallSpeed > PlayerMovement::MinLandingFeedbackSpeed)
	{
		const float Strength = FMath::Clamp(FallSpeed / HardLandingSpeed, 0.f, 1.f);

		if (CameraFeel)
		{
			CameraFeel->AddDip(Strength * PlayerMovement::LandingDipStrength);
		}

		PlaySoundAtFeet(LandSound, FMath::Lerp(0.4f, 1.f, Strength));

		APlayerController* PlayerController = GetController<APlayerController>();
		if (PlayerController && LandCameraShake && Strength > 0.5f)
		{
			PlayerController->ClientStartCameraShake(LandCameraShake, Strength);
		}
	}

	// Jump buffered just before touching the ground
	if (LastJumpPressTime >= 0.f && GetWorld()->GetTimeSeconds() - LastJumpPressTime <= JumpBufferTime)
	{
		LastJumpPressTime = -1.f;
		Jump();
		bJumpedSinceGrounded = true;
		return;
	}

	// Holding crouch while landing fast goes straight into a slide
	if (bWantsToCrouch && GetVelocity().Size2D() >= SlideMinStartSpeed)
	{
		StartSlide();
	}
}

void APlayerCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	// Sliding off a ledge ends the slide but keeps the speed
	if (bIsSliding && !GetCharacterMovement()->IsMovingOnGround())
	{
		StopSlide();
	}
}

// -------------------------------------------------------------------------------------------------
// Crouch and slide

void APlayerCharacter::OnCrouchPressed()
{
	bWantsToCrouch = true;

	if (IsTraversing())
	{
		return;
	}

	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const bool bFastEnough = GetVelocity().Size2D() >= SlideMinStartSpeed;

	if (Movement->IsMovingOnGround() && bFastEnough)
	{
		StartSlide();
	}
	else
	{
		Crouch();
	}
}

void APlayerCharacter::OnCrouchReleased()
{
	bWantsToCrouch = false;

	if (bIsSliding)
	{
		StopSlide();
	}

	UnCrouch();
}

void APlayerCharacter::StartSlide()
{
	if (bIsSliding)
	{
		return;
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();

	bIsSliding = true;
	Crouch();

	// No friction, constant braking: the slide decays linearly and slopes keep it going
	Movement->GroundFriction = 0.f;
	Movement->BrakingDecelerationWalking = SlideFriction;
	Movement->MaxAcceleration = SlideSteering;
	Movement->MaxWalkSpeedCrouched = SlideMaxSpeed;

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastSlideTime > SlideBoostCooldown)
	{
		const FVector Direction = GetVelocity().GetSafeNormal2D();
		Movement->Velocity += Direction * SlideBoost;
		Movement->Velocity = Movement->Velocity.GetClampedToMaxSize2D(SlideMaxSpeed);
	}
	LastSlideTime = Now;

	PlaySoundAtFeet(SlideSound, 0.8f);

	if (CameraFeel)
	{
		CameraFeel->AddDip(PlayerMovement::SlideStartDip);
	}
}

void APlayerCharacter::StopSlide()
{
	if (!bIsSliding)
	{
		return;
	}

	bIsSliding = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->GroundFriction = SavedGroundFriction;
	Movement->BrakingDecelerationWalking = SavedBrakingDeceleration;
	Movement->MaxAcceleration = SavedMaxAcceleration;
	Movement->MaxWalkSpeedCrouched = CrouchSpeed;

	if (!bWantsToCrouch)
	{
		UnCrouch();
	}
}

void APlayerCharacter::UpdateSlide(float DeltaTime)
{
	if (!bIsSliding)
	{
		return;
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();

	// Slopes: project gravity on the floor so downhill slides speed up and uphill ones die out
	if (Movement->CurrentFloor.IsWalkableFloor())
	{
		const FVector FloorNormal = Movement->CurrentFloor.HitResult.ImpactNormal;
		const FVector Gravity(0.f, 0.f, Movement->GetGravityZ() * SlideGravityScale);
		const FVector SlopeAcceleration = Gravity - FloorNormal * Gravity.Dot(FloorNormal);
		Movement->Velocity += SlopeAcceleration * DeltaTime;
	}

	// Light steering towards the input
	if (Controller && !LastMoveInput.IsNearlyZero())
	{
		const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
		const FVector InputDirection = YawRotation.RotateVector(FVector(LastMoveInput.Y, LastMoveInput.X, 0.f)).GetSafeNormal();
		const float Speed = Movement->Velocity.Size2D();
		const FVector Steered = (Movement->Velocity.GetSafeNormal2D() + InputDirection * SlideSteering / 1000.f * DeltaTime * 4.f).GetSafeNormal2D();
		Movement->Velocity.X = Steered.X * Speed;
		Movement->Velocity.Y = Steered.Y * Speed;
	}

	Movement->Velocity = Movement->Velocity.GetClampedToMaxSize2D(SlideMaxSpeed);

	if (Movement->Velocity.Size2D() < CrouchSpeed + PlayerMovement::SlideEndSpeedMargin)
	{
		StopSlide();
	}
}

void APlayerCharacter::UpdateCrouchOffset(float DeltaTime)
{
	// The mesh has no crouch animation: lower the whole body (and the camera attached to it)
	// ACharacter rewrites the mesh location (and BaseTranslationOffset) on crouch / uncrouch, so the
	// offset is always applied on top of that base instead of accumulated, which would drift.
	const float TargetOffset = bIsCrouched ? CrouchCameraDrop : 0.f;
	CrouchOffset = FMath::FInterpTo(CrouchOffset, TargetOffset, DeltaTime, PlayerMovement::CrouchOffsetInterpSpeed);

	const FVector DesiredLocation = GetBaseTranslationOffset() - FVector(0.f, 0.f, CrouchOffset);
	if (!GetMesh()->GetRelativeLocation().Equals(DesiredLocation, 0.01f))
	{
		GetMesh()->SetRelativeLocation(DesiredLocation);
	}
}

// -------------------------------------------------------------------------------------------------
// Traversal

void APlayerCharacter::OnTraversalStarted(ETraversalType Type, float Height)
{
	StopSlide();
	StopFiring();

	if (CameraFeel)
	{
		const float RollDirection = FMath::RandBool() ? 1.f : -1.f;
		CameraFeel->AddDip(PlayerMovement::MantleDip * FMath::Clamp(Height / 150.f, 0.4f, 1.f));
		CameraFeel->AddRollImpulse(RollDirection * PlayerMovement::MantleRollImpulse);
	}

	PlaySoundAtFeet(MantleSound, 0.8f);
}

void APlayerCharacter::OnTraversalEnded(ETraversalType Type)
{
	if (Type == ETraversalType::Vault && CameraFeel)
	{
		CameraFeel->AddDip(PlayerMovement::VaultLandingDip);
	}

	PlaySoundAtFeet(LandSound, 0.5f);
}

// -------------------------------------------------------------------------------------------------
// Footsteps

void APlayerCharacter::UpdateFootsteps(float DeltaTime)
{
	const float Speed = GetVelocity().Size2D();
	if (!GetCharacterMovement()->IsMovingOnGround() || bIsSliding || Speed < PlayerMovement::MinFootstepSpeed || FootstepSounds.IsEmpty())
	{
		FootstepDistance = 0.f;
		return;
	}

	FootstepDistance += Speed * DeltaTime;

	const float Stride = FootstepStride * FMath::Lerp(1.f, PlayerMovement::SprintStrideMultiplier, SprintAlpha);
	if (FootstepDistance < Stride)
	{
		return;
	}

	FootstepDistance -= Stride;

	const float Volume = bIsCrouched ? 0.3f : FMath::GetMappedRangeValueClamped(FVector2D(WalkSpeed * 0.5f, SprintSpeed), FVector2D(0.45f, 1.f), Speed);
	PlaySoundAtFeet(FootstepSounds[FMath::RandRange(0, FootstepSounds.Num() - 1)], Volume);
}

void APlayerCharacter::PlaySoundAtFeet(USoundBase* Sound, float VolumeMultiplier) const
{
	if (!Sound)
	{
		return;
	}

	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	UGameplayStatics::PlaySoundAtLocation(this, Sound, Feet, VolumeMultiplier, FMath::FRandRange(0.95f, 1.05f));
}
