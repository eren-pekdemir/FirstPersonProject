#include "Zombie/ZombiePickup.h"

#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/StaticMesh.h"
#include "Gun/GunBase.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/PlayerCharacter.h"
#include "UObject/ConstructorHelpers.h"

namespace ZombiePickup
{
	constexpr float TriggerRadius = 90.f;
	constexpr float SpinSpeed = 70.f;
	constexpr float BobHeight = 6.f;
	constexpr float BobSpeed = 2.2f;
	/** Height of the item above the ground */
	constexpr float HoverHeight = 24.f;
	/** Bigger than life so it reads in the grass and the dark */
	constexpr float VisualScale = 1.45f;
	/** How often a player standing on it while it was not needed is checked again */
	constexpr float RetryInterval = 0.3f;

	const FLinearColor AmmoGlow(1.f, 0.7f, 0.25f);
	const FLinearColor HealthGlow(1.f, 0.2f, 0.2f);
	const FLinearColor BatteryGlow(0.25f, 0.65f, 1.f);
}

AZombiePickup::AZombiePickup()
{
	PrimaryActorTick.bCanEverTick = true;

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(ZombiePickup::TriggerRadius);
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	SetRootComponent(Trigger);

	Visual = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Trigger);
	Visual->SetRelativeScale3D(FVector(ZombiePickup::VisualScale));

	auto MakePart = [this](const TCHAR* Name)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Visual);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		return Part;
	};
	BodyMesh = MakePart(TEXT("Body"));
	DetailMeshA = MakePart(TEXT("DetailA"));
	DetailMeshB = MakePart(TEXT("DetailB"));

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Visual);
	Glow->SetIntensityUnits(ELightUnits::Lumens);
	Glow->SetIntensity(170.f);
	Glow->SetAttenuationRadius(320.f);
	Glow->SetCastShadows(false);
	Glow->SetRelativeLocation(FVector(0.f, 0.f, 45.f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Magazine(TEXT("/Game/FirstPersonRifleAnimations/Meshes/Magazine/SM_Magazine.SM_Magazine"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
	ShapeMaterial = Material.Object;
	MagazineMesh = Magazine.Object;
}

void AZombiePickup::BeginPlay()
{
	Super::BeginPlay();

	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AZombiePickup::OnTriggerOverlap);
	ApplyLook();
	Visual->SetRelativeRotation(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f));
}

void AZombiePickup::SetPickupType(EZombiePickupType InType)
{
	Type = InType;
	if (HasActorBegunPlay())
	{
		ApplyLook();
	}
}

void AZombiePickup::SetPart(UStaticMeshComponent* Part, UStaticMesh* Mesh, const FVector& Location, const FRotator& Rotation, const FVector& Scale, const FLinearColor& Color)
{
	Part->SetStaticMesh(Mesh);
	Part->SetVisibility(Mesh != nullptr);
	Part->SetRelativeLocationAndRotation(Location, Rotation);
	Part->SetRelativeScale3D(Scale);

	// Basic shapes take a flat color; other meshes keep their own materials
	if (Mesh && (Mesh == CubeMesh || Mesh == CylinderMesh) && ShapeMaterial)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(ShapeMaterial, this);
		Material->SetVectorParameterValue(TEXT("Color"), Color);
		Part->SetMaterial(0, Material);
	}
}

void AZombiePickup::ApplyLook()
{
	const float H = ZombiePickup::HoverHeight;
	FLinearColor GlowColor = ZombiePickup::AmmoGlow;

	switch (Type)
	{
	case EZombiePickupType::Ammo:
		// Olive ammo can with a magazine leaning on top
		SetPart(BodyMesh, CubeMesh, FVector(0.f, 0.f, H), FRotator::ZeroRotator, FVector(0.34f, 0.18f, 0.2f), FLinearColor(0.08f, 0.1f, 0.035f));
		SetPart(DetailMeshA, CubeMesh, FVector(0.f, 0.f, H + 10.5f), FRotator::ZeroRotator, FVector(0.3f, 0.06f, 0.012f), FLinearColor(0.55f, 0.42f, 0.05f));
		SetPart(DetailMeshB, MagazineMesh, FVector(0.f, 4.f, H + 12.f), FRotator(0.f, 90.f, 75.f), FVector(1.2f), FLinearColor::White);
		GlowColor = ZombiePickup::AmmoGlow;
		break;

	case EZombiePickupType::Health:
		// White first aid kit with a red cross
		SetPart(BodyMesh, CubeMesh, FVector(0.f, 0.f, H), FRotator::ZeroRotator, FVector(0.3f, 0.22f, 0.12f), FLinearColor(0.75f, 0.75f, 0.72f));
		SetPart(DetailMeshA, CubeMesh, FVector(0.f, 0.f, H + 6.2f), FRotator::ZeroRotator, FVector(0.16f, 0.05f, 0.01f), FLinearColor(0.7f, 0.02f, 0.02f));
		SetPart(DetailMeshB, CubeMesh, FVector(0.f, 0.f, H + 6.2f), FRotator::ZeroRotator, FVector(0.05f, 0.16f, 0.01f), FLinearColor(0.7f, 0.02f, 0.02f));
		GlowColor = ZombiePickup::HealthGlow;
		break;

	case EZombiePickupType::Battery:
		// Big black cell with a copper top
		SetPart(BodyMesh, CylinderMesh, FVector(0.f, 0.f, H + 2.f), FRotator::ZeroRotator, FVector(0.11f, 0.11f, 0.26f), FLinearColor(0.02f, 0.02f, 0.025f));
		SetPart(DetailMeshA, CylinderMesh, FVector(0.f, 0.f, H + 18.f), FRotator::ZeroRotator, FVector(0.112f, 0.112f, 0.07f), FLinearColor(0.75f, 0.38f, 0.08f));
		SetPart(DetailMeshB, CylinderMesh, FVector(0.f, 0.f, H + 22.5f), FRotator::ZeroRotator, FVector(0.035f, 0.035f, 0.03f), FLinearColor(0.6f, 0.6f, 0.6f));
		GlowColor = ZombiePickup::BatteryGlow;
		break;
	}

	Glow->SetLightColor(GlowColor);
	BaseGlowIntensity = Glow->Intensity;
}

