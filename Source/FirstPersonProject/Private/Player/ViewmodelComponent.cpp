#include "Player/ViewmodelComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "GameFramework/Pawn.h"
#include "Gun/GunBase.h"
#include "TimerManager.h"

namespace Viewmodel
{
	const FName DefaultWeaponSocket(TEXT("hand_r"));
	const FName SightSocket(TEXT("Sight"));

	constexpr float PoseBlendSpeed = 8.f;
	constexpr float ProceduralReloadBlendSpeed = 6.f;

	FVector RotatorToVector(const FRotator& Rotator) { return FVector(Rotator.Pitch, Rotator.Yaw, Rotator.Roll); }
	FRotator VectorToRotator(const FVector& Vector) { return FRotator(Vector.X, Vector.Y, Vector.Z); }
}

UViewmodelComponent::UViewmodelComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UViewmodelComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const APawn* OwnerPawn = GetOwner<APawn>())
	{
		LastViewRotation = OwnerPawn->GetControlRotation();
	}
}

FName UViewmodelComponent::GetWeaponSocket(const UWeaponDataAsset* WeaponData)
{
	return WeaponData ? WeaponData->AttachSocketName : Viewmodel::DefaultWeaponSocket;
}

void UViewmodelComponent::Initialize(USkeletalMeshComponent* InArms, USkeletalMeshComponent* InGloves, UCameraComponent* InCamera,
	USkeletalMeshComponent* BodyMesh, const FFirstPersonArmsSetup& InSetup)
{
	Arms = InArms;
	Gloves = InGloves;
	Camera = InCamera;
	Setup = InSetup;

	if (!Arms || !Camera || !BodyMesh || !Setup.ArmsMesh)
	{
		SetComponentTickEnabled(false);
		return;
	}

	Arms->AttachToComponent(Camera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	Arms->SetRelativeTransform(Setup.CameraOffset);
	Arms->SetSkeletalMesh(Setup.ArmsMesh);
	Arms->SetAnimInstanceClass(Setup.AnimClass);
	Arms->SetOnlyOwnerSee(true);
	Arms->SetCastShadow(false);
	Arms->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Arms->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	Arms->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Arms->SetVisibility(false);

	if (Gloves)
	{
		Gloves->AttachToComponent(Arms, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		Gloves->SetSkeletalMesh(Setup.GlovesMesh);
		Gloves->SetLeaderPoseComponent(Arms);
		Gloves->SetOnlyOwnerSee(true);
		Gloves->SetCastShadow(false);
		Gloves->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Gloves->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
		Gloves->SetVisibility(false);
	}

	// The owner sees the arms instead of the body, the body still casts the shadow
	BodyMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	BodyMesh->SetCastHiddenShadow(true);

	Camera->SetEnableFirstPersonFieldOfView(true);
	Camera->SetFirstPersonFieldOfView(ViewmodelFOV);
	Camera->SetEnableFirstPersonScale(true);
	Camera->SetFirstPersonScale(ViewmodelDepthScale);

	AddTickPrerequisiteComponent(Arms);
}

void UViewmodelComponent::SetWeapon(AGunBase* NewGun)
{
	Gun = NewGun;
	const UWeaponDataAsset* WeaponData = Gun ? Gun->GetWeaponData() : nullptr;

	if (!Arms || !WeaponData)
	{
		ClearWeapon();
		return;
	}

	// A weapon can bring its own arm animation set (pistol grip vs. long gun)
	const TSubclassOf<UAnimInstance> AnimClass = WeaponData->ArmsAnimClass ? WeaponData->ArmsAnimClass : Setup.AnimClass;
	if (AnimClass && Arms->GetAnimClass() != AnimClass)
	{
		Arms->SetAnimInstanceClass(AnimClass);
	}

	Arms->SetVisibility(true);
	if (Gloves)
	{
		Gloves->SetVisibility(Gloves->GetSkeletalMeshAsset() != nullptr);
	}

	KickLocation.Reset();
	KickRotation.Reset();
	GetWorld()->GetTimerManager().ClearTimer(CycleKickTimer);
	ProceduralReloadPose = 0.f;
	LastAmmoInMagazine = Gun->GetAmmoInMagazine();
	bWasReloading = false;
	bReloadUsesMontage = false;

	PlayArmsMontage(WeaponData->ArmsDrawMontage, WeaponData->EquipTime);
}

void UViewmodelComponent::ClearWeapon()
{
	Gun = nullptr;

	if (Arms)
	{
		Arms->SetVisibility(false);
	}
	if (Gloves)
	{
		Gloves->SetVisibility(false);
	}
}

void UViewmodelComponent::PlayArmsMontage(UAnimMontage* Montage, float FitDuration) const
{
	UAnimInstance* AnimInstance = Arms ? Arms->GetAnimInstance() : nullptr;
	if (!Montage || !AnimInstance)
	{
		return;
	}

	const float Rate = FitDuration > 0.f ? Montage->GetPlayLength() / FitDuration : 1.f;
	AnimInstance->Montage_Play(Montage, FMath::Max(Rate, KINDA_SMALL_NUMBER));
}

void UViewmodelComponent::OnFired(bool bAiming)
{
	const UWeaponDataAsset* WeaponData = Gun ? Gun->GetWeaponData() : nullptr;
	if (!WeaponData)
	{
		return;
	}

	UAnimMontage* FireMontage = (bAiming && WeaponData->ArmsAimFireMontage) ? WeaponData->ArmsAimFireMontage : WeaponData->ArmsFireMontage;
	PlayArmsMontage(FireMontage);

	const float Scale = KickImpulseScale * (bAiming ? AimKickScale : 1.f);
	const FVector& Location = WeaponData->ViewKickLocation;
	const FRotator& Rotation = WeaponData->ViewKickRotation;

	KickLocation.AddImpulse(FVector(Location.X, FMath::FRandRange(-0.1f, 0.1f), Location.Z) * Scale);
	KickRotation.AddImpulse(FVector(
		Rotation.Pitch * FMath::FRandRange(0.85f, 1.15f),
		FMath::FRandRange(-Rotation.Yaw, Rotation.Yaw),
		FMath::FRandRange(-Rotation.Roll, Rotation.Roll)) * Scale);

	if (!WeaponData->ViewCycleKickLocation.IsZero() || !WeaponData->ViewCycleKickRotation.IsZero())
	{
		GetWorld()->GetTimerManager().SetTimer(CycleKickTimer, this, &UViewmodelComponent::PlayCycleKick, FMath::Max(WeaponData->CycleSoundDelay, 0.01f), false);
	}
}

void UViewmodelComponent::PlayCycleKick()
{
	const UWeaponDataAsset* WeaponData = Gun ? Gun->GetWeaponData() : nullptr;
	if (!WeaponData)
	{
		return;
	}

	// Pulling the pump back jolts the weapon back and rolls it; the spring brings it home
	KickLocation.AddImpulse(WeaponData->ViewCycleKickLocation * KickImpulseScale);
	KickRotation.AddImpulse(Viewmodel::RotatorToVector(WeaponData->ViewCycleKickRotation) * KickImpulseScale);
}

void UViewmodelComponent::AddDip(float Strength)
{
	DipSpring.AddImpulse(FVector(0.f, 0.f, -Strength));
}

FString UViewmodelComponent::GetDebugString() const
{
	return FString::Printf(TEXT("aim=%.2f kickLoc=(%.2f %.2f %.2f) kickRot=(%.2f %.2f %.2f) swayLoc=(%.2f %.2f %.2f) swayRot=(%.2f %.2f %.2f) dip=%.2f reload=%.2f"),
		AimAlpha,
		KickLocation.Value.X, KickLocation.Value.Y, KickLocation.Value.Z,
		KickRotation.Value.X, KickRotation.Value.Y, KickRotation.Value.Z,
		SwayLocation.Value.X, SwayLocation.Value.Y, SwayLocation.Value.Z,
		SwayRotation.Value.X, SwayRotation.Value.Y, SwayRotation.Value.Z,
		DipSpring.Value.Z, ProceduralReloadPose);
}

void UViewmodelComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Arms || !Gun || !Gun->GetWeaponData() || DeltaTime <= 0.f)
	{
		return;
	}

	AimAlpha = FMath::FInterpTo(AimAlpha, State.bAiming ? 1.f : 0.f, DeltaTime, State.AimSpeed);
	LoweredPose = FMath::FInterpTo(LoweredPose, State.bLowered ? 1.f : 0.f, DeltaTime, Viewmodel::PoseBlendSpeed);

	UpdateAnimVariables();
	UpdateSway(DeltaTime);
	UpdateReload(DeltaTime);

	const UWeaponDataAsset* WeaponData = Gun->GetWeaponData();
	const float Stiffness = WeaponData->ViewKickStiffness > 0.f ? WeaponData->ViewKickStiffness : KickStiffness;
	const float Damping = WeaponData->ViewKickDamping > 0.f ? WeaponData->ViewKickDamping : KickDamping;
	KickLocation.Update(DeltaTime, Stiffness, Damping);
	KickRotation.Update(DeltaTime, Stiffness, Damping);
	DipSpring.Update(DeltaTime, KickStiffness * 0.35f, KickDamping * 0.6f);

	ApplyTransform();
}

