#include "Public/Gun/GunBase.h"
#include "NiagaraFunctionLibrary.h" 
#include "Components/StaticMeshComponent.h"
#include "Player/PlayerCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystemComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "DrawDebugHelpers.h"


AGunBase::AGunBase()
{
	PrimaryActorTick.bCanEverTick = true;
	
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("MeshComponent");
	MeshComponent->SetupAttachment(GetRootComponent());
	
	SlideMesh = CreateDefaultSubobject<UStaticMeshComponent>("SlideMesh");
	SlideMesh->SetupAttachment(MeshComponent);
}

void AGunBase::BeginPlay()
{
	Super::BeginPlay();

	CurrentSpread = WeaponDataAsset->BaseSpread;
	if (SlideMesh) SlideRestLocation = SlideMesh->GetRelativeLocation();

	// Mesh'lerin collision'ı Blueprint'te elde tutulan silah için NoCollision
	// olarak ayarlanmış olabilir; yerde duran (henüz alınmamış) silahlar için
	// bunu bIsHeld durumuna göre zorla doğru hale getir, yoksa interact trace
	// silahı hiç vuramaz.
	SetHeld(bIsHeld);
}

void AGunBase::SpawnMuzzleSmoke()
{
	if (!WeaponDataAsset || !WeaponDataAsset->MuzzleSmokeSystem) return;

	UNiagaraFunctionLibrary::SpawnSystemAttached(
		WeaponDataAsset->MuzzleSmokeSystem,
		MeshComponent,
		FName("MuzzleSocket"),
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget,
		true
	);
}

void AGunBase::UpdateSmoke(float DeltaTime)
{
	if (!bFiredSinceSmoke || !WeaponDataAsset) return;

	SmokeTimer += DeltaTime;
	if (SmokeTimer > WeaponDataAsset->SmokeDelay)
	{
		SpawnMuzzleSmoke();
		bFiredSinceSmoke = false;   // ateş edilene kadar bir daha çıkmaz
	}
}

void AGunBase::KickSlide()
{
	if (!SlideMesh) return;
	// anında geri it (-X = geri, mesh yönüne göre değişebilir)
	SlideMesh->SetRelativeLocation(SlideRestLocation - FVector(SlideKickBack, 0.f, 0.f));
}

void AGunBase::UpdateSlide(float DeltaTime)
{
	if (!SlideMesh) return;

	const FVector Current = SlideMesh->GetRelativeLocation();
	if (!Current.Equals(SlideRestLocation, 0.01f))
	{
		const FVector New = FMath::VInterpTo(Current, SlideRestLocation, DeltaTime, SlideReturnSpeed);
		SlideMesh->SetRelativeLocation(New);
	}
}

void AGunBase::UpdateSpread(float DeltaTime)
{
	float Target = WeaponDataAsset->BaseSpread;
	
	if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		const float Speed = OwnerPawn->GetVelocity().Size();
		Target += FMath::GetMappedRangeValueClamped(
			FVector2D(0.f, 600.f), FVector2D(0.f, 25.f), Speed);
	}

	CurrentSpread = FMath::FInterpTo(CurrentSpread, Target, DeltaTime, WeaponDataAsset->SpreadRecoverySpeed);
}

void AGunBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	UpdateRecoil(DeltaTime);
	UpdateSmoke(DeltaTime);
	UpdateSlide(DeltaTime); 
	UpdateSpread(DeltaTime);
}

void AGunBase::StartFire(APlayerController* PlayerController)
{
	if (!PlayerController) return;
	if (!WeaponDataAsset) return;
	
	CurrentOwnerController = PlayerController;
	
	TimeBetweenShots = 60.f / WeaponDataAsset->RoundsPerMinute;
	
	PerformFire();
	
	if (WeaponDataAsset->FireMode == EWeaponFireMode::Automatic)
	{
		GetWorldTimerManager().SetTimer(TimerHandle_AutoFire, this, &AGunBase::PerformFire, TimeBetweenShots, true);
	}

}

void AGunBase::StopFire()
{
	GetWorldTimerManager().ClearTimer(TimerHandle_AutoFire);
	CurrentOwnerController = nullptr;
}