void AZombiePickup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	Age += DeltaTime;
	Visual->AddRelativeRotation(FRotator(0.f, ZombiePickup::SpinSpeed * DeltaTime, 0.f));
	FVector Location = Visual->GetRelativeLocation();
	Location.Z = FMath::Sin(Age * ZombiePickup::BobSpeed) * ZombiePickup::BobHeight;
	Visual->SetRelativeLocation(Location);

	// Soft pulse, and blinking when about to vanish
	float Pulse = 0.75f + 0.25f * FMath::Sin(Age * 3.f);
	const float LifeLeft = GetLifeSpan() > 0.f ? GetLifeSpan() : -1.f;
	if (LifeLeft >= 0.f && LifeLeft < BlinkTime)
	{
		const bool bOn = FMath::Fmod(Age, LifeLeft < BlinkTime * 0.4f ? 0.15f : 0.35f) > 0.08f;
		Visual->SetVisibility(bOn, true);
		Pulse *= bOn ? 1.f : 0.f;
	}
	Glow->SetIntensity(BaseGlowIntensity * Pulse);

	// Someone standing on it who did not need it before may need it now
	if (Age >= NextRetryTime)
	{
		NextRetryTime = Age + ZombiePickup::RetryInterval;
		TArray<AActor*> Overlapping;
		Trigger->GetOverlappingActors(Overlapping, APlayerCharacter::StaticClass());
		for (AActor* Actor : Overlapping)
		{
			OnTriggerOverlap(Trigger, Actor, nullptr, 0, false, FHitResult());
		}
	}
}

void AZombiePickup::OnTriggerOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	APlayerCharacter* Player = Cast<APlayerCharacter>(OtherActor);
	if (!Player || Player->IsDead() || IsActorBeingDestroyed() || !TryGive(*Player))
	{
		return;
	}

	USoundBase* Sound = nullptr;
	switch (Type)
	{
	case EZombiePickupType::Ammo:
		Sound = AmmoSound;
		Player->ShowPickupMessage(TEXT("+ AMMO"), ZombiePickup::AmmoGlow);
		break;
	case EZombiePickupType::Health:
		Sound = HealthSound;
		Player->ShowPickupMessage(FString::Printf(TEXT("+ %d HEALTH"), FMath::RoundToInt(HealthAmount)), ZombiePickup::HealthGlow);
		break;
	case EZombiePickupType::Battery:
		Sound = BatterySound;
		Player->ShowPickupMessage(TEXT("+ BATTERY"), ZombiePickup::BatteryGlow);
		break;
	}

	if (Sound)
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}
	UE_LOG(LogTemp, Display, TEXT("ZombiePickup taken type=%d"), static_cast<int32>(Type));
	Destroy();
}

bool AZombiePickup::TryGive(APlayerCharacter& Player) const
{
	switch (Type)
	{
	case EZombiePickupType::Ammo:
	{
		bool bGiven = false;
		for (AGunBase* Gun : Player.GetWeapons())
		{
			const UWeaponDataAsset* Data = Gun ? Gun->GetWeaponData() : nullptr;
			if (Data && Gun->GetReserveAmmo() < Data->MaxReserveAmmo)
			{
				Gun->AddReserveAmmo(FMath::Max(1, FMath::RoundToInt(Data->MaxReserveAmmo * AmmoFraction)));
				bGiven = true;
			}
		}
		return bGiven;
	}

	case EZombiePickupType::Health:
		return Player.AddHealth(HealthAmount);

	case EZombiePickupType::Battery:
		return Player.AddBattery(BatteryAmount);
	}
	return false;
}
