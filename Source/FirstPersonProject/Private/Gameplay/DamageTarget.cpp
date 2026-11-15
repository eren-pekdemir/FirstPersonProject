#include "Gameplay/DamageTarget.h"

#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace DamageTarget
{
	const FName ColorParameter(TEXT("Color"));
	const FName HeadTag(TEXT("Head"));

	constexpr float FlashFadeSpeed = 8.f;
	constexpr float FallSpeed = 6.f;
	constexpr float RiseSpeed = 3.f;
	constexpr float KnockedOverPitch = -85.f;
}

ADamageTarget::ADamageTarget()
{
	PrimaryActorTick.bCanEverTick = true;

	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	SetRootComponent(Pivot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(Pivot);
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, 75.f));
	BodyMesh->SetRelativeScale3D(FVector(0.55f, 0.55f, 1.5f));
	BodyMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeadMesh"));
	HeadMesh->SetupAttachment(Pivot);
	HeadMesh->SetRelativeLocation(FVector(0.f, 0.f, 172.f));
	HeadMesh->SetRelativeScale3D(FVector(0.38f));
	HeadMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	HeadMesh->ComponentTags.Add(DamageTarget::HeadTag);

	if (CylinderMesh.Succeeded())
	{
		BodyMesh->SetStaticMesh(CylinderMesh.Object);
	}

	if (SphereMesh.Succeeded())
	{
		HeadMesh->SetStaticMesh(SphereMesh.Object);
	}

	SetCanBeDamaged(true);
}

void ADamageTarget::BeginPlay()
{
	Super::BeginPlay();

	Health = MaxHealth;
	SpawnLocation = GetActorLocation();

	for (UStaticMeshComponent* Mesh : { BodyMesh.Get(), HeadMesh.Get() })
	{
		if (UMaterialInstanceDynamic* Material = Mesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			DynamicMaterials.Add(Material);
		}
	}

	SetColor(BaseColor);
}

void ADamageTarget::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateFlash(DeltaTime);
	UpdateFall(DeltaTime);
	UpdateStrafe(DeltaTime);
}

float ADamageTarget::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!IsAlive() || DamageAmount <= 0.f)
	{
		return 0.f;
	}

	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	Health = FMath::Max(Health - Applied, 0.f);
	FlashAlpha = 1.f;

	if (!IsAlive())
	{
		Die();
	}

	return Applied;
}

void ADamageTarget::Die()
{
	if (KillSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, KillSound, GetActorLocation());
	}

	GetWorldTimerManager().SetTimer(RespawnTimer, this, &ADamageTarget::Respawn, RespawnDelay, false);
}

void ADamageTarget::Respawn()
{
	Health = MaxHealth;
}

void ADamageTarget::UpdateFlash(float DeltaTime)
{
	if (FlashAlpha <= 0.f)
	{
		return;
	}

	FlashAlpha = FMath::Max(FlashAlpha - DeltaTime * DamageTarget::FlashFadeSpeed, 0.f);
	SetColor(FMath::Lerp(BaseColor, HitColor, FlashAlpha));
}

void ADamageTarget::UpdateFall(float DeltaTime)
{
	const float TargetAlpha = IsAlive() ? 0.f : 1.f;
	if (FMath::IsNearlyEqual(FallAlpha, TargetAlpha))
	{
		return;
	}

	const float Speed = IsAlive() ? DamageTarget::RiseSpeed : DamageTarget::FallSpeed;
	FallAlpha = FMath::FInterpConstantTo(FallAlpha, TargetAlpha, DeltaTime, Speed);

	// Ease out so the target slaps down and pops back up
	const float Eased = FMath::InterpEaseOut(0.f, 1.f, FallAlpha, 2.f);
	BodyMesh->SetRelativeRotation(FRotator(Eased * DamageTarget::KnockedOverPitch, 0.f, 0.f));
	HeadMesh->SetRelativeLocation(FRotator(Eased * DamageTarget::KnockedOverPitch, 0.f, 0.f).RotateVector(FVector(0.f, 0.f, 172.f)));
	BodyMesh->SetRelativeLocation(FRotator(Eased * DamageTarget::KnockedOverPitch, 0.f, 0.f).RotateVector(FVector(0.f, 0.f, 75.f)));
}

void ADamageTarget::UpdateStrafe(float DeltaTime)
{
	if (StrafeDistance <= 0.f || !IsAlive())
	{
		return;
	}

	StrafeTime += DeltaTime;

	// Triangle-ish wave with constant speed, like a target on a rail
	const float Period = 4.f * StrafeDistance / FMath::Max(StrafeSpeed, 1.f);
	const float Phase = FMath::Fmod(StrafeTime, Period) / Period;
	const float Offset = StrafeDistance * (Phase < 0.5f ? (4.f * Phase - 1.f) : (3.f - 4.f * Phase));

	SetActorLocation(SpawnLocation + GetActorRightVector() * Offset);
}

void ADamageTarget::SetColor(const FLinearColor& Color)
{
	for (UMaterialInstanceDynamic* Material : DynamicMaterials)
	{
		Material->SetVectorParameterValue(DamageTarget::ColorParameter, Color);
	}
}