void AGunBase::PerformFire()
{
	APlayerCharacter* Player = Cast<APlayerCharacter>(GetOwner());
	if (Player)
	{
		Player->PlayAnimMontage(WeaponDataAsset->CharacterFireMontage);
	}
	
	KickSlide(); 
	
	//SHOOT PARTICLE
	UParticleSystemComponent* MuzzleFX = UGameplayStatics::SpawnEmitterAttached(
	WeaponDataAsset->ShootParticle, MeshComponent, FName("MuzzleSocket"),
	FVector::ZeroVector, FRotator::ZeroRotator,
	EAttachLocation::SnapToTarget, true);

	if (MuzzleFX)
		MuzzleFX->SetRelativeScale3D(FVector(ShootParticleScale));
	
	//SHOOT SOUND
	if (WeaponDataAsset->FireSound)
	{
		UGameplayStatics::PlaySound2D(GetWorld(), WeaponDataAsset->FireSound);
	}
	
	// CAMERA SHAKE
	if (WeaponDataAsset->FireCameraShake)
	{
		CurrentOwnerController->ClientStartCameraShake(WeaponDataAsset->FireCameraShake);
	} 
	
	int32 SizeX, SizeY;
	CurrentOwnerController->GetViewportSize(SizeX, SizeY);
	FVector2D ScreenCenter(SizeX * 0.5f, SizeY * 0.5f);

	FVector CamLocation, CamDirection;
	CurrentOwnerController->DeprojectScreenPositionToWorld(
		ScreenCenter.X, ScreenCenter.Y, CamLocation, CamDirection);

	FVector StartPoint = CamLocation;                      
	FVector EndPoint   = CamLocation + CamDirection * TraceDistance;

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);          // silahın kendisi
	Params.AddIgnoredActor(GetOwner());    // karakter (oyuncu)

	GetWorld()->LineTraceSingleByChannel(Hit, StartPoint, EndPoint, ECC_Visibility, Params);
	
	// IMPACT EFFECT
	if (Hit.bBlockingHit && WeaponDataAsset->ImpactActorClass)
	{
		FActorSpawnParameters SpawnParameters;
		GetWorld()->SpawnActor<AActor>(
			WeaponDataAsset->ImpactActorClass,
			Hit.ImpactPoint,
			Hit.ImpactNormal.Rotation(),
			SpawnParameters);
	}
	
	const FVector Muzzle = MeshComponent->GetSocketLocation("MuzzleSocket");
	const FVector ImpactPoint = Hit.bBlockingHit ? Hit.ImpactPoint : EndPoint;
	

	
	if (WeaponDataAsset->TracerSystem)
	{
		UNiagaraComponent* Tracer = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), WeaponDataAsset->TracerSystem, Muzzle, FRotator::ZeroRotator, FVector(0.1));

		if (Tracer)
		{
			Tracer->SetVariableVec3(FName("MuzzlePosition"), Muzzle);

			TArray<FVector> Impacts;
			Impacts.Add(ImpactPoint);
			UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(
				Tracer, FName("ImpactPositions"), Impacts);

			Tracer->SetVariableBool(FName("Trigger"), true);
		}
	}
	ApplyRecoil(); 
	CurrentSpread = FMath::Min(CurrentSpread + WeaponDataAsset->SpreadPerShot, WeaponDataAsset->	MaxSpread);
	
	bFiredSinceSmoke = true;  
	SmokeTimer = 0.f;  
}


FVector AGunBase::TraceEndPoint(APlayerController* PlayerController)
{
	int32 SizeX;
	int32 SizeY;
	PlayerController->GetViewportSize(SizeX, SizeY);
	
	FVector2D ScreenCenter(SizeX / 2.0f, SizeY / 2.0f);
	
	FVector WorldLocation;
	FVector WorldDirection;
	
	bool bSuccess = PlayerController->DeprojectScreenPositionToWorld(ScreenCenter.X, ScreenCenter.Y, WorldLocation, WorldDirection);
	if (bSuccess)
	{
		return WorldLocation + (WorldDirection * TraceDistance);
	}
	return FVector::ZeroVector;
}

void AGunBase::SetHeld(bool bNewHeld)
{
	bIsHeld = bNewHeld;

	const ECollisionEnabled::Type CollisionType = bIsHeld ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics;

	if (MeshComponent)
	{
		MeshComponent->SetCollisionEnabled(CollisionType);
	}

	if (SlideMesh)
	{
		SlideMesh->SetCollisionEnabled(CollisionType);
	}
}

void AGunBase::ApplyRecoil()
{
	if (!WeaponDataAsset || WeaponDataAsset->RecoilPattern.Num() == 0) return;

	const int32 Idx = FMath::Min(RecoilIndex, WeaponDataAsset->RecoilPattern.Num() - 1);
	FVector2D Step = WeaponDataAsset->RecoilPattern[Idx];
	Step.X += FMath::RandRange(-0.1f, 0.1f);

	RecoilTarget.Pitch += Step.Y;   // yukarı
	RecoilTarget.Yaw   += Step.X;   // yana

	RecoilIndex++;
	TimeSinceLastShot = 0.f;
	bRecovering = false;
}

void AGunBase::UpdateRecoil(float DeltaTime)
{
	if (!WeaponDataAsset) return;

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled()) return;

	TimeSinceLastShot += DeltaTime;

	// ateş bitti → pattern'i sıfırla, AMA view'a DOKUNMA (recovery yok)
	if (TimeSinceLastShot > WeaponDataAsset->RecoilResetDelay)
	{
		RecoilTarget  = FRotator::ZeroRotator;
		RecoilCurrent = FRotator::ZeroRotator;
		RecoilIndex   = 0;
		return;
	}

	// ateş sırasında → sadece kick'i uygula
	const FRotator NewCurrent = FMath::RInterpTo(RecoilCurrent, RecoilTarget,
												 DeltaTime, WeaponDataAsset->RecoilApplySpeed);
	const FRotator Delta = NewCurrent - RecoilCurrent;
	OwnerPawn->AddControllerPitchInput(-Delta.Pitch);
	OwnerPawn->AddControllerYawInput(Delta.Yaw);
	RecoilCurrent = NewCurrent;
}
