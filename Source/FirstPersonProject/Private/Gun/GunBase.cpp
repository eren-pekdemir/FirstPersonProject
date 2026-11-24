#include "Gun/GunBase.h"

#include "Gun/WeaponViewAnimInstance.h"
#include "Perception/AISense_Hearing.h"
#include "Zombie/ZombieCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/DamageEvents.h"
#include "FirstPersonProject.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/DamageTarget.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "Particles/ParticleSystemComponent.h"
#include "TimerManager.h"

namespace GunBase
{
	const FName MuzzleSocket(TEXT("MuzzleSocket"));
	const FName HeadBone(TEXT("head"));
	const FName HeadTag(TEXT("Head"));

	// Niagara user parameters exposed by the tracer system
	const FName TracerMuzzlePosition(TEXT("MuzzlePosition"));
	const FName TracerImpactPositions(TEXT("ImpactPositions"));
	const FName TracerTrigger(TEXT("Trigger"));
	const FVector TracerScale(0.1f);

	/** Owner speed at which the full movement spread is applied */
	constexpr float FullMovementSpreadSpeed = 600.f;

	/** Trigger released within this time after a shot plays the tail sound */
	constexpr float TailSoundWindow = 0.25f;

	/** How far below a weapon lying in the world the ground is searched for */
	constexpr float RestTraceDistance = 500.f;

	/** Gap kept between a resting weapon and the ground */
	constexpr float RestHeight = 0.5f;

	/** How far ahead of a held weapon's muzzle its flash light is placed */
	constexpr float FirstPersonLightForward = 110.f;

	/** Recoil smaller than this is considered recovered */
	constexpr float RecoveryEpsilon = 0.01f;
}

AGunBase::AGunBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	SetRootComponent(MeshComponent);

	SlideMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SlideMesh"));
	SlideMesh->SetupAttachment(MeshComponent);

	ViewMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ViewMesh"));
	ViewMesh->SetupAttachment(MeshComponent);
	ViewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ViewMesh->SetGenerateOverlapEvents(false);
	ViewMesh->SetVisibility(false);

	PickupCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("PickupCollision"));
	PickupCollision->SetupAttachment(MeshComponent);
	PickupCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickupCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PickupCollision->SetGenerateOverlapEvents(false);
	PickupCollision->SetCanEverAffectNavigation(false);

	MuzzleLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("MuzzleLight"));
	MuzzleLight->SetupAttachment(MeshComponent, GunBase::MuzzleSocket);
	MuzzleLight->SetIntensityUnits(ELightUnits::Candelas);
	MuzzleLight->SetIntensity(0.f);
	MuzzleLight->SetAttenuationRadius(350.f);
	MuzzleLight->SetLightColor(FLinearColor(1.f, 0.62f, 0.3f));
	MuzzleLight->SetCastShadows(false);
	MuzzleLight->SetVisibility(false);
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

		if (!bHasAmmoState)
		{
			AmmoInMagazine = WeaponDataAsset->MagazineSize;
			ReserveAmmo = WeaponDataAsset->StartingReserveAmmo;
			bHasAmmoState = true;
		}
	}

	SlideRestLocation = SlideMesh->GetRelativeLocation();
	SetupViewMesh();
	FitPickupCollision();

	// Blueprints may author the meshes with collision disabled (held state).
	// Re-apply the current state so guns lying in the world can be hit by the interaction trace.
	SetHeld(bIsHeld);
}

void AGunBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearAllTimersForObject(this);

	Super::EndPlay(EndPlayReason);
}

void AGunBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateRecoil(DeltaTime);
	UpdateSpread(DeltaTime);
	UpdateSlide(DeltaTime);
	UpdateMuzzleSmoke(DeltaTime);
	UpdateMuzzleLight(DeltaTime);
}

void AGunBase::FitPickupCollision()
{
	FBox Bounds(ForceInit);
	for (const UStaticMeshComponent* Mesh : { MeshComponent.Get(), SlideMesh.Get() })
	{
		if (Mesh && Mesh->GetStaticMesh())
		{
			Bounds += Mesh->GetStaticMesh()->GetBoundingBox().TransformBy(Mesh->GetRelativeTransform());
		}
	}

	if (UsesViewMesh())
	{
		// The animated mesh is what the player sees and aims at
		const FVector& Extent = WeaponDataAsset->PickupExtent;
		Bounds = Extent.IsNearlyZero()
			? WeaponDataAsset->ViewMesh->GetBounds().GetBox().TransformBy(ViewMesh->GetRelativeTransform())
			: FBox(ViewMesh->GetRelativeLocation() - Extent, ViewMesh->GetRelativeLocation() + Extent);
	}

	if (!Bounds.IsValid)
	{
		return;
	}

	// Slightly larger than the weapon so thin guns are easy to aim at
	constexpr float Padding = 4.f;
	PickupCollision->SetRelativeLocation(Bounds.GetCenter());
	PickupCollision->SetBoxExtent(Bounds.GetExtent() + FVector(Padding));
}

