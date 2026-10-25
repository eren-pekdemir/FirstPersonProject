#include "Gun/GunBase.h"

#include "Components/StaticMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "FirstPersonProject.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "Particles/ParticleSystemComponent.h"
#include "TimerManager.h"

namespace GunBase
{
	const FName MuzzleSocket(TEXT("MuzzleSocket"));

	// Niagara user parameters exposed by the tracer system
	const FName TracerMuzzlePosition(TEXT("MuzzlePosition"));
	const FName TracerImpactPositions(TEXT("ImpactPositions"));
	const FName TracerTrigger(TEXT("Trigger"));
	const FVector TracerScale(0.1f);

	/** Owner speed at which the full movement spread is applied */
	constexpr float FullMovementSpreadSpeed = 600.f;
}

AGunBase::AGunBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	SetRootComponent(MeshComponent);

	SlideMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SlideMesh"));
	SlideMesh->SetupAttachment(MeshComponent);
}

void AGunBase::BeginPlay()
{
	Super::BeginPlay();

	if (!WeaponDataAsset)
	{
		UE_LOG(LogFirstPersonProject, Error, TEXT("%s has no WeaponDataAsset assigned and will not be able to fire."), *GetName());
	}
	else
	{
		CurrentSpread = WeaponDataAsset->BaseSpread;
	}

	SlideRestLocation = SlideMesh->GetRelativeLocation();

	// Blueprints may author the meshes with collision disabled (held state).
	// Re-apply the current state so guns lying in the world can be hit by the interaction trace.
	SetHeld(bIsHeld);
}

void AGunBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateRecoil(DeltaTime);
	UpdateSpread(DeltaTime);
	UpdateSlide(DeltaTime);
	UpdateMuzzleSmoke(DeltaTime);
}

void AGunBase::SetHeld(bool bNewHeld)
{
	bIsHeld = bNewHeld;

	const ECollisionEnabled::Type Collision = bIsHeld ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics;
	MeshComponent->SetCollisionEnabled(Collision);
	SlideMesh->SetCollisionEnabled(Collision);

	if (!bIsHeld)
	{
		StopFire();
		ResetRecoil();
		bMuzzleSmokePending = false;
		SlideMesh->SetRelativeLocation(SlideRestLocation);
	}

	// Everything updated on tick only matters while the weapon is in someone's hands
	SetActorTickEnabled(bIsHeld);
}

// -------------------------------------------------------------------------------------------------
// Firing

void AGunBase::StartFire()
{
	if (!WeaponDataAsset)
	{
		return;
	}

	const float TimeBetweenShots = WeaponDataAsset->GetTimeBetweenShots();
	const float TimeUntilReady = LastFireTime + TimeBetweenShots - GetWorld()->GetTimeSeconds();
	const bool bAutomatic = WeaponDataAsset->FireMode == EWeaponFireMode::Automatic;

	if (TimeUntilReady > 0.f)
	{
		// Trigger pulled again before the weapon cycled: automatic weapons resume
		// as soon as they are ready, single fire weapons ignore the press.
		if (bAutomatic)
		{
			GetWorldTimerManager().SetTimer(AutoFireTimer, this, &AGunBase::Fire, TimeBetweenShots, true, TimeUntilReady);
		}
		return;
	}

	Fire();

	if (bAutomatic)
	{
		GetWorldTimerManager().SetTimer(AutoFireTimer, this, &AGunBase::Fire, TimeBetweenShots, true);
	}
}

void AGunBase::StopFire()
{
	GetWorldTimerManager().ClearTimer(AutoFireTimer);
}

void AGunBase::Fire()
{
	const APawn* OwnerPawn = GetOwner<APawn>();
	APlayerController* PlayerController = OwnerPawn ? OwnerPawn->GetController<APlayerController>() : nullptr;
	if (!WeaponDataAsset || !PlayerController)
	{
		StopFire();
		return;
	}

	LastFireTime = GetWorld()->GetTimeSeconds();

	PlayFireFeedback(*PlayerController);

	FHitResult Hit;
	const FVector ShotEnd = TraceFromView(*PlayerController, Hit);
	SpawnImpact(Hit);
	SpawnTracer(ShotEnd);

	ApplyRecoil();
	CurrentSpread = FMath::Min(CurrentSpread + WeaponDataAsset->SpreadPerShot, WeaponDataAsset->MaxSpread);

	bMuzzleSmokePending = true;
	MuzzleSmokeTimer = 0.f;
}

FVector AGunBase::TraceFromView(const APlayerController& PlayerController, FHitResult& OutHit) const
{
	// The crosshair sits at the center of the screen, so the shot follows the view direction
	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController.GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * WeaponDataAsset->MaxRange;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(GunFire), false, this);
	Params.AddIgnoredActor(GetOwner());

	GetWorld()->LineTraceSingleByChannel(OutHit, ViewLocation, TraceEnd, ECC_Visibility, Params);

	return OutHit.bBlockingHit ? OutHit.ImpactPoint : TraceEnd;
}