void UViewmodelComponent::UpdateAnimVariables()
{
	// The arms Anim Blueprint reads speed and falling from the pawn itself, only aiming has to be pushed
	UAnimInstance* AnimInstance = Arms->GetAnimInstance();
	if (!AnimInstance || Setup.AimVariableName.IsNone())
	{
		return;
	}

	if (FBoolProperty* AimProperty = FindFProperty<FBoolProperty>(AnimInstance->GetClass(), Setup.AimVariableName))
	{
		// Weapons aimed in code keep the hip animation: the arms Anim Blueprint only knows the rifle aim pose
		AimProperty->SetPropertyValue_InContainer(AnimInstance, State.bAiming && Gun->GetWeaponData()->bArmsAimAnimation);
	}
}

void UViewmodelComponent::UpdateSway(float DeltaTime)
{
	const APawn* OwnerPawn = GetOwner<APawn>();
	if (!OwnerPawn)
	{
		return;
	}

	// The weapon lags behind view rotation (mouse look, recoil) proportionally to its angular speed
	const FRotator ViewRotation = OwnerPawn->GetControlRotation();
	const FRotator Delta = (ViewRotation - LastViewRotation).GetNormalized();
	LastViewRotation = ViewRotation;

	const float Scale = FMath::Lerp(1.f, AimSwayScale, AimAlpha);
	const float YawSpeed = Delta.Yaw / DeltaTime / 100.f;
	const float PitchSpeed = Delta.Pitch / DeltaTime / 100.f;

	const FVector RotationTarget = FVector(
		FMath::Clamp(-PitchSpeed * LookSwayRotation, -MaxSwayRotation, MaxSwayRotation),
		FMath::Clamp(-YawSpeed * LookSwayRotation, -MaxSwayRotation, MaxSwayRotation),
		FMath::Clamp(-YawSpeed * LookSwayRotation * 0.5f - State.MoveInput.X * MoveSwayRoll, -MaxSwayRotation, MaxSwayRotation)) * Scale;

	const FVector LocationTarget = FVector(
		-State.MoveInput.Y * MoveSwayLocation * 0.5f,
		FMath::Clamp(-YawSpeed * LookSwayLocation, -2.5f, 2.5f) - State.MoveInput.X * MoveSwayLocation,
		FMath::Clamp(-PitchSpeed * LookSwayLocation, -2.5f, 2.5f)) * Scale;

	SwayRotation.Update(DeltaTime, SwayStiffness, SwayDamping, RotationTarget);
	SwayLocation.Update(DeltaTime, SwayStiffness, SwayDamping, LocationTarget);
}