float AGunBase::GetFirstPersonEffectScale() const
{
	if (!bFirstPersonView)
	{
		return 1.f;
	}

	// The flash sits right on the line of sight when aiming: keep it small enough to see the target
	return FirstPersonEffectScale * (bIsAiming ? AimEffectScale : 1.f);
}

void AGunBase::RestOnGround()
{
	if (!UsesViewMesh() || !GetWorld())
	{
		return;
	}

	// On its side, facing the way the actor faces
	const FQuat Facing = FRotator(0.f, GetActorRotation().Yaw, 0.f).Quaternion();
	ViewMesh->SetWorldRotation(Facing * WeaponDataAsset->WorldRestRotation.Quaternion());

	// Drop it onto whatever is below. The model's vertex bounds are used: component bounds come from the
	// physics asset, which can be far off for imported rigs (scaled bones, loose parts).
	auto ModelBox = [this]() { return WeaponDataAsset->ViewMesh->GetBounds().GetBox().TransformBy(ViewMesh->GetComponentTransform()); };
	FBox Box = ModelBox();
	const FBoxSphereBounds Bounds(Box);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GunRest), false, this);
	if (GetOwner())
	{
		Params.AddIgnoredActor(GetOwner());
	}

	FHitResult Hit;
	const FVector Start = Bounds.Origin + FVector(0.f, 0.f, Bounds.BoxExtent.Z);
	const FVector End = Bounds.Origin - FVector(0.f, 0.f, GunBase::RestTraceDistance);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		const float Bottom = Bounds.Origin.Z - Bounds.BoxExtent.Z;
		ViewMesh->AddWorldOffset(FVector(0.f, 0.f, Hit.ImpactPoint.Z + GunBase::RestHeight - Bottom));
	}

	// The pickup box follows the model, which no longer matches the actor axes
	constexpr float Padding = 4.f;
	Box = ModelBox();
	PickupCollision->SetWorldRotation(FQuat::Identity);
	PickupCollision->SetWorldLocation(Box.GetCenter());
	PickupCollision->SetBoxExtent(Box.GetExtent() + FVector(Padding));
}

bool AGunBase::UsesViewMesh() const
{
	return WeaponDataAsset && WeaponDataAsset->ViewMesh && ViewMesh;
}

