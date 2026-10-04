// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WeaponDataAsset.generated.h"

class UNiagaraSystem;
class UCameraShakeBase;
class UAnimInstance;

UENUM(BlueprintType)
enum class EWeaponFireMode : uint8
{
	Single      UMETA(DisplayName = "Single Fire"),
	Automatic   UMETA(DisplayName = "Automatic Fire")
};

UCLASS()
class FIRSTPERSONPROJECT_API UWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TSubclassOf<UAnimInstance> AnimLayerClass;
	
	UPROPERTY(EditDefaultsOnly, Category = "Weapon Settings")
	EWeaponFireMode FireMode;
	
	UPROPERTY(EditDefaultsOnly, Category = "Attachment")
	FName AttachSocketName = "WeaponSocket";
	
	UPROPERTY(EditDefaultsOnly, Category = "Visuals")
	UNiagaraSystem* MuzzleSmokeSystem;

	UPROPERTY(EditDefaultsOnly, Category = "Impact")
	TSubclassOf<AActor> ImpactActorClass;
	
	UPROPERTY(EditDefaultsOnly, Category = "Visuals")
	UParticleSystem* ShootParticle;
	
	UPROPERTY(EditDefaultsOnly, Category = "Visuals")
	UNiagaraSystem* TracerSystem;

	UPROPERTY(EditDefaultsOnly, Category = "Visuals")
	TSubclassOf<UCameraShakeBase> FireCameraShake;
	
	UPROPERTY(EditDefaultsOnly, Category = "Visuals")
	USoundBase* FireSound;
	
	UPROPERTY(EditDefaultsOnly, Category = "Weapon Settings")
	float RoundsPerMinute = 60.f;
	
	UPROPERTY(EditDefaultsOnly, Category = "Animations")
	UAnimMontage* CharacterFireMontage;

	UPROPERTY(EditDefaultsOnly, Category = "Animations")
	UAnimMontage* EquipMontage;
	
	// X = Yaw (yana), Y = Pitch (yukarı)
	UPROPERTY(EditAnywhere, Category = "Recoil")
	TArray<FVector2D> RecoilPattern;

	UPROPERTY(EditAnywhere, Category = "Recoil")
	float RecoilApplySpeed = 18.f;     // tekleme ne kadar hızlı uygulanır (snappy = yüksek)

	UPROPERTY(EditAnywhere, Category = "Recoil")
	float RecoilRecoverySpeed = 8.f;   // toparlanma hızı

	UPROPERTY(EditAnywhere, Category = "Recoil")
	float RecoilResetDelay = 0.3f;     // ateş bırakılınca pattern'in sıfırlanma süresi
	
	UPROPERTY(EditAnywhere, Category = "FX")
	float SmokeDelay = 0.4f;  
	
	
	UPROPERTY(EditAnywhere, Category = "Crosshair") float BaseSpread = 8.f;        // dinlenme gap (px)
	UPROPERTY(EditAnywhere, Category = "Crosshair") float SpreadPerShot = 14.f;    // atış başına artış
	UPROPERTY(EditAnywhere, Category = "Crosshair") float MaxSpread = 70.f;        // tavan
	UPROPERTY(EditAnywhere, Category = "Crosshair") float SpreadRecoverySpeed = 6.f;
};