void UViewmodelComponent::UpdateReload(float DeltaTime)
{
	const UWeaponDataAsset* WeaponData = Gun->GetWeaponData();
	const bool bReloading = Gun->IsReloading();
	const int32 Ammo = Gun->GetAmmoInMagazine();
	UAnimInstance* AnimInstance = Arms->GetAnimInstance();

	if (bReloading && !bWasReloading)
	{
		// A magazine reload plays the arms animation, stretched to the reload time the weapon uses
		const bool bEmpty = Ammo <= 0;
		UAnimMontage* Montage = (bEmpty && WeaponData->ArmsReloadEmptyMontage) ? WeaponData->ArmsReloadEmptyMontage : WeaponData->ArmsReloadMontage;
		bReloadUsesMontage = Montage && WeaponData->ReloadType == EWeaponReloadType::Magazine;
		if (bReloadUsesMontage)
		{
			PlayArmsMontage(Montage, Gun->GetReloadDuration());
		}
	}
	else if (!bReloading && bWasReloading && bReloadUsesMontage && AnimInstance && Ammo <= LastAmmoInMagazine)
	{
		// Cancelled before the new magazine went in (weapon switch...)
		AnimInstance->Montage_Stop(0.2f);
	}

	// Without an arm animation (shell by shell) the weapon is tilted and nudged by code
	const bool bProcedural = bReloading && !bReloadUsesMontage;
	ProceduralReloadPose = FMath::FInterpTo(ProceduralReloadPose, bProcedural ? 1.f : 0.f, DeltaTime, Viewmodel::ProceduralReloadBlendSpeed);

	if (bProcedural && Ammo > LastAmmoInMagazine)
	{
		KickLocation.AddImpulse(FVector(6.f, 0.f, 8.f));
		KickRotation.AddImpulse(FVector(-18.f, 0.f, 14.f));
	}
	else if (!bReloading && bWasReloading && !bReloadUsesMontage && Ammo > 0)
	{
		// Racking the pump at the end
		KickLocation.AddImpulse(FVector(-22.f, 0.f, 4.f));
		KickRotation.AddImpulse(FVector(20.f, 0.f, -20.f));
	}

	bWasReloading = bReloading;
	LastAmmoInMagazine = Ammo;
}