void AGunBase::SetupViewMesh()
{
	if (!UsesViewMesh())
	{
		return;
	}

	ViewMesh->SetSkeletalMesh(WeaponDataAsset->ViewMesh);
	if (WeaponDataAsset->ViewAnimClass)
	{
		ViewMesh->SetAnimInstanceClass(WeaponDataAsset->ViewAnimClass);
	}
	else if (!WeaponDataAsset->ViewPartKicks.IsEmpty())
	{
		// Models without an Anim Blueprint still get their slide or pump thrown back by each shot
		ViewMesh->SetAnimInstanceClass(UWeaponViewAnimInstance::StaticClass());
	}
	ViewMesh->SetRelativeTransform(WeaponDataAsset->ViewMeshOffset);
	ViewMesh->SetVisibility(true);

	for (const FName& Bone : WeaponDataAsset->ViewHiddenBones)
	{
		ViewMesh->HideBoneByName(Bone, PBO_None);
	}

	// The static meshes of the Blueprint stay for the actor root and collision but are no longer drawn
	TArray<UStaticMeshComponent*> StaticMeshes;
	GetComponents<UStaticMeshComponent>(StaticMeshes);
	for (UStaticMeshComponent* StaticMesh : StaticMeshes)
	{
		StaticMesh->SetVisibility(false);
	}

	FName Socket;
	MuzzleLight->AttachToComponent(GetMuzzleComponent(Socket), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
}

USceneComponent* AGunBase::GetMuzzleComponent(FName& OutSocket) const
{
	if (UsesViewMesh() && ViewMesh->DoesSocketExist(WeaponDataAsset->ViewMuzzleSocket))
	{
		OutSocket = WeaponDataAsset->ViewMuzzleSocket;
		return ViewMesh;
	}

	OutSocket = GunBase::MuzzleSocket;
	return MeshComponent;
}

FVector AGunBase::GetVisualMuzzleLocation() const
{
	FName Socket;
	const FVector Muzzle = GetMuzzleComponent(Socket)->GetSocketLocation(Socket);

	const AActor* OwnerActor = GetOwner();
	const UCameraComponent* Camera = OwnerActor ? OwnerActor->FindComponentByClass<UCameraComponent>() : nullptr;
	if (!bFirstPersonView || !Camera)
	{
		return Muzzle;
	}

	// Same transform the renderer applies to first person primitives: depth scaled, sides scaled by the FOV ratio
	const float Scale = Camera->bEnableFirstPersonScale ? Camera->FirstPersonScale : 1.f;
	const float HalfFOV = FMath::DegreesToRadians(FMath::Clamp(Camera->FieldOfView, 1.f, 170.f) * 0.5f);
	const float HalfFirstPersonFOV = FMath::DegreesToRadians(FMath::Clamp(Camera->FirstPersonFieldOfView, 1.f, 170.f) * 0.5f);
	const float FOVCorrection = Camera->bEnableFirstPersonFieldOfView ? FMath::Tan(HalfFOV) / FMath::Tan(HalfFirstPersonFOV) : 1.f;

	const FTransform CameraTransform = Camera->GetComponentTransform();
	FVector Local = CameraTransform.InverseTransformPositionNoScale(Muzzle);
	Local.X *= Scale;
	Local.Y *= Scale * FOVCorrection;
	Local.Z *= Scale * FOVCorrection;
	return CameraTransform.TransformPositionNoScale(Local);
}

void AGunBase::PlayViewMontage(UAnimMontage* Montage, float FitDuration) const
{
	UAnimInstance* AnimInstance = UsesViewMesh() ? ViewMesh->GetAnimInstance() : nullptr;
	if (!Montage || !AnimInstance)
	{
		return;
	}

	const float Rate = FitDuration > 0.f ? Montage->GetPlayLength() / FitDuration : 1.f;
	AnimInstance->Montage_Play(Montage, FMath::Max(Rate, KINDA_SMALL_NUMBER));
}

void AGunBase::SetFirstPersonView(bool bNewFirstPersonView)
{
	bFirstPersonView = bNewFirstPersonView;

	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (Primitive != PickupCollision)
		{
			ApplyFirstPersonView(Primitive);
		}
	}
}

void AGunBase::ApplyFirstPersonView(UPrimitiveComponent* Component) const
{
	if (Component)
	{
		Component->SetFirstPersonPrimitiveType(bFirstPersonView ? EFirstPersonPrimitiveType::FirstPerson : EFirstPersonPrimitiveType::None);
	}
}

void AGunBase::UpdateMuzzleLight(float DeltaTime)
{
	if (MuzzleLightTimeLeft <= 0.f)
	{
		return;
	}

	MuzzleLightTimeLeft -= DeltaTime;
	if (MuzzleLightTimeLeft <= 0.f)
	{
		MuzzleLight->SetVisibility(false);
		return;
	}

	const float Alpha = MuzzleLightTimeLeft / FMath::Max(MuzzleLightDuration, KINDA_SMALL_NUMBER);
	MuzzleLight->SetIntensity(MuzzleLightIntensity * Alpha);
}

void AGunBase::SetHeld(bool bNewHeld)
{
	bIsHeld = bNewHeld;

	const ECollisionEnabled::Type Collision = bIsHeld ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics;
	MeshComponent->SetCollisionEnabled(Collision);
	SlideMesh->SetCollisionEnabled(Collision);
	PickupCollision->SetCollisionEnabled(bIsHeld ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);

	if (!bIsHeld)
	{
		StopFire();
		CancelReload();
		ResetRecoil();
		bIsAiming = false;
		bMuzzleSmokePending = false;
		SlideMesh->SetRelativeLocation(SlideRestLocation);
		SetActorHiddenInGame(false);
		RestOnGround();
	}
	else if (UsesViewMesh())
	{
		// In the hands the model sits where the arms hold it
		ViewMesh->SetRelativeTransform(WeaponDataAsset->ViewMeshOffset);
	}

	// Everything updated on tick only matters while the weapon is in someone's hands
	SetActorTickEnabled(bIsHeld);
}

void AGunBase::SetHolstered(bool bNewHolstered)
{
	if (bNewHolstered)
	{
		StopFire();
		CancelReload();
		ResetRecoil();
		bIsAiming = false;
		bMuzzleSmokePending = false;
	}

	SetActorHiddenInGame(bNewHolstered);
	SetActorTickEnabled(bIsHeld && !bNewHolstered);

	MuzzleLightTimeLeft = 0.f;
	MuzzleLight->SetVisibility(false);
}

void AGunBase::OnEquipped()
{
	const float EquipTime = WeaponDataAsset ? WeaponDataAsset->EquipTime : 0.f;
	EquipReadyTime = GetWorld()->GetTimeSeconds() + EquipTime;

	if (WeaponDataAsset)
	{
		PlayWeaponSound(WeaponDataAsset->EquipSound);
	}
}