void AGunBase::PlayFireFeedback(APlayerController& PlayerController)
{
	if (ACharacter* OwnerCharacter = GetOwner<ACharacter>())
	{
		OwnerCharacter->PlayAnimMontage(WeaponDataAsset->CharacterFireMontage);
	}

	KickSlide();

	if (WeaponDataAsset->ShootParticle)
	{
		if (UParticleSystemComponent* MuzzleFlash = UGameplayStatics::SpawnEmitterAttached(
			WeaponDataAsset->ShootParticle, MeshComponent, GunBase::MuzzleSocket,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true))
		{
			MuzzleFlash->SetRelativeScale3D(FVector(ShootParticleScale));
		}
	}

	if (WeaponDataAsset->FireSound)
	{
		UGameplayStatics::PlaySound2D(this, WeaponDataAsset->FireSound);
	}

	if (WeaponDataAsset->FireCameraShake)
	{
		PlayerController.ClientStartCameraShake(WeaponDataAsset->FireCameraShake);
	}
}

void AGunBase::SpawnImpact(const FHitResult& Hit) const
{
	if (!Hit.bBlockingHit || !WeaponDataAsset->ImpactActorClass)
	{
		return;
	}

	GetWorld()->SpawnActor<AActor>(WeaponDataAsset->ImpactActorClass, Hit.ImpactPoint, Hit.ImpactNormal.Rotation());
}

void AGunBase::SpawnTracer(const FVector& ShotEnd) const
{
	if (!WeaponDataAsset->TracerSystem)
	{
		return;
	}

	const FVector MuzzleLocation = MeshComponent->GetSocketLocation(GunBase::MuzzleSocket);

	UNiagaraComponent* Tracer = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(), WeaponDataAsset->TracerSystem, MuzzleLocation, FRotator::ZeroRotator, GunBase::TracerScale);

	if (Tracer)
	{
		Tracer->SetVariableVec3(GunBase::TracerMuzzlePosition, MuzzleLocation);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(Tracer, GunBase::TracerImpactPositions, { ShotEnd });
		Tracer->SetVariableBool(GunBase::TracerTrigger, true);
	}
}

// -------------------------------------------------------------------------------------------------
// Recoil

void AGunBase::ApplyRecoil()
{
	const TArray<FVector2D>& Pattern = WeaponDataAsset->RecoilPattern;
	if (Pattern.IsEmpty())
	{
		return;
	}

	FVector2D Kick = Pattern[FMath::Min(RecoilIndex, Pattern.Num() - 1)];
	Kick.X += FMath::FRandRange(-0.1f, 0.1f);

	RecoilTarget.Pitch += Kick.Y;
	RecoilTarget.Yaw += Kick.X;

	++RecoilIndex;
	TimeSinceLastShot = 0.f;
}

void AGunBase::UpdateRecoil(float DeltaTime)
{
	APawn* OwnerPawn = GetOwner<APawn>();
	if (!WeaponDataAsset || !OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	TimeSinceLastShot += DeltaTime;

	// Once the player stops shooting the pattern restarts. The view is intentionally not
	// pulled back down: the player compensates recoil manually.
	if (TimeSinceLastShot > WeaponDataAsset->RecoilResetDelay)
	{
		ResetRecoil();
		return;
	}

	// Feed the kick into the view gradually instead of snapping it
	const FRotator NewRecoil = FMath::RInterpTo(RecoilCurrent, RecoilTarget, DeltaTime, WeaponDataAsset->RecoilApplySpeed);
	const FRotator Delta = NewRecoil - RecoilCurrent;
	OwnerPawn->AddControllerPitchInput(-Delta.Pitch);
	OwnerPawn->AddControllerYawInput(Delta.Yaw);
	RecoilCurrent = NewRecoil;
}

void AGunBase::ResetRecoil()
{
	RecoilTarget = FRotator::ZeroRotator;
	RecoilCurrent = FRotator::ZeroRotator;
	RecoilIndex = 0;
}

// -------------------------------------------------------------------------------------------------
// Spread, slide and smoke

void AGunBase::UpdateSpread(float DeltaTime)
{
	if (!WeaponDataAsset)
	{
		return;
	}

	float TargetSpread = WeaponDataAsset->BaseSpread;

	if (const APawn* OwnerPawn = GetOwner<APawn>())
	{
		TargetSpread += FMath::GetMappedRangeValueClamped(
			FVector2D(0.f, GunBase::FullMovementSpreadSpeed),
			FVector2D(0.f, WeaponDataAsset->MovementSpread),
			OwnerPawn->GetVelocity().Size());
	}

	CurrentSpread = FMath::FInterpTo(CurrentSpread, TargetSpread, DeltaTime, WeaponDataAsset->SpreadRecoverySpeed);
}

void AGunBase::KickSlide()
{
	SlideMesh->SetRelativeLocation(SlideRestLocation - FVector(SlideKickBack, 0.f, 0.f));
}

void AGunBase::UpdateSlide(float DeltaTime)
{
	const FVector Current = SlideMesh->GetRelativeLocation();
	if (!Current.Equals(SlideRestLocation, 0.01f))
	{
		SlideMesh->SetRelativeLocation(FMath::VInterpTo(Current, SlideRestLocation, DeltaTime, SlideReturnSpeed));
	}
}

void AGunBase::UpdateMuzzleSmoke(float DeltaTime)
{
	if (!bMuzzleSmokePending || !WeaponDataAsset)
	{
		return;
	}

	MuzzleSmokeTimer += DeltaTime;
	if (MuzzleSmokeTimer < WeaponDataAsset->SmokeDelay)
	{
		return;
	}

	bMuzzleSmokePending = false;

	if (WeaponDataAsset->MuzzleSmokeSystem)
	{
		UNiagaraFunctionLibrary::SpawnSystemAttached(
			WeaponDataAsset->MuzzleSmokeSystem, MeshComponent, GunBase::MuzzleSocket,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true);
	}
}
