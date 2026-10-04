// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Data/WeaponDataAsset.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "GunBase.generated.h"

class UNiagaraComponent;

UCLASS()
class FIRSTPERSONPROJECT_API AGunBase : public AActor
{
	GENERATED_BODY()

public:
	AGunBase();
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Components")
	UWeaponDataAsset* WeaponDataAsset;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	void StartFire(APlayerController* PlayerController);

	void StopFire();

	void PerformFire();

	FVector TraceEndPoint(APlayerController* PlayerController);

	// Pickup / interaction

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction")
	FText PickupPromptText = FText::FromString(TEXT("Al"));

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void SetHeld(bool bNewHeld);

	bool IsHeld() const { return bIsHeld; }

private:

	bool bIsHeld = false;
	
	float TraceDistance = 3000.f;
	
	// TIMER
	FTimerHandle TimerHandle_AutoFire;
	
	APlayerController* CurrentOwnerController;
	
	float TimeBetweenShots;
protected:
	virtual void BeginPlay() override;
	
	// recoil state
	FRotator RecoilTarget  = FRotator::ZeroRotator;
	FRotator RecoilCurrent = FRotator::ZeroRotator;
	int32 RecoilIndex = 0;
	bool bRecovering = false;
	float TimeSinceLastShot = 0.f;

	void ApplyRecoil();
	void UpdateRecoil(float DeltaTime);
	
	float SmokeTimer = 0.f;
	bool bFiredSinceSmoke = false;

	void SpawnMuzzleSmoke();
	void UpdateSmoke(float DeltaTime);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SlideMesh;   // sürgü / üst parça

	UPROPERTY(EditAnywhere, Category = "Slide")
	float SlideKickBack = 3.f;        // cm geri (mesh ölçeğine göre ayarla)

	UPROPERTY(EditAnywhere, Category = "Slide")
	float SlideReturnSpeed = 40.f;    // geri dönüş hızı

	// state
	FVector SlideRestLocation;

	void KickSlide();
	void UpdateSlide(float DeltaTime);
	
	UPROPERTY(EditAnywhere)
	float ShootParticleScale;
	
	

	// sınıf içinde:
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> TracerComponent = nullptr;

	bool bTracerTrigger = false;   // bool trigger için

	float CurrentSpread = 0.f;

	UFUNCTION(BlueprintPure, Category = "Crosshair")
	float GetCurrentSpread() const { return CurrentSpread; }

	void UpdateSpread(float DeltaTime);

public:
	virtual void Tick(float DeltaTime) override;
};