FText AGunBase::GetDisplayName() const
{
	return WeaponDataAsset ? WeaponDataAsset->DisplayName : FText::FromString(GetName());
}

int32 AGunBase::GetMagazineSize() const
{
	return WeaponDataAsset ? WeaponDataAsset->MagazineSize : 0;
}

int32 AGunBase::AddReserveAmmo(int32 Amount)
{
	if (!WeaponDataAsset || Amount <= 0)
	{
		return 0;
	}

	const int32 Added = FMath::Min(Amount, WeaponDataAsset->MaxReserveAmmo - ReserveAmmo);
	ReserveAmmo += FMath::Max(Added, 0);
	return FMath::Max(Added, 0);
}

float AGunBase::GetCurrentSpread() const
{
	const float AimMultiplier = (bIsAiming && WeaponDataAsset) ? WeaponDataAsset->AimSpreadMultiplier : 1.f;
	return CurrentSpread * AimMultiplier;
}

// -------------------------------------------------------------------------------------------------
// Firing

bool AGunBase::CanFire() const
{
	if (!WeaponDataAsset || AmmoInMagazine <= 0)
	{
		return false;
	}

	if (GetWorld()->GetTimeSeconds() < EquipReadyTime)
	{
		return false;
	}

	// Per round reloads can be interrupted by the trigger, magazine reloads can't
	return !bIsReloading || WeaponDataAsset->ReloadType == EWeaponReloadType::PerRound;
}

void AGunBase::StartFire()
{
	if (!WeaponDataAsset)
	{
		return;
	}

	if (AmmoInMagazine <= 0)
	{
		if (!bIsReloading)
		{
			DryFire();
		}
		return;
	}

	if (!CanFire())
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
	const bool bWasFiring = GetWorldTimerManager().IsTimerActive(AutoFireTimer);
	GetWorldTimerManager().ClearTimer(AutoFireTimer);

	// Automatic weapons ring out once the burst ends
	const bool bRecentShot = GetWorld() && GetWorld()->GetTimeSeconds() - LastFireTime < GunBase::TailSoundWindow;
	if (bWasFiring && bRecentShot && WeaponDataAsset && WeaponDataAsset->FireMode == EWeaponFireMode::Automatic)
	{
		PlayRandomSound(WeaponDataAsset->TailSounds);
	}
}

void AGunBase::DryFire()
{
	StopFire();
	PlayWeaponSound(WeaponDataAsset->DryFireSound);
	OnEmpty.Broadcast(*this);
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

	if (AmmoInMagazine <= 0)
	{
		DryFire();
		return;
	}

	if (bIsReloading)
	{
		// Only per round reloads reach this point: the trigger interrupts them
		CancelReload();
	}

	LastFireTime = GetWorld()->GetTimeSeconds();
	--AmmoInMagazine;

	PlayFireFeedback(*PlayerController);

	// Gunshots carry: zombies hear them and come to look
	UAISense_Hearing::ReportNoiseEvent(GetWorld(), GetActorLocation(), 1.f, GetOwner(), 0.f, TEXT("Gunshot"));

	TArray<FVector> ShotEnds;
	const FGunShotResult Result = FireTraces(*PlayerController, ShotEnds);
	SpawnTracer(ShotEnds);

	if (Result.bHitDamageable)
	{
		OnHit.Broadcast(Result);
	}

	ApplyRecoil();
	CurrentSpread = FMath::Min(CurrentSpread + WeaponDataAsset->SpreadPerShot, WeaponDataAsset->MaxSpread);

	bMuzzleSmokePending = true;
	MuzzleSmokeTimer = 0.f;

	OnFired.Broadcast(*this);

	if (AmmoInMagazine <= 0)
	{
		StopFire();
		OnEmpty.Broadcast(*this);
	}
}