void UViewmodelComponent::ApplyTransform()
{
	auto AroundPivot = [this](const FVector& Location, const FVector& Rotation)
	{
		return FTransform(-ProceduralPivot) * FTransform(Viewmodel::VectorToRotator(Rotation)) * FTransform(ProceduralPivot + Location);
	};

	// Pose layers: where the weapon is held (climbing, reloading by hand, per weapon correction)
	FVector PoseLocation = Gun->GetWeaponData()->ArmsOffset + FVector(-4.f, 0.f, -16.f) * LoweredPose;
	FVector PoseRotation = FVector(-30.f * LoweredPose, 0.f, 0.f);

	const float ReloadAlpha = FMath::SmoothStep(0.f, 1.f, ProceduralReloadPose);
	PoseLocation += ProceduralReloadOffset * ReloadAlpha;
	PoseRotation += Viewmodel::RotatorToVector(ProceduralReloadTilt) * ReloadAlpha;

	FTransform ArmsToCamera = Setup.CameraOffset * AroundPivot(PoseLocation, PoseRotation);

	// A Sight socket refines the aim: alone (no aim animation) or on top of an aim animation made for another weapon.
	// It is measured on the pose without kick and sway, and held while a montage plays (fire, draw), so it never
	// re-centers the weapon against its own recoil.
	const USkeletalMeshComponent* WeaponMesh = Gun->GetViewMesh();
	if (AimAlpha > KINDA_SMALL_NUMBER && WeaponMesh && WeaponMesh->DoesSocketExist(Viewmodel::SightSocket))
	{
		const UAnimInstance* AnimInstance = Arms->GetAnimInstance();
		const bool bMontagePlaying = AnimInstance && AnimInstance->IsAnyMontagePlaying();
		if (!bHasSightAlignment || !bMontagePlaying)
		{
			SightAlignment = ComputeSightAlignment(ArmsToCamera);
			bHasSightAlignment = true;
		}

		const float AimEase = FMath::SmoothStep(0.f, 1.f, AimAlpha);
		const FTransform Blended(
			FQuat::Slerp(FQuat::Identity, SightAlignment.GetRotation(), AimEase),
			SightAlignment.GetTranslation() * AimEase);
		ArmsToCamera = ArmsToCamera * Blended;
	}
	else
	{
		bHasSightAlignment = false;
	}

	// Motion layers on top: the weapon lags, kicks and dips from wherever it is held, aimed or not
	const FVector MotionLocation = SwayLocation.Value + KickLocation.Value + DipSpring.Value;
	const FVector MotionRotation = SwayRotation.Value + KickRotation.Value;
	Arms->SetRelativeTransform(ArmsToCamera * AroundPivot(MotionLocation, MotionRotation));
}

FTransform UViewmodelComponent::ComputeSightAlignment(const FTransform& ArmsToCamera) const
{
	const USkeletalMeshComponent* WeaponMesh = Gun->GetViewMesh();
	if (!WeaponMesh || !WeaponMesh->DoesSocketExist(Viewmodel::SightSocket))
	{
		return FTransform::Identity;
	}

	// Sight socket in camera space, through the arms (independent of where the arms were last frame)
	const FTransform SightInArms = WeaponMesh->GetSocketTransform(Viewmodel::SightSocket).GetRelativeTransform(Arms->GetComponentTransform());
	const FTransform SightInCamera = SightInArms * ArmsToCamera;

	// The socket axes only give the lines: never turn the weapon around, even if a socket was authored backwards
	FVector LineOfSight = SightInCamera.GetUnitAxis(EAxis::X);
	FVector Up = SightInCamera.GetUnitAxis(EAxis::Z);
	if (LineOfSight.X < 0.f)
	{
		LineOfSight = -LineOfSight;
	}
	if (Up.Z < 0.f)
	{
		Up = -Up;
	}

	// Turn the line of sight onto the view axis, then roll the weapon upright
	const FQuat Align = FQuat::FindBetweenNormals(LineOfSight, FVector::ForwardVector);
	const FVector AlignedUp = Align.RotateVector(Up);
	const FQuat Rotation = FQuat(FVector::ForwardVector, FMath::Atan2(AlignedUp.Y, AlignedUp.Z)) * Align;

	// No distance given: keep the depth the aim animation brings the sight to, only center it
	const float Distance = Gun->GetWeaponData()->AimSightDistance;
	const FVector Target(Distance > 0.f ? Distance : SightInCamera.GetLocation().X, 0.f, 0.f);
	return FTransform(Rotation, Target - Rotation.RotateVector(SightInCamera.GetLocation()));
}
