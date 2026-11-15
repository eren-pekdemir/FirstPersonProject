#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TraversalComponent.generated.h"

class ACharacter;

UENUM(BlueprintType)
enum class ETraversalType : uint8
{
	None,
	/** Climb up onto a ledge and stop on top of it */
	Mantle,
	/** Hop over a low, thin obstacle and keep running */
	Vault
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTraversalStarted, ETraversalType /*Type*/, float /*Height*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnTraversalEnded, ETraversalType /*Type*/);

/** Everything needed to play one traversal move */
struct FTraversalMove
{
	ETraversalType Type = ETraversalType::None;
	FVector Start = FVector::ZeroVector;

	/** Mantle: point on the ledge. Vault: peak above the obstacle. */
	FVector Middle = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;

	float Height = 0.f;
	float Duration = 0.f;
	float ExitSpeed = 0.f;
};

/**
 *  Procedural first person mantling and vaulting (Apex / Titanfall style).
 *  Ledges are detected with traces in front of the owner; the move itself drives the actor
 *  along a curve with movement disabled, then hands control back to the character movement.
 */
UCLASS(ClassGroup = (Movement), meta = (BlueprintSpawnableComponent))
class FIRSTPERSONPROJECT_API UTraversalComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UTraversalComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Tries to start a mantle or vault. bFromJump allows higher ledges (the jump input is used to climb). */
	bool TryTraversal(bool bFromJump);

	UFUNCTION(BlueprintPure, Category = "Traversal")
	bool IsTraversing() const { return CurrentMove.Type != ETraversalType::None; }

	UFUNCTION(BlueprintPure, Category = "Traversal")
	ETraversalType GetTraversalType() const { return CurrentMove.Type; }

	FOnTraversalStarted OnTraversalStarted;
	FOnTraversalEnded OnTraversalEnded;

protected:

	virtual void BeginPlay() override;

	/** Ledges lower than this are simply walked over */
	UPROPERTY(EditAnywhere, Category = "Detection", meta = (ClampMin = "0", Units = "cm"))
	float MinLedgeHeight = 45.f;

	/** Highest ledge (above the feet) that can be climbed by pressing jump */
	UPROPERTY(EditAnywhere, Category = "Detection", meta = (ClampMin = "0", Units = "cm"))
	float MaxJumpMantleHeight = 230.f;

	/** Highest ledge (above the feet) grabbed automatically while airborne and pushing forward */
	UPROPERTY(EditAnywhere, Category = "Detection", meta = (ClampMin = "0", Units = "cm"))
	float MaxAirMantleHeight = 150.f;

	/** How far in front of the capsule a wall can be */
	UPROPERTY(EditAnywhere, Category = "Detection", meta = (ClampMin = "0", Units = "cm"))
	float ReachDistance = 60.f;

	/** Grab ledges automatically when pushing into them in the air */
	UPROPERTY(EditAnywhere, Category = "Detection")
	bool bAutoMantleInAir = true;

	UPROPERTY(EditAnywhere, Category = "Vault", meta = (ClampMin = "0", Units = "cm"))
	float MaxVaultHeight = 125.f;

	/** Obstacles thicker than this are mantled instead of vaulted */
	UPROPERTY(EditAnywhere, Category = "Vault", meta = (ClampMin = "0", Units = "cm"))
	float MaxVaultDepth = 120.f;

	UPROPERTY(EditAnywhere, Category = "Vault", meta = (ClampMin = "0", Units = "cm/s"))
	float MinVaultSpeed = 200.f;

	UPROPERTY(EditAnywhere, Category = "Vault", meta = (ClampMin = "0", Units = "s"))
	float VaultDuration = 0.42f;

	/** Minimum speed kept when landing from a vault */
	UPROPERTY(EditAnywhere, Category = "Vault", meta = (ClampMin = "0", Units = "cm/s"))
	float VaultExitSpeed = 500.f;

	UPROPERTY(EditAnywhere, Category = "Mantle", meta = (ClampMin = "0", Units = "s"))
	float MantleBaseDuration = 0.18f;

	/** Extra mantle time per meter of height */
	UPROPERTY(EditAnywhere, Category = "Mantle", meta = (ClampMin = "0", Units = "s"))
	float MantleDurationPerMeter = 0.14f;

	/** Speed the character keeps moving forward with after a mantle */
	UPROPERTY(EditAnywhere, Category = "Mantle", meta = (ClampMin = "0", Units = "cm/s"))
	float MantleExitSpeed = 350.f;

private:

	/** Direction the player wants to traverse in: input direction, or the actor's forward */
	FVector GetTraversalDirection() const;

	/** Finds a vertical wall in front of the feet / body. Returns false when nothing climbable is there. */
	bool FindWall(const FVector& Direction, FHitResult& OutWallHit) const;

	/** Finds the walkable top of the wall. Returns false if it's too high or blocked. */
	bool FindLedge(const FHitResult& WallHit, const FVector& Direction, float MaxHeight, FVector& OutLedge) const;

	/** True if the owner's capsule fits standing at CapsuleCenter */
	bool HasRoomAt(const FVector& CapsuleCenter) const;

	/** Builds a vault move over a thin obstacle, if possible */
	bool BuildVault(const FHitResult& WallHit, const FVector& Ledge, const FVector& Direction, FTraversalMove& OutMove) const;

	bool BuildMantle(const FVector& Ledge, const FVector& Direction, FTraversalMove& OutMove) const;

	void StartMove(const FTraversalMove& Move);
	void UpdateMove(float DeltaTime);
	void FinishMove();

	/** Position along the current move at Alpha (0..1) */
	FVector EvaluateMove(float Alpha) const;

	bool ShouldAutoMantle() const;

	UPROPERTY()
	TObjectPtr<ACharacter> Character;

	FTraversalMove CurrentMove;
	float Elapsed = 0.f;
};