FGunShotResult AGunBase::FireTraces(const APlayerController& PlayerController, TArray<FVector>& OutShotEnds)
{
	// The crosshair sits at the center of the screen, so shots follow the view direction
	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController.GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector ViewDirection = ViewRotation.Vector();
	const float SpreadHalfAngle = GetCurrentSpread() * WeaponDataAsset->SpreadToAngle;
	const int32 Pellets = FMath::Max(WeaponDataAsset->PelletsPerShot, 1);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(GunFire), true, this);
	Params.AddIgnoredActor(GetOwner());
	Params.bReturnPhysicalMaterial = true;

	FGunShotResult Result;
	OutShotEnds.Reserve(Pellets);

	for (int32 PelletIndex = 0; PelletIndex < Pellets; ++PelletIndex)
	{
		// Single bullets deviate inside the crosshair, pellets also inside the fixed shotgun cone
		const float ConeHalfAngle = SpreadHalfAngle + (Pellets > 1 ? WeaponDataAsset->PelletConeAngle : 0.f);
		const FVector ShotDirection = ConeHalfAngle > 0.f
			? FMath::VRandCone(ViewDirection, FMath::DegreesToRadians(ConeHalfAngle))
			: ViewDirection;

		const FVector TraceEnd = ViewLocation + ShotDirection * WeaponDataAsset->MaxRange;

		FHitResult Hit;
		GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, Params);

		OutShotEnds.Add(Hit.bBlockingHit ? Hit.ImpactPoint : TraceEnd);

		if (Hit.bBlockingHit)
		{
			// Zombies make their own flesh impact (blood, sound), the generic one is for the world
			if (!Cast<AZombieCharacter>(Hit.GetActor()))
			{
				SpawnImpact(Hit);
			}
			ApplyHitDamage(Hit, ShotDirection, Result);
		}
	}

	return Result;
}

void AGunBase::ApplyHitDamage(const FHitResult& Hit, const FVector& ShotDirection, FGunShotResult& InOutResult)
{
	if (UPrimitiveComponent* HitComponent = Hit.GetComponent())
	{
		if (HitComponent->IsSimulatingPhysics())
		{
			HitComponent->AddImpulseAtLocation(ShotDirection * WeaponDataAsset->HitImpulse, Hit.ImpactPoint, Hit.BoneName);
		}
	}

	AActor* HitActor = Hit.GetActor();
	if (!HitActor || !HitActor->CanBeDamaged())
	{
		return;
	}

	const bool bHeadshot = Hit.BoneName == GunBase::HeadBone
		|| (Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(GunBase::HeadTag));

	const float Damage = WeaponDataAsset->Damage * (bHeadshot ? WeaponDataAsset->HeadshotMultiplier : 1.f)
		* WeaponDataAsset->GetRangeDamageScale(Hit.Distance);

	const ADamageTarget* Target = Cast<ADamageTarget>(HitActor);
	const AZombieCharacter* Zombie = Cast<AZombieCharacter>(HitActor);
	const bool bWasAlive = Zombie ? Zombie->IsAlive() : (!Target || Target->IsAlive());

	const APawn* OwnerPawn = GetOwner<APawn>();
	const float DamageDealt = UGameplayStatics::ApplyPointDamage(
		HitActor, Damage, ShotDirection, Hit, OwnerPawn ? OwnerPawn->GetController() : nullptr, this, UDamageType::StaticClass());

	if (DamageDealt <= 0.f)
	{
		return;
	}

	if (!InOutResult.bHitDamageable)
	{
		InOutResult.HitLocation = Hit.ImpactPoint;
	}

	InOutResult.bHitDamageable = true;
	InOutResult.Damage += DamageDealt;
	InOutResult.bHeadshot |= bHeadshot;
	const bool bIsAlive = Zombie ? Zombie->IsAlive() : (!Target || Target->IsAlive());
	InOutResult.bKilled |= (Target || Zombie) && bWasAlive && !bIsAlive;
}

