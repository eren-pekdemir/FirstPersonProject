#include "Player/TraversalComponent.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace Traversal
{
	/** Heights above the feet probed for a wall, as a fraction between MinLedgeHeight and the max height */
	constexpr float WallProbeFractions[] = { 0.f, 0.3f, 0.6f, 0.9f };

	/** Walls must be close to vertical */
	constexpr float MaxWallNormalZ = 0.35f;

	/** And roughly facing the traversal direction */
	constexpr float MinWallFacing = 0.5f;

	/** How far into the obstacle the ledge trace starts */
	constexpr float LedgeProbeDepth = 15.f;

	constexpr float LedgeProbeRadius = 8.f;

	/** Gap kept between the capsule and the geometry when placing it */
	constexpr float Clearance = 2.f;

	/** Step used to find the far edge of an obstacle when vaulting */
	constexpr float VaultDepthStep = 15.f;

	/** Required drop behind an obstacle for it to count as vaultable */
	constexpr float MinVaultDrop = 35.f;

	/** How far below the ledge the vault landing is searched for */
	constexpr float VaultLandingSearch = 250.f;

	/** Peak of the vault arc above the ledge, on top of the capsule half height */
	constexpr float VaultArcHeight = 15.f;

	/** Fraction of the mantle spent rising before moving forward onto the ledge */
	constexpr float MantleRiseFraction = 0.55f;

	/** Only auto-mantle when not rising fast (near the jump apex or falling) */
	constexpr float MaxAutoMantleRiseSpeed = 250.f;
}

UTraversalComponent::UTraversalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UTraversalComponent::BeginPlay()
{
	Super::BeginPlay();

	Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		SetComponentTickEnabled(false);
	}
}

void UTraversalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (IsTraversing())
	{
		UpdateMove(DeltaTime);
		return;
	}

	if (ShouldAutoMantle())
	{
		TryTraversal(false);
	}
}

bool UTraversalComponent::ShouldAutoMantle() const
{
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!bAutoMantleInAir || !Movement->IsFalling() || Movement->Velocity.Z > Traversal::MaxAutoMantleRiseSpeed)
	{
		return false;
	}

	// Only when actively pushing forward, so falling past ledges doesn't grab them
	const FVector Input = Movement->GetCurrentAcceleration().GetSafeNormal2D();
	return Input.Dot(Character->GetActorForwardVector()) > 0.5f;
}

// -------------------------------------------------------------------------------------------------
// Detection

FVector UTraversalComponent::GetTraversalDirection() const
{
	const FVector Input = Character->GetCharacterMovement()->GetCurrentAcceleration().GetSafeNormal2D();
	return Input.IsNearlyZero() ? Character->GetActorForwardVector().GetSafeNormal2D() : Input;
}

bool UTraversalComponent::TryTraversal(bool bFromJump)
{
	if (!Character || IsTraversing())
	{
		return false;
	}

	const FVector Direction = GetTraversalDirection();
	const float MaxHeight = bFromJump ? MaxJumpMantleHeight : MaxAirMantleHeight;

	FHitResult WallHit;
	if (!FindWall(Direction, WallHit))
	{
		return false;
	}

	FVector Ledge;
	if (!FindLedge(WallHit, Direction, MaxHeight, Ledge))
	{
		return false;
	}

	FTraversalMove Move;
	if (!BuildVault(WallHit, Ledge, Direction, Move) && !BuildMantle(Ledge, Direction, Move))
	{
		return false;
	}

	StartMove(Move);
	return true;
}

bool UTraversalComponent::FindWall(const FVector& Direction, FHitResult& OutWallHit) const
{
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const FVector Location = Character->GetActorLocation();
	const float FeetZ = Location.Z - Capsule->GetScaledCapsuleHalfHeight();
	const float TraceLength = Capsule->GetScaledCapsuleRadius() + ReachDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(TraversalWall), false, Character);

	for (const float Fraction : Traversal::WallProbeFractions)
	{
		const float ProbeHeight = FMath::Lerp(MinLedgeHeight + 5.f, MaxJumpMantleHeight - 10.f, Fraction);
		const FVector Start(Location.X, Location.Y, FeetZ + ProbeHeight);
		const FVector End = Start + Direction * TraceLength;

		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			continue;
		}

		const bool bVertical = FMath::Abs(Hit.ImpactNormal.Z) < Traversal::MaxWallNormalZ;
		const bool bFacing = (-Hit.ImpactNormal.GetSafeNormal2D()).Dot(Direction) > Traversal::MinWallFacing;
		if (bVertical && bFacing && !Hit.bStartPenetrating)
		{
			OutWallHit = Hit;
			return true;
		}
	}

	return false;
}

