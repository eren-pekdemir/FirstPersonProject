#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DamageTarget.generated.h"

class UMaterialInstanceDynamic;
class USceneComponent;
class USoundBase;
class UStaticMeshComponent;

/**
 *  Shooting range target dummy. Has a body and a head (tagged "Head" for headshots),
 *  flashes when hit, falls over when killed and stands back up after a delay.
 */
UCLASS()
class FIRSTPERSONPROJECT_API ADamageTarget : public AActor
{
	GENERATED_BODY()

public:

	ADamageTarget();

	virtual void Tick(float DeltaTime) override;

	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UFUNCTION(BlueprintPure, Category = "Target")
	bool IsAlive() const { return Health > 0.f; }

	UFUNCTION(BlueprintPure, Category = "Target")
	float GetHealthPercent() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }

protected:

	virtual void BeginPlay() override;

	/** Pivot at the base of the target, rotated to knock it over */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Pivot;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> HeadMesh;

	UPROPERTY(EditAnywhere, Category = "Target", meta = (ClampMin = "1"))
	float MaxHealth = 150.f;

	UPROPERTY(EditAnywhere, Category = "Target", meta = (ClampMin = "0", Units = "s"))
	float RespawnDelay = 2.5f;

	/** Sideways movement amplitude. 0 keeps the target static. */
	UPROPERTY(EditAnywhere, Category = "Target", meta = (ClampMin = "0", Units = "cm"))
	float StrafeDistance = 0.f;

	UPROPERTY(EditAnywhere, Category = "Target", meta = (ClampMin = "0", Units = "cm/s"))
	float StrafeSpeed = 250.f;

	UPROPERTY(EditAnywhere, Category = "Target")
	TObjectPtr<USoundBase> KillSound;

	UPROPERTY(EditAnywhere, Category = "Target")
	FLinearColor BaseColor = FLinearColor(0.75f, 0.35f, 0.08f);

	UPROPERTY(EditAnywhere, Category = "Target")
	FLinearColor HitColor = FLinearColor(1.f, 1.f, 1.f);

private:

	void Die();
	void Respawn();

	void UpdateFlash(float DeltaTime);
	void UpdateFall(float DeltaTime);
	void UpdateStrafe(float DeltaTime);

	void SetColor(const FLinearColor& Color);

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;

	FTimerHandle RespawnTimer;

	float Health = 0.f;
	float FlashAlpha = 0.f;

	/** 0 = standing, 1 = knocked over */
	float FallAlpha = 0.f;

	float StrafeTime = 0.f;
	FVector SpawnLocation = FVector::ZeroVector;
};