void AGunBase::PlayFireFeedback(APlayerController& PlayerController)
{
	FName MuzzleSocketName;
	USceneComponent* MuzzleComponent = GetMuzzleComponent(MuzzleSocketName);

	if (ACharacter* OwnerCharacter = GetOwner<ACharacter>())
	{
		OwnerCharacter->PlayAnimMontage(WeaponDataAsset->CharacterFireMontage);
	}

	KickSlide();
	PlayViewMontage(WeaponDataAsset->WeaponFireMontage);
	if (UWeaponViewAnimInstance* PartsInstance = UsesViewMesh() ? Cast<UWeaponViewAnimInstance>(ViewMesh->GetAnimInstance()) : nullptr)
	{
		PartsInstance->SetEmpty(AmmoInMagazine <= 0);
		PartsInstance->KickParts();
	}

	if (WeaponDataAsset->ShootParticle)
	{
		const FVector FlashScale(ShootParticleScale * GetFirstPersonEffectScale());
		if (bFirstPersonView)
		{
			// Particle sprites ignore first person rendering: place the flash where the player sees the muzzle
			// and point it where the shot goes, whatever axes the model's muzzle socket has
			FVector ViewLocation;
			FRotator ViewRotation;
			PlayerController.GetPlayerViewPoint(ViewLocation, ViewRotation);
			UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), WeaponDataAsset->ShootParticle,
				FTransform(ViewRotation + WeaponDataAsset->ViewFlashRotation, GetVisualMuzzleLocation(), FlashScale), true);
		}
		else if (UParticleSystemComponent* MuzzleFlash = UGameplayStatics::SpawnEmitterAttached(
			WeaponDataAsset->ShootParticle, MuzzleComponent, MuzzleSocketName,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true))
		{
			MuzzleFlash->SetRelativeScale3D(FlashScale);
		}
	}

	if (MuzzleLightIntensity > 0.f)
	{
		if (bFirstPersonView)
		{
			// Held weapons sit a hand's width from the arms: light the scene ahead instead of blowing out the skin
			FVector ViewLocation;
			FRotator ViewRotation;
			PlayerController.GetPlayerViewPoint(ViewLocation, ViewRotation);
			MuzzleLight->SetWorldLocation(MuzzleComponent->GetSocketLocation(MuzzleSocketName) + ViewRotation.Vector() * GunBase::FirstPersonLightForward);
		}

		MuzzleLightTimeLeft = MuzzleLightDuration;
		MuzzleLight->SetIntensity(MuzzleLightIntensity);
		MuzzleLight->SetVisibility(true);
	}

	if (WeaponDataAsset->FireSounds.IsEmpty())
	{
		PlayWeaponSound(WeaponDataAsset->FireSound);
	}
	else
	{
		PlayRandomSound(WeaponDataAsset->FireSounds);
	}

	if (WeaponDataAsset->FireMode == EWeaponFireMode::Single)
	{
		PlayRandomSound(WeaponDataAsset->TailSounds);
	}

	if (WeaponDataAsset->CycleSound)
	{
		TWeakObjectPtr<USoundBase> CycleSound = WeaponDataAsset->CycleSound;
		GetWorldTimerManager().SetTimer(CycleSoundTimer, FTimerDelegate::CreateWeakLambda(this, [this, CycleSound]()
		{
			PlayWeaponSound(CycleSound.Get());
		}), WeaponDataAsset->CycleSoundDelay, false);
	}

	if (WeaponDataAsset->FireCameraShake)
	{
		const float ShakeScale = WeaponDataAsset->FireCameraShakeScale * (bIsAiming ? WeaponDataAsset->AimRecoilMultiplier : 1.f);
		PlayerController.ClientStartCameraShake(WeaponDataAsset->FireCameraShake, ShakeScale);
	}
}

void AGunBase::SpawnImpact(const FHitResult& Hit) const
{
	if (!WeaponDataAsset->ImpactActorClass)
	{
		return;
	}

	GetWorld()->SpawnActor<AActor>(WeaponDataAsset->ImpactActorClass, Hit.ImpactPoint, Hit.ImpactNormal.Rotation());
}

void AGunBase::SpawnTracer(const TArray<FVector>& ShotEnds) const
{
	if (!WeaponDataAsset->TracerSystem || ShotEnds.IsEmpty())
	{
		return;
	}

	const FVector MuzzleLocation = GetVisualMuzzleLocation();

	UNiagaraComponent* Tracer = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(), WeaponDataAsset->TracerSystem, MuzzleLocation, FRotator::ZeroRotator, GunBase::TracerScale);

	if (Tracer)
	{
		Tracer->SetVariableVec3(GunBase::TracerMuzzlePosition, MuzzleLocation);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(Tracer, GunBase::TracerImpactPositions, ShotEnds);
		Tracer->SetVariableBool(GunBase::TracerTrigger, true);
	}
}

void AGunBase::PlayWeaponSound(USoundBase* Sound) const
{
	if (Sound)
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}
}

void AGunBase::PlayRandomSound(const TArray<TObjectPtr<USoundBase>>& Sounds) const
{
	if (!Sounds.IsEmpty())
	{
		PlayWeaponSound(Sounds[FMath::RandRange(0, Sounds.Num() - 1)]);
	}
}

// -------------------------------------------------------------------------------------------------
// Reloading

