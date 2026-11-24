#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ZombiePickup.generated.h"

class APlayerCharacter;
class UPointLightComponent;
class USoundBase;
class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EZombiePickupType : uint8
{
	Ammo,
	Health,
	Battery
};

/**
 *  Supplies lying on the ground: ammo for every weapon, a first aid kit or a flashlight battery.
 *  Built from simple shapes and a faint glow so it can be found in the dark. Walk over it to take it;
 *  it stays when the player does not need it (full health, full ammo).
 */
UCLASS()
class FIRSTPERSONPROJECT_API AZombiePickup : public AActor
{
	GENERATED_BODY()

public:

	AZombiePickup();

	virtual void Tick(float DeltaTime) override;

	/** Sets what it gives and how it looks. Call right after spawning. */
	void SetPickupType(EZombiePickupType InType);

	EZombiePickupType GetPickupType() const { return Type; }

protected:

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Visual;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> DetailMeshA;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> DetailMeshB;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UPointLightComponent> Glow;

	UPROPERTY(EditAnywhere, Category = "Pickup")
	EZombiePickupType Type = EZombiePickupType::Ammo;

	/** Fraction of each weapon's max reserve ammo given */
	UPROPERTY(EditDefaultsOnly, Category = "Pickup", meta = (ClampMin = "0", ClampMax = "1"))
	float AmmoFraction = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Pickup", meta = (ClampMin = "0"))
	float HealthAmount = 45.f;

	/** Fraction of a full battery */
	UPROPERTY(EditDefaultsOnly, Category = "Pickup", meta = (ClampMin = "0", ClampMax = "1"))
	float BatteryAmount = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Pickup")
	TObjectPtr<USoundBase> AmmoSound;

	UPROPERTY(EditDefaultsOnly, Category = "Pickup")
	TObjectPtr<USoundBase> HealthSound;

	UPROPERTY(EditDefaultsOnly, Category = "Pickup")
	TObjectPtr<USoundBase> BatterySound;

	/** Magazine shown on top of the ammo box */
	UPROPERTY(EditDefaultsOnly, Category = "Pickup")
	TObjectPtr<UStaticMesh> MagazineMesh;

	/** Seconds before it blinks out at the end of its life span */
	UPROPERTY(EditDefaultsOnly, Category = "Pickup", meta = (Units = "s"))
	float BlinkTime = 5.f;

private:

	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** Gives the supply. Returns false when the player does not need it. */
	bool TryGive(APlayerCharacter& Player) const;
	void ApplyLook();
	void SetPart(UStaticMeshComponent* Part, UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& Scale, const FLinearColor& Color);

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> ShapeMaterial;

	float BaseGlowIntensity = 0.f;
	float Age = 0.f;
	float NextRetryTime = 0.f;
};