bool UTraversalComponent::FindLedge(const FHitResult& WallHit, const FVector& Direction, float MaxHeight, FVector& OutLedge) const
{
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float FeetZ = Character->GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight();

	const FVector ProbeXY = WallHit.ImpactPoint + Direction * Traversal::LedgeProbeDepth;
	const FVector Start(ProbeXY.X, ProbeXY.Y, FeetZ + MaxHeight + Traversal::LedgeProbeRadius);
	const FVector End(ProbeXY.X, ProbeXY.Y, FeetZ + MinLedgeHeight);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(TraversalLedge), false, Character);

	FHitResult Hit;
	const bool bHit = GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeSphere(Traversal::LedgeProbeRadius), Params);

	// Starting inside geometry means the wall is taller than we can reach
	if (!bHit || Hit.bStartPenetrating || !Character->GetCharacterMovement()->IsWalkable(Hit))
	{
		return false;
	}

	OutLedge = Hit.ImpactPoint;
	const float Height = OutLedge.Z - FeetZ;
	return Height >= MinLedgeHeight && Height <= MaxHeight;
}

bool UTraversalComponent::HasRoomAt(const FVector& CapsuleCenter) const
{
	const ACharacter* DefaultCharacter = Character->GetClass()->GetDefaultObject<ACharacter>();
	const UCapsuleComponent* DefaultCapsule = DefaultCharacter->GetCapsuleComponent();

	const FCollisionShape Shape = FCollisionShape::MakeCapsule(
		DefaultCapsule->GetScaledCapsuleRadius(), DefaultCapsule->GetScaledCapsuleHalfHeight());

	FCollisionQueryParams Params(SCENE_QUERY_STAT(TraversalRoom), false, Character);
	return !GetWorld()->OverlapBlockingTestByChannel(CapsuleCenter, FQuat::Identity,
		Character->GetCapsuleComponent()->GetCollisionObjectType(), Shape, Params);
}

bool UTraversalComponent::BuildVault(const FHitResult& WallHit, const FVector& Ledge, const FVector& Direction, FTraversalMove& OutMove) const
{
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float FeetZ = Character->GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight();
	const float Height = Ledge.Z - FeetZ;
	const float Speed = Character->GetVelocity().Size2D();

	if (Height > MaxVaultHeight || Speed < MinVaultSpeed)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(TraversalVault), false, Character);

	// Walk along the top until it drops away: that's the far edge
	float Depth = -1.f;
	for (float Probe = Traversal::VaultDepthStep; Probe <= MaxVaultDepth; Probe += Traversal::VaultDepthStep)
	{
		const FVector Point = WallHit.ImpactPoint + Direction * Probe;
		const FVector Start(Point.X, Point.Y, Ledge.Z + 5.f);
		const FVector End(Point.X, Point.Y, Ledge.Z - Traversal::MinVaultDrop);

		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			Depth = Probe;
			break;
		}
	}

	if (Depth < 0.f)
	{
		return false;
	}

	const ACharacter* DefaultCharacter = Character->GetClass()->GetDefaultObject<ACharacter>();
	const float Radius = DefaultCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = DefaultCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	// Find the floor on the far side
	const FVector LandXY = WallHit.ImpactPoint + Direction * (Depth + Radius + 20.f);
	FHitResult FloorHit;
	const bool bFloor = GetWorld()->LineTraceSingleByChannel(FloorHit,
		FVector(LandXY.X, LandXY.Y, Ledge.Z + 5.f),
		FVector(LandXY.X, LandXY.Y, Ledge.Z - Traversal::VaultLandingSearch), ECC_Visibility, Params);

	// No floor nearby: land in the air and fall, like vaulting off a ledge
	const float LandZ = bFloor ? FloorHit.ImpactPoint.Z : FeetZ;
	const FVector End(LandXY.X, LandXY.Y, LandZ + HalfHeight + Traversal::Clearance);

	const FVector Peak = FVector(
		(Character->GetActorLocation().X + End.X) * 0.5f,
		(Character->GetActorLocation().Y + End.Y) * 0.5f,
		Ledge.Z + HalfHeight + Traversal::VaultArcHeight);

	if (!HasRoomAt(End) || !HasRoomAt(FVector(Ledge.X, Ledge.Y, Peak.Z)))
	{
		return false;
	}

	OutMove.Type = ETraversalType::Vault;
	OutMove.Middle = Peak;
	OutMove.End = End;
	OutMove.Direction = Direction;
	OutMove.Height = Height;
	OutMove.Duration = VaultDuration;
	OutMove.ExitSpeed = FMath::Max(Speed, VaultExitSpeed);
	return true;
}