bool AGunBase::StartReload()
{
	if (!WeaponDataAsset || bIsReloading || ReserveAmmo <= 0 || AmmoInMagazine >= WeaponDataAsset->MagazineSize)
	{
		return false;
	}

	StopFire();

	bIsReloading = true;
	ReloadStartTime = GetWorld()->GetTimeSeconds();

	const bool bEmpty = AmmoInMagazine <= 0;
	const bool bPerRound = WeaponDataAsset->ReloadType == EWeaponReloadType::PerRound;

	if (bPerRound)
	{
		const int32 RoundsNeeded = FMath::Min(WeaponDataAsset->MagazineSize - AmmoInMagazine, ReserveAmmo);
		ReloadDuration = WeaponDataAsset->ReloadTime + RoundsNeeded * WeaponDataAsset->TimePerRound;
		GetWorldTimerManager().SetTimer(ReloadTimer, this, &AGunBase::InsertRound, WeaponDataAsset->TimePerRound, true, WeaponDataAsset->ReloadTime);
	}
	else
	{
		ReloadDuration = (bEmpty && WeaponDataAsset->EmptyReloadTime > 0.f) ? WeaponDataAsset->EmptyReloadTime : WeaponDataAsset->ReloadTime;
		GetWorldTimerManager().SetTimer(ReloadTimer, this, &AGunBase::FinishMagazineReload, ReloadDuration, false);
	}

	// Stretch the authored sound timeline when the reload is longer than the base time (empty reload)
	const float TimeScale = bPerRound ? 1.f : ReloadDuration / FMath::Max(WeaponDataAsset->ReloadTime, KINDA_SMALL_NUMBER);
	for (const FWeaponTimedSound& TimedSound : WeaponDataAsset->ReloadSounds)
	{
		ScheduleReloadSound(TimedSound.Sound, TimedSound.Time * TimeScale);
	}

	if (!bPerRound)
	{
		UAnimMontage* WeaponMontage = (bEmpty && WeaponDataAsset->WeaponReloadEmptyMontage) ? WeaponDataAsset->WeaponReloadEmptyMontage : WeaponDataAsset->WeaponReloadMontage;
		PlayViewMontage(WeaponMontage, ReloadDuration);
	}

	if (ACharacter* OwnerCharacter = GetOwner<ACharacter>())
	{
		if (UAnimMontage* Montage = WeaponDataAsset->ReloadMontage)
		{
			const float PlayRate = Montage->GetPlayLength() / FMath::Max(ReloadDuration, KINDA_SMALL_NUMBER);
			OwnerCharacter->PlayAnimMontage(Montage, PlayRate);
		}
	}

	return true;
}

void AGunBase::ScheduleReloadSound(USoundBase* Sound, float Delay)
{
	if (!Sound)
	{
		return;
	}

	if (Delay <= 0.f)
	{
		PlayWeaponSound(Sound);
		return;
	}

	TWeakObjectPtr<USoundBase> WeakSound = Sound;
	FTimerHandle& Handle = ReloadSoundTimers.AddDefaulted_GetRef();
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, WeakSound]()
	{
		PlayWeaponSound(WeakSound.Get());
	}), Delay, false);
}

void AGunBase::FinishMagazineReload()
{
	const int32 RoundsNeeded = WeaponDataAsset->MagazineSize - AmmoInMagazine;
	const int32 RoundsLoaded = FMath::Min(RoundsNeeded, ReserveAmmo);

	AmmoInMagazine += RoundsLoaded;
	ReserveAmmo -= RoundsLoaded;

	EndReload();
}

void AGunBase::InsertRound()
{
	if (ReserveAmmo <= 0 || AmmoInMagazine >= WeaponDataAsset->MagazineSize)
	{
		PlayWeaponSound(WeaponDataAsset->ReloadEndSound);
		EndReload();
		return;
	}

	++AmmoInMagazine;
	--ReserveAmmo;
	PlayWeaponSound(WeaponDataAsset->RoundInsertSound);

	if (ReserveAmmo <= 0 || AmmoInMagazine >= WeaponDataAsset->MagazineSize)
	{
		PlayWeaponSound(WeaponDataAsset->ReloadEndSound);
		EndReload();
	}
}

void AGunBase::EndReload()
{
	bIsReloading = false;

	// A locked slide goes home with the new magazine
	if (UWeaponViewAnimInstance* PartsInstance = UsesViewMesh() ? Cast<UWeaponViewAnimInstance>(ViewMesh->GetAnimInstance()) : nullptr)
	{
		PartsInstance->SetEmpty(AmmoInMagazine <= 0);
	}
	GetWorldTimerManager().ClearTimer(ReloadTimer);

	for (FTimerHandle& Handle : ReloadSoundTimers)
	{
		GetWorldTimerManager().ClearTimer(Handle);
	}
	ReloadSoundTimers.Reset();
}

void AGunBase::CancelReload()
{
	if (!bIsReloading)
	{
		return;
	}

	EndReload();

	if (UAnimInstance* ViewAnim = UsesViewMesh() ? ViewMesh->GetAnimInstance() : nullptr)
	{
		ViewAnim->Montage_Stop(0.2f);
	}

	ACharacter* OwnerCharacter = GetOwner<ACharacter>();
	if (OwnerCharacter && WeaponDataAsset && WeaponDataAsset->ReloadMontage)
	{
		OwnerCharacter->StopAnimMontage(WeaponDataAsset->ReloadMontage);
	}
}