bool UTraversalComponent::BuildMantle(const FVector& Ledge, const FVector& Direction, FTraversalMove& OutMove) const
{
	const ACharacter* DefaultCharacter = Character->GetClass()->GetDefaultObject<ACharacter>();
	const float Radius = DefaultCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = DefaultCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float FeetZ = Character->GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight();
	const float Height = Ledge.Z - FeetZ;

	const FVector End = Ledge + Direction * (Radius + Traversal::Clearance) + FVector(0.f, 0.f, HalfHeight + Traversal::Clearance);

	// Rise straight up in front of the wall, then step onto the ledge
	const FVector Location = Character->GetActorLocation();
	const FVector RiseTop(Location.X, Location.Y, End.Z);

	if (!HasRoomAt(End) || !HasRoomAt(RiseTop))
	{
		return false;
	}

	OutMove.Type = ETraversalType::Mantle;
	OutMove.Middle = RiseTop;
	OutMove.End = End;
	OutMove.Direction = Direction;
	OutMove.Height = Height;
	OutMove.Duration = MantleBaseDuration + MantleDurationPerMeter * Height / 100.f;
	OutMove.ExitSpeed = MantleExitSpeed;
	return true;
}

// -------------------------------------------------------------------------------------------------
// Execution

void UTraversalComponent::StartMove(const FTraversalMove& Move)
{
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();

	// Always traverse standing, so the capsule matches the room checks
	if (Character->bIsCrouched)
	{
		Movement->UnCrouch(false);
	}

	CurrentMove = Move;
	CurrentMove.Start = Character->GetActorLocation();
	Elapsed = 0.f;

	Movement->StopMovementImmediately();
	// Custom mode has no physics: the move fully drives the actor and ignores input
	Movement->SetMovementMode(MOVE_Custom, 0);

	OnTraversalStarted.Broadcast(CurrentMove.Type, CurrentMove.Height);
}

FVector UTraversalComponent::EvaluateMove(float Alpha) const
{
	if (CurrentMove.Type == ETraversalType::Vault)
	{
		// Quadratic bezier arc over the obstacle
		const float Inverse = 1.f - Alpha;
		return Inverse * Inverse * CurrentMove.Start + 2.f * Inverse * Alpha * CurrentMove.Middle + Alpha * Alpha * CurrentMove.End;
	}

	// Mantle: rise fast (ease out), the forward motion overlaps the end of the rise
	const float RiseAlpha = FMath::InterpEaseOut(0.f, 1.f, FMath::Clamp(Alpha / Traversal::MantleRiseFraction, 0.f, 1.f), 2.f);
	const float ForwardAlpha = FMath::InterpEaseInOut(0.f, 1.f,
		FMath::Clamp((Alpha - Traversal::MantleRiseFraction * 0.6f) / (1.f - Traversal::MantleRiseFraction * 0.6f), 0.f, 1.f), 2.f);

	FVector Position = FMath::Lerp(CurrentMove.Start, CurrentMove.End, ForwardAlpha);
	Position.Z = FMath::Lerp(CurrentMove.Start.Z, CurrentMove.Middle.Z, RiseAlpha);
	return Position;
}

void UTraversalComponent::UpdateMove(float DeltaTime)
{
	Elapsed += DeltaTime;
	const float Alpha = FMath::Clamp(Elapsed / FMath::Max(CurrentMove.Duration, KINDA_SMALL_NUMBER), 0.f, 1.f);

	Character->SetActorLocation(EvaluateMove(Alpha), false, nullptr, ETeleportType::None);
	Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;

	if (Alpha >= 1.f)
	{
		FinishMove();
	}
}

void UTraversalComponent::FinishMove()
{
	const ETraversalType FinishedType = CurrentMove.Type;
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();

	Movement->SetMovementMode(FinishedType == ETraversalType::Vault ? MOVE_Falling : MOVE_Walking);
	Movement->Velocity = CurrentMove.Direction * CurrentMove.ExitSpeed;

	CurrentMove = FTraversalMove();
	Elapsed = 0.f;

	OnTraversalEnded.Broadcast(FinishedType);
}