float AGunBase::GetReloadProgress() const
{
	if (!bIsReloading || ReloadDuration <= 0.f)
	{
		return 0.f;
	}

	return FMath::Clamp((GetWorld()->GetTimeSeconds() - ReloadStartTime) / ReloadDuration, 0.f, 1.f);
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

	const float Multiplier = bIsAiming ? WeaponDataAsset->AimRecoilMultiplier : 1.f;

	FVector2D Kick = Pattern[FMath::Min(RecoilIndex, Pattern.Num() - 1)];
	Kick.X += FMath::FRandRange(-0.1f, 0.1f);

	RecoilTarget.Pitch += Kick.Y * Multiplier;
	RecoilTarget.Yaw += Kick.X * Multiplier;

	++RecoilIndex;
	TimeSinceLastShot = 0.f;
	RecoilRecoveryRemaining = 0.f;
}

void AGunBase::UpdateRecoil(float DeltaTime)
{
	APawn* OwnerPawn = GetOwner<APawn>();
	if (!WeaponDataAsset || !OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	TimeSinceLastShot += DeltaTime;

	// Once the player stops shooting the pattern restarts and part of the climb is pulled back,
	// the rest has to be compensated manually.
	if (TimeSinceLastShot > WeaponDataAsset->RecoilResetDelay)
	{
		if (RecoilIndex > 0)
		{
			RecoilRecoveryRemaining = RecoilCurrent.Pitch * WeaponDataAsset->RecoilRecoveryFraction;
			ResetRecoil();
		}

		if (RecoilRecoveryRemaining > GunBase::RecoveryEpsilon)
		{
			const float Step = FMath::Min(RecoilRecoveryRemaining,
				RecoilRecoveryRemaining * FMath::Clamp(DeltaTime * WeaponDataAsset->RecoilRecoverySpeed, 0.f, 1.f) + DeltaTime * 0.5f);
			AddViewRotation(*OwnerPawn, FRotator(-Step, 0.f, 0.f));
			RecoilRecoveryRemaining -= Step;
		}
		return;
	}

	// Feed the kick into the view gradually instead of snapping it
	const FRotator NewRecoil = FMath::RInterpTo(RecoilCurrent, RecoilTarget, DeltaTime, WeaponDataAsset->RecoilApplySpeed);
	const FRotator Delta = NewRecoil - RecoilCurrent;
	AddViewRotation(*OwnerPawn, FRotator(Delta.Pitch, Delta.Yaw, 0.f));
	RecoilCurrent = NewRecoil;
}

void AGunBase::AddViewRotation(APawn& OwnerPawn, const FRotator& Delta)
{
	// Written to the control rotation directly: the look input path scales pitch and yaw,
	// which would make the authored recoil degrees meaningless.
	AController* OwnerController = OwnerPawn.GetController();
	if (!OwnerController)
	{
		return;
	}

	FRotator ControlRotation = OwnerController->GetControlRotation();
	ControlRotation.Pitch = FMath::ClampAngle(ControlRotation.Pitch + Delta.Pitch, -89.f, 89.f);
	ControlRotation.Yaw += Delta.Yaw;
	OwnerController->SetControlRotation(ControlRotation);
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

	if (const ACharacter* OwnerCharacter = GetOwner<ACharacter>())
	{
		TargetSpread += FMath::GetMappedRangeValueClamped(
			FVector2D(0.f, GunBase::FullMovementSpreadSpeed),
			FVector2D(0.f, WeaponDataAsset->MovementSpread),
			OwnerCharacter->GetVelocity().Size2D());

		if (OwnerCharacter->GetCharacterMovement() && OwnerCharacter->GetCharacterMovement()->IsFalling())
		{
			TargetSpread += WeaponDataAsset->AirSpread;
		}
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
		FName MuzzleSocketName;
		USceneComponent* MuzzleComponent = GetMuzzleComponent(MuzzleSocketName);
		if (bFirstPersonView)
		{
			// Like the flash: spawned at the muzzle as seen on screen, and left to drift in the world
			const APawn* OwnerPawn = GetOwner<APawn>();
			const FRotator Facing = OwnerPawn ? OwnerPawn->GetControlRotation() : MuzzleComponent->GetSocketRotation(MuzzleSocketName);
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), WeaponDataAsset->MuzzleSmokeSystem,
				GetVisualMuzzleLocation(), Facing, FVector(GetFirstPersonEffectScale()));
		}
		else
		{
			UNiagaraFunctionLibrary::SpawnSystemAttached(
				WeaponDataAsset->MuzzleSmokeSystem, MuzzleComponent, MuzzleSocketName,
				FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true);
		}
	}
}
