#include "Zombie/ZombieCharacter.h"

#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "FirstPersonProject.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraFunctionLibrary.h"
#include "Components/DecalComponent.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Zombie/ZombieAIController.h"
#include "Zombie/ZombieAnimInstance.h"

namespace Zombie
{
	const FName HeadBone(TEXT("head"));
	const FName NeckBone(TEXT("neck_01"));

	/** How fast recent damage is forgotten, per second */
	constexpr float RecentDamageDecay = 60.f;
	constexpr float HitReactionMaxWeight = 0.55f;
	constexpr float HitReactionFadeSpeed = 2.6f;
	constexpr float CapsuleHalfHeight = 90.f;
	constexpr float CapsuleRadius = 34.f;
	/** Front cone (cosine) in which the blow can land */
	constexpr float AttackConeCos = 0.2f;
	constexpr float JumpscareCooldown = 9.f;
	/** Frenzied zombies move this much faster */
	constexpr float FrenzySpeedScale = 1.45f;
}

AZombieCharacter::AZombieCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	AIControllerClass = AZombieAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->InitCapsuleSize(Zombie::CapsuleRadius, Zombie::CapsuleHalfHeight);
	// Bullets go through the capsule and hit the body parts of the mesh
	Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	USkeletalMeshComponent* Body = GetMesh();
	Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -Zombie::CapsuleHalfHeight), FRotator(0.f, -90.f, 0.f));
	Body->SetCollisionObjectType(ECC_PhysicsBody);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Body->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Body->SetGenerateOverlapEvents(false);
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Body->SetAnimInstanceClass(UZombieAnimInstance::StaticClass());

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->bUseControllerDesiredRotation = false;
	Movement->RotationRate = FRotator(0.f, 300.f, 0.f);
	Movement->MaxWalkSpeed = 110.f;
	Movement->MaxAcceleration = 900.f;
	Movement->BrakingDecelerationWalking = 900.f;
	Movement->bRequestedMoveUseAcceleration = true;
	bUseControllerRotationYaw = false;

	FZombieTypeSettings Walker;
	Walker.WanderSpeed = 45.f;
	Walker.ChaseSpeed = 105.f;
	Walker.LungeSpeed = 150.f;
	TypeSettings.Add(EZombieType::Walker, Walker);

	FZombieTypeSettings Runner;
	Runner.Health = 90.f;
	Runner.WanderSpeed = 60.f;
	Runner.ChaseSpeed = 360.f;
	Runner.LungeSpeed = 440.f;
	Runner.AttackDamage = 14.f;
	Runner.AttackCooldown = 1.1f;
	Runner.Scale = 0.97f;
	Runner.StaggerDamage = 35.f;
	Runner.AttackRate = 1.25f;
	TypeSettings.Add(EZombieType::Runner, Runner);

	FZombieTypeSettings Brute;
	Brute.Health = 320.f;
	Brute.WanderSpeed = 40.f;
	Brute.ChaseSpeed = 95.f;
	Brute.LungeSpeed = 130.f;
	Brute.AttackDamage = 38.f;
	Brute.AttackCooldown = 1.9f;
	Brute.Scale = 1.18f;
	Brute.StaggerDamage = 140.f;
	Brute.AttackRate = 0.85f;
	TypeSettings.Add(EZombieType::Brute, Brute);

	FZombieTypeSettings Crawler;
	Crawler.Health = 70.f;
	Crawler.WanderSpeed = 30.f;
	Crawler.ChaseSpeed = 70.f;
	Crawler.LungeSpeed = 95.f;
	Crawler.AttackDamage = 12.f;
	Crawler.AttackCooldown = 1.f;
	Crawler.StaggerDamage = 60.f;
	Crawler.AttackRate = 2.2f;
	Crawler.CapsuleHalfHeight = 40.f;
	Crawler.EyeGlow = FLinearColor(0.9f, 0.35f, 0.05f, 1.f);
	Crawler.VoicePitch = 1.25f;
	Crawler.VoiceVolume = 0.6f;
	TypeSettings.Add(EZombieType::Crawler, Crawler);

	FZombieTypeSettings Screamer;
	Screamer.Health = 75.f;
	Screamer.WanderSpeed = 60.f;
	Screamer.ChaseSpeed = 300.f;
	Screamer.LungeSpeed = 360.f;
	Screamer.AttackDamage = 10.f;
	Screamer.Scale = 0.95f;
	Screamer.StaggerDamage = 30.f;
	Screamer.EyeGlow = FLinearColor(0.85f, 0.9f, 0.2f, 1.f);
	Screamer.VoicePitch = 1.4f;
	Screamer.VoiceVolume = 1.3f;
	TypeSettings.Add(EZombieType::Screamer, Screamer);

	FZombieTypeSettings Stalker;
	Stalker.Health = 160.f;
	Stalker.WanderSpeed = 80.f;
	Stalker.ChaseSpeed = 330.f;
	Stalker.LungeSpeed = 420.f;
	Stalker.AttackDamage = 32.f;
	Stalker.AttackCooldown = 2.2f;
	Stalker.StaggerDamage = 90.f;
	Stalker.AttackRate = 1.3f;
	Stalker.MeshScale = FVector(0.9f, 0.9f, 1.14f);
	Stalker.EyeGlow = FLinearColor(1.f, 0.02f, 0.02f, 1.f);
	Stalker.VoicePitch = 0.7f;
	Stalker.bSilent = true;
	TypeSettings.Add(EZombieType::Stalker, Stalker);

	// Brutes and runners get their own eyes and voices too
	TypeSettings[EZombieType::Brute].EyeGlow = FLinearColor(0.6f, 0.05f, 0.f, 1.f);
	TypeSettings[EZombieType::Brute].VoicePitch = 0.75f;
	TypeSettings[EZombieType::Brute].VoiceVolume = 1.3f;
	TypeSettings[EZombieType::Runner].VoicePitch = 1.15f;
}

void AZombieCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (Health <= 0.f || MaxHealth <= 0.f || !GetZombieAnim())
	{
		SetupType(Type);
	}
}

const FZombieTypeSettings& AZombieCharacter::GetSettings() const
{
	static const FZombieTypeSettings Default;
	const FZombieTypeSettings* Settings = TypeSettings.Find(Type);
	return Settings ? *Settings : Default;
}

UZombieAnimInstance* AZombieCharacter::GetZombieAnim() const
{
	return Cast<UZombieAnimInstance>(GetMesh()->GetAnimInstance());
}

void AZombieCharacter::SetupType(EZombieType InType, float HealthScale)
{
	Type = InType;
	const FZombieTypeSettings& Settings = GetSettings();

	MaxHealth = Health = Settings.Health * FMath::Max(HealthScale, 0.1f);

	// Slightly different sizes so a horde does not look cloned
	const float Scale = Settings.Scale * FMath::FRandRange(0.95f, 1.05f);
	SetActorScale3D(FVector(Scale));

	const float HalfHeight = Settings.CapsuleHalfHeight > 0.f ? Settings.CapsuleHalfHeight : Zombie::CapsuleHalfHeight;
	GetCapsuleComponent()->SetCapsuleHalfHeight(HalfHeight);
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight));
	GetMesh()->SetRelativeScale3D(Settings.MeshScale);

	SetMoveSpeed(Settings.WanderSpeed);

	if (UZombieAnimInstance* Anim = GetZombieAnim())
	{
		UAnimSequence* Idle = Settings.IdleAnim ? Settings.IdleAnim.Get() : IdleAnim.Get();
		UAnimSequence* MoveAnim = WalkAnim;
		float MoveAnimSpeed = WalkAnimSpeed;
		if (Settings.MoveAnim)
		{
			MoveAnim = Settings.MoveAnim;
			MoveAnimSpeed = Settings.MoveAnimSpeed > 0.f ? Settings.MoveAnimSpeed : WalkAnimSpeed;
		}
		else if (RunAnim && (Type == EZombieType::Runner || Type == EZombieType::Screamer || Type == EZombieType::Stalker))
		{
			MoveAnim = RunAnim;
			MoveAnimSpeed = RunAnimSpeed;
		}
		Anim->SetLocomotion(Idle, MoveAnim, MoveAnimSpeed);
	}

	ApplyLook();
}

void AZombieCharacter::ApplyLook()
{
	USkeletalMeshComponent* Body = GetMesh();
	const FZombieTypeSettings& Settings = GetSettings();

	if (!HairVariants.IsEmpty())
	{
		const int32 HairIndex = Body->GetMaterialIndex(HairSlot);
		if (HairIndex != INDEX_NONE)
		{
			Body->SetMaterial(HairIndex, HairVariants[FMath::RandRange(0, HairVariants.Num() - 1)]);
		}
	}

	if (TintOverlayMaterial && Settings.SkinTint.A > 0.f)
	{
		UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(TintOverlayMaterial, this);
		Tint->SetVectorParameterValue(TEXT("Tint"), Settings.SkinTint);
		Body->SetOverlayMaterial(Tint);
	}
	else
	{
		Body->SetOverlayMaterial(nullptr);
	}

	if (EyeGlowMaterial && Settings.EyeGlow.A > 0.f)
	{
		UMaterialInstanceDynamic* Eyes = UMaterialInstanceDynamic::Create(EyeGlowMaterial, this);
		Eyes->SetVectorParameterValue(TEXT("GlowColor"), Settings.EyeGlow);
		Eyes->SetScalarParameterValue(TEXT("GlowStrength"), 90.f);
		for (const FName& Slot : EyeSlots)
		{
			const int32 SlotIndex = Body->GetMaterialIndex(Slot);
			if (SlotIndex != INDEX_NONE)
			{
				Body->SetMaterial(SlotIndex, Eyes);
			}
		}
	}
}

const TArray<FZombieAttack>& AZombieCharacter::GetAttacks() const
{
	const FZombieTypeSettings& Settings = GetSettings();
	return Settings.Attacks.IsEmpty() ? Attacks : Settings.Attacks;
}

void AZombieCharacter::SetDormant()
{
	bDormant = true;
	if (UZombieAnimInstance* Anim = GetZombieAnim())
	{
		Anim->SetLocomotion(DormantAnim ? DormantAnim.Get() : IdleAnim.Get(), nullptr, WalkAnimSpeed);
	}
	SetActorRotation(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f));
}

float AZombieCharacter::WakeUp()
{
	if (!bDormant)
	{
		return 0.f;
	}

	bDormant = false;
	const float HealthLeft = Health;
	SetupType(Type, MaxHealth / FMath::Max(GetSettings().Health, 1.f));
	Health = HealthLeft;

	float Duration = 1.2f;
	UZombieAnimInstance* Anim = GetZombieAnim();
	if (Anim && WakeAnim)
	{
		// Slow blend in from the lying pose: it rises from the ground
		Duration = FMath::Min(Anim->PlayAction(WakeAnim, 1.2f, 0.7f, 0.35f), 2.2f);
	}

	BusyUntil = GetWorld()->GetTimeSeconds() + Duration;
	PlaySound(ScreamSounds);
	return Duration;
}

void AZombieCharacter::SetFrenzied(float Duration)
{
	FrenzyUntil = FMath::Max(FrenzyUntil, GetWorld()->GetTimeSeconds() + Duration);
}

bool AZombieCharacter::IsFrenzied() const
{
	return GetWorld()->GetTimeSeconds() < FrenzyUntil;
}

void AZombieCharacter::HitTarget(AActor* Target, float Damage, float Knockback)
{
	if (!Target || !IsAlive())
	{
		return;
	}

	UGameplayStatics::ApplyDamage(Target, Damage, GetController(), this, UDamageType::StaticClass());
	if (HitPlayerSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, HitPlayerSound, Target->GetActorLocation());
	}

	if (ACharacter* Victim = Cast<ACharacter>(Target))
	{
		const FVector Push = (Victim->GetActorLocation() - GetActorLocation()).GetSafeNormal2D() * Knockback + FVector(0.f, 0.f, Knockback * 0.35f);
		Victim->LaunchCharacter(Push, true, true);
	}
	PlayJumpscareIfUnseen(Target);
}

void AZombieCharacter::PlayJumpscareIfUnseen(const AActor* Victim) const
{
	static float LastJumpscareTime = -100.f;
	const APawn* VictimPawn = Cast<APawn>(Victim);
	const APlayerController* PlayerController = VictimPawn ? VictimPawn->GetController<APlayerController>() : nullptr;
	const float Now = GetWorld()->GetTimeSeconds();

	// The time restarts with a new level
	if (Now < LastJumpscareTime)
	{
		LastJumpscareTime = -100.f;
	}

	if (!JumpscareSound || !PlayerController || Now - LastJumpscareTime < Zombie::JumpscareCooldown)
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const float Facing = FVector::DotProduct(ViewRotation.Vector(), (GetActorLocation() - ViewLocation).GetSafeNormal());

	// Caught from the side or from behind
	if (Facing < 0.35f)
	{
		LastJumpscareTime = Now;
		UGameplayStatics::PlaySound2D(this, JumpscareSound, 0.9f);
	}
}

void AZombieCharacter::SetMoveSpeed(float Speed)
{
	if (IsFrenzied())
	{
		Speed *= Zombie::FrenzySpeedScale;
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = Speed;
	Movement->MaxAcceleration = FMath::Max(600.f, Speed * 4.f);
}

void AZombieCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	RecentDamage = FMath::Max(0.f, RecentDamage - Zombie::RecentDamageDecay * DeltaTime);
	UpdateHitReaction(DeltaTime);
	UpdateFootsteps(DeltaTime);

	// Keep facing the victim while swinging
	if (bAttacking && AttackTarget.IsValid())
	{
		const FVector ToTarget = (AttackTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		if (!ToTarget.IsNearlyZero())
		{
			const FRotator Desired = ToTarget.Rotation();
			SetActorRotation(FMath::RInterpTo(GetActorRotation(), Desired, DeltaTime, 8.f));
			AddMovementInput(ToTarget, 1.f);
		}
	}
}

bool AZombieCharacter::IsBusy() const
{
	return bAttacking || GetWorld()->GetTimeSeconds() < BusyUntil || IsStaggered();
}

bool AZombieCharacter::IsStaggered() const
{
	return GetWorld()->GetTimeSeconds() < StaggerUntil;
}

bool AZombieCharacter::CanAttack() const
{
	return IsAlive() && !IsBusy() && GetWorld()->GetTimeSeconds() - LastAttackTime >= GetSettings().AttackCooldown;
}

float AZombieCharacter::GetTimeSinceDamaged() const
{
	return GetWorld()->GetTimeSeconds() - LastDamageTime;
}

bool AZombieCharacter::StartAttack(AActor* Target)
{
	const TArray<FZombieAttack>& TypeAttacks = GetAttacks();
	if (!Target || !CanAttack() || TypeAttacks.IsEmpty())
	{
		return false;
	}

	const FZombieTypeSettings& Settings = GetSettings();
	const FZombieAttack& Attack = TypeAttacks[FMath::RandRange(0, TypeAttacks.Num() - 1)];
	const float Rate = FMath::Max(Settings.AttackRate, 0.1f);

	UZombieAnimInstance* ZombieAnim = GetZombieAnim();
	const float Duration = ZombieAnim ? ZombieAnim->PlayAction(Attack.Anim, Rate, 0.12f, 0.25f, Attack.StartTime, Attack.EndTime) : 1.f;
	const float HitDelay = FMath::Max((Attack.HitTime - Attack.StartTime) / Rate, 0.05f);

	bAttacking = true;
	AttackTarget = Target;
	LastAttackTime = GetWorld()->GetTimeSeconds();
	SetMoveSpeed(AttackStepSpeed);

	PlaySound(AttackSounds);

	FTimerManager& Timers = GetWorldTimerManager();
	Timers.SetTimer(AttackHitTimer, this, &AZombieCharacter::LandAttack, HitDelay, false);
	Timers.SetTimer(AttackEndTimer, this, &AZombieCharacter::EndAttack, FMath::Max(Duration * 0.85f, HitDelay + 0.1f), false);
	return true;
}

void AZombieCharacter::LandAttack()
{
	AActor* Target = AttackTarget.Get();
	if (!Target || !IsAlive())
	{
		return;
	}

	// The blow lands only if the victim is still in reach and in front: stepping back dodges it
	const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	const float Reach = AttackReach * GetActorScale3D().X;
	const bool bInReach = ToTarget.Size2D() <= Reach && FMath::Abs(ToTarget.Z) < 150.f;
	const bool bInFront = FVector::DotProduct(ToTarget.GetSafeNormal2D(), GetActorForwardVector()) >= Zombie::AttackConeCos;

	if (bInReach && bInFront)
	{
		UGameplayStatics::ApplyDamage(Target, GetSettings().AttackDamage, GetController(), this, UDamageType::StaticClass());
		if (HitPlayerSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, HitPlayerSound, Target->GetActorLocation());
		}
		PlayJumpscareIfUnseen(Target);
	}
}

void AZombieCharacter::EndAttack()
{
	bAttacking = false;
	AttackTarget.Reset();
}

float AZombieCharacter::Scream()
{
	UAnimSequence* Anim = ScreamAnims.IsEmpty() ? nullptr : ScreamAnims[FMath::RandRange(0, ScreamAnims.Num() - 1)].Get();
	UZombieAnimInstance* ZombieAnim = GetZombieAnim();
	const float Duration = (ZombieAnim && Anim) ? FMath::Min(ZombieAnim->PlayAction(Anim, 1.1f, 0.2f, 0.3f), 2.2f) : 0.8f;

	BusyUntil = GetWorld()->GetTimeSeconds() + Duration;
	PlaySound(ScreamSounds);
	return Duration;
}

void AZombieCharacter::Groan()
{
	if (!IsAlive() || bDormant)
	{
		return;
	}

	if (GetSettings().bSilent)
	{
		PlaySound(WhisperSounds, 0.5f);
		return;
	}
	PlaySound(GroanSounds, FMath::FRandRange(0.6f, 1.f));
}

float AZombieCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (DamageAmount <= 0.f)
	{
		return 0.f;
	}

	// Corpses still bleed when shot
	if (!IsAlive())
	{
		if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
		{
			const FPointDamageEvent& PointEvent = static_cast<const FPointDamageEvent&>(DamageEvent);
			SpawnBloodHit(PointEvent.HitInfo.ImpactPoint, PointEvent.ShotDirection.GetSafeNormal(), DamageAmount * 0.5f);
			BeginFrameDamage();
			TrackLimbDamage(PointEvent.HitInfo.BoneName, DamageAmount, PointEvent.ShotDirection.GetSafeNormal());
		}
		return 0.f;
	}

	const float Damage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (Damage <= 0.f)
	{
		return 0.f;
	}

	FName HitBone = NAME_None;
	FVector Direction = DamageCauser ? (GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal() : -GetActorForwardVector();
	FVector HitLocation = GetActorLocation();
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& PointEvent = static_cast<const FPointDamageEvent&>(DamageEvent);
		HitBone = PointEvent.HitInfo.BoneName;
		Direction = PointEvent.ShotDirection.GetSafeNormal();
		HitLocation = PointEvent.HitInfo.ImpactPoint;
	}

	const bool bHeadshot = HitBone == Zombie::HeadBone || HitBone == Zombie::NeckBone;

	BeginFrameDamage();
	FrameDamage += Damage;
	bKilledByPlayer = EventInstigator && EventInstigator->IsPlayerController();

	SpawnBloodHit(HitLocation, Direction, Damage, bHeadshot);

	Health -= Damage;
	LastDamageTime = GetWorld()->GetTimeSeconds();
	RecentDamage += Damage;

	if (Health <= 0.f)
	{
		Die(Direction, HitBone, bHeadshot);
		TrackLimbDamage(HitBone, Damage, Direction);
		return Damage;
	}

	TrackLimbDamage(HitBone, Damage, Direction);
	StartHitReaction(HitBone, Direction, Damage);

	// Enough damage at once knocks it off balance, interrupting an attack
	if (RecentDamage >= GetSettings().StaggerDamage)
	{
		RecentDamage = 0.f;
		StaggerUntil = GetWorld()->GetTimeSeconds() + StaggerTime;

		GetWorldTimerManager().ClearTimer(AttackHitTimer);
		EndAttack();

		if (UZombieAnimInstance* Anim = GetZombieAnim())
		{
			if (!HitAnims.IsEmpty())
			{
				Anim->PlayAction(HitAnims[FMath::RandRange(0, HitAnims.Num() - 1)], 1.2f, 0.08f, 0.25f);
			}
			else
			{
				Anim->StopAction(0.1f);
			}
		}

		GetCharacterMovement()->StopMovementImmediately();
		PlaySound(PainSounds);
	}
	else if (FMath::FRand() < 0.3f)
	{
		PlaySound(PainSounds, 0.8f);
	}

	if (AZombieAIController* AI = Cast<AZombieAIController>(GetController()))
	{
		AI->OnDamaged(EventInstigator ? EventInstigator->GetPawn() : nullptr);
	}

	return Damage;
}

void AZombieCharacter::StartHitReaction(FName Bone, const FVector& Direction, float Damage)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body->GetPhysicsAsset() || Bone == NAME_None)
	{
		return;
	}

	// The upper body flinches physically from the impact, then the animation takes back over
	Body->SetAllBodiesBelowSimulatePhysics(HitReactionRootBone, true, true);
	bHitReactionActive = true;
	HitReactionWeight = FMath::Clamp(HitReactionWeight + Damage / 60.f, 0.2f, Zombie::HitReactionMaxWeight);
	Body->SetAllBodiesBelowPhysicsBlendWeight(HitReactionRootBone, HitReactionWeight, false, true);
	Body->AddImpulse(Direction * HitReactionImpulse * FMath::Clamp(Damage / 30.f, 0.4f, 2.f), Bone, true);
}

void AZombieCharacter::UpdateHitReaction(float DeltaTime)
{
	if (!bHitReactionActive || !IsAlive())
	{
		return;
	}

	USkeletalMeshComponent* Body = GetMesh();
	HitReactionWeight -= Zombie::HitReactionFadeSpeed * DeltaTime;
	if (HitReactionWeight <= 0.f)
	{
		HitReactionWeight = 0.f;
		bHitReactionActive = false;
		Body->SetAllBodiesBelowSimulatePhysics(HitReactionRootBone, false, true);
		Body->SetAllBodiesBelowPhysicsBlendWeight(HitReactionRootBone, 0.f, false, true);
		return;
	}

	Body->SetAllBodiesBelowPhysicsBlendWeight(HitReactionRootBone, HitReactionWeight, false, true);
}

void AZombieCharacter::Die(const FVector& ImpulseDirection, FName HitBone, bool bHeadshot)
{
	Health = 0.f;
	bAttacking = false;
	GetWorldTimerManager().ClearTimer(AttackHitTimer);
	GetWorldTimerManager().ClearTimer(AttackEndTimer);

	PlaySound(DeathSounds);

	if (AZombieAIController* AI = Cast<AZombieAIController>(GetController()))
	{
		AI->OnPawnDied();
	}

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Ragdoll, thrown by the killing shot
	USkeletalMeshComponent* Body = GetMesh();
	Body->SetCollisionProfileName(TEXT("Ragdoll"));
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Body->SetAllBodiesSimulatePhysics(true);
	Body->SetAllBodiesPhysicsBlendWeight(1.f);
	Body->WakeAllRigidBodies();
	Body->bPauseAnims = true;

	const FName ImpulseBone = HitBone != NAME_None ? HitBone : FName(TEXT("spine_03"));

	// A blast at close range throws the body back off its feet
	const float Heaviness = FMath::Clamp(FrameDamage / HeavyKillDamage, 1.f, 2.5f);
	const FVector Throw = (ImpulseDirection + FVector(0.f, 0.f, Heaviness > 1.2f ? 0.35f : 0.f)).GetSafeNormal();
	Body->AddImpulse(Throw * DeathImpulse * Heaviness * (bHeadshot ? 1.2f : 1.f), ImpulseBone, true);
	if (Heaviness > 1.2f)
	{
		Body->AddImpulse(Throw * DeathImpulse * 0.6f * Heaviness, TEXT("pelvis"), true);
	}

	if (bHeadshot && FrameDamage >= HeadPopDamage)
	{
		PopHead(ImpulseDirection);
	}

	// A brutal kill freezes the moment for a split second
	if (Heaviness > 1.2f || bHeadPopped)
	{
		StartKillHitStop();
	}

	UE_LOG(LogTemp, Display, TEXT("ZombieDeath type=%d killing burst=%.0f maxHealth=%.0f earlier damage=%.0f headshot=%d headPopped=%d"),
		static_cast<int32>(Type), FrameDamage, MaxHealth, FMath::Max(0.f, MaxHealth - (Health + FrameDamage)), bHeadshot ? 1 : 0, bHeadPopped ? 1 : 0);

	SetLifeSpan(CorpseLifeSpan);
	OnDied.Broadcast(this, bHeadshot);
}

void AZombieCharacter::PlaySound(const TArray<TObjectPtr<USoundBase>>& Sounds, float VolumeMultiplier) const
{
	if (Sounds.IsEmpty())
	{
		return;
	}

	if (USoundBase* Sound = Sounds[FMath::RandRange(0, Sounds.Num() - 1)])
	{
		const FZombieTypeSettings& Settings = GetSettings();
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation() + FVector(0.f, 0.f, 60.f),
			VolumeMultiplier * Settings.VoiceVolume, Settings.VoicePitch * FMath::FRandRange(0.9f, 1.1f), 0.f, SoundAttenuation);
	}
}

// -------------------------------------------------------------------------------------------------
// Gore

void AZombieCharacter::SpawnBloodHit(const FVector& Location, const FVector& ShotDirection, float Damage, bool bHead)
{
	const float Now = GetWorld()->GetTimeSeconds();

	if (BloodHitEffect)
	{
		// Sprays out of the exit side, along the shot
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BloodHitEffect, Location, ShotDirection.Rotation(),
			FVector(BloodEffectScale * FMath::Clamp(Damage / 25.f, 0.6f, 1.6f)), true, true);
	}

	// One wet thud per burst, not one per pellet
	const TArray<TObjectPtr<USoundBase>>& HitSounds = (bHead && !HeadHitSounds.IsEmpty()) ? HeadHitSounds : FleshHitSounds;
	if (!HitSounds.IsEmpty() && Now - LastFleshSoundTime > 0.06f)
	{
		LastFleshSoundTime = Now;
		UGameplayStatics::PlaySoundAtLocation(this, HitSounds[FMath::RandRange(0, HitSounds.Num() - 1)], Location,
			FMath::Clamp(0.6f + Damage / 60.f, 0.6f, 1.2f), FMath::FRandRange(0.9f, 1.1f));
	}

	if (FMath::FRand() < 0.5f)
	{
		SpawnBloodDecal(Location, ShotDirection, FMath::Clamp(Damage * 1.6f, 25.f, 80.f));
	}
}

void AZombieCharacter::SpawnBloodDecal(const FVector& Location, const FVector& Direction, float Size) const
{
	if (BloodDecals.IsEmpty())
	{
		return;
	}

	// Blood flies on along the shot and lands on whatever is behind or below
	const FVector Along = (Direction + FVector(0.f, 0.f, -0.6f)).GetSafeNormal();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ZombieBlood), false, this);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Location, Location + Along * 450.f, ECC_WorldStatic, Params))
	{
		return;
	}

	UMaterialInterface* Material = BloodDecals[FMath::RandRange(0, BloodDecals.Num() - 1)];
	const FRotator Rotation = (-Hit.ImpactNormal).Rotation() + FRotator(0.f, 0.f, FMath::FRandRange(0.f, 360.f));
	if (UDecalComponent* Decal = UGameplayStatics::SpawnDecalAtLocation(GetWorld(), Material, FVector(12.f, Size, Size), Hit.ImpactPoint, Rotation, BloodDecalLifeSpan))
	{
		Decal->SetFadeScreenSize(0.002f);
		Decal->SetFadeOut(BloodDecalLifeSpan - 4.f, 4.f, false);
	}
}

void AZombieCharacter::PopHead(const FVector& ShotDirection)
{
	if (bHeadPopped)
	{
		return;
	}
	bHeadPopped = true;

	USkeletalMeshComponent* Body = GetMesh();
	const FVector HeadLocation = Body->GetSocketLocation(Zombie::HeadBone);
	Body->HideBoneByName(Zombie::HeadBone, PBO_Term);

	if (BloodBurstEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BloodBurstEffect, HeadLocation, ShotDirection.Rotation(), FVector(BloodEffectScale * 1.5f), true, true);
		UNiagaraFunctionLibrary::SpawnSystemAttached(BloodBurstEffect, Body, Zombie::NeckBone, FVector::ZeroVector, FRotator(90.f, 0.f, 0.f),
			FVector(BloodEffectScale * 0.7f), EAttachLocation::SnapToTarget, true, ENCPoolMethod::None);
	}

	if (HeadPopSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, HeadPopSound, HeadLocation, 1.f, FMath::FRandRange(0.9f, 1.05f));
	}

	for (int32 Index = 0; Index < 3; ++Index)
	{
		SpawnBloodDecal(HeadLocation, (ShotDirection + FMath::VRand() * 0.4f).GetSafeNormal(), FMath::FRandRange(50.f, 90.f));
	}
}

void AZombieCharacter::BeginFrameDamage()
{
	// Pellets of one blast land in the same frame: add them up as one hit
	if (FrameDamageFrame != GFrameCounter)
	{
		FrameDamageFrame = GFrameCounter;
		FrameDamage = 0.f;
		FrameLimbDamage.Reset();
	}
}

FName AZombieCharacter::FindLimb(FName Bone) const
{
	const USkeletalMeshComponent* Body = GetMesh();
	for (int32 Depth = 0; Depth < 4 && Bone != NAME_None; ++Depth)
	{
		if (SeverableLimbs.Contains(Bone))
		{
			return Bone;
		}
		Bone = Body->GetParentBone(Bone);
	}
	return NAME_None;
}

void AZombieCharacter::TrackLimbDamage(FName HitBone, float Damage, const FVector& ShotDirection)
{
	const FName Limb = FindLimb(HitBone);
	if (Limb == NAME_None || SeveredLimbs.Contains(Limb) || SeveredLimbs.Num() >= MaxSeveredLimbs)
	{
		return;
	}

	float& LimbDamage = FrameLimbDamage.FindOrAdd(Limb);
	LimbDamage += Damage;

	// Arms can be shot off a living zombie, it keeps coming; legs only come off the dead
	// Dead flesh tears more easily
	const bool bArm = Limb.ToString().Contains(TEXT("arm"));
	const float Threshold = IsAlive() ? LimbSeverDamage : LimbSeverDamage * 0.7f;
	if (LimbDamage >= Threshold && (bArm || !IsAlive()))
	{
		SeverLimb(Limb, ShotDirection);
	}
}

void AZombieCharacter::SeverLimb(FName Limb, const FVector& ShotDirection)
{
	USkeletalMeshComponent* Body = GetMesh();
	const FName Parent = Body->GetParentBone(Limb);
	if (Parent == NAME_None)
	{
		return;
	}

	SeveredLimbs.Add(Limb);
	const FVector Joint = Body->GetSocketLocation(Limb);
	Body->HideBoneByName(Limb, PBO_Term);

	if (BloodBurstEffect)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BloodBurstEffect, Joint, ShotDirection.Rotation(), FVector(BloodEffectScale * 1.1f), true, true);
		// The stump keeps spurting as the body moves
		UNiagaraFunctionLibrary::SpawnSystemAttached(BloodBurstEffect, Body, Parent, Joint, ShotDirection.Rotation(),
			FVector(BloodEffectScale * 0.5f), EAttachLocation::KeepWorldPosition, true, ENCPoolMethod::None);
	}

	if (USoundBase* Sound = LimbSeverSound ? LimbSeverSound.Get() : HeadPopSound.Get())
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Joint, 0.9f, FMath::FRandRange(1.f, 1.15f));
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		SpawnBloodDecal(Joint, (ShotDirection + FMath::VRand() * 0.35f).GetSafeNormal(), FMath::FRandRange(45.f, 75.f));
	}

	if (!IsAlive())
	{
		StartKillHitStop();
	}

	UE_LOG(LogTemp, Display, TEXT("ZombieSever type=%d limb=%s alive=%d"), static_cast<int32>(Type), *Limb.ToString(), IsAlive() ? 1 : 0);
}

void AZombieCharacter::StartKillHitStop() const
{
	UWorld* World = GetWorld();
	if (!bKilledByPlayer || !World || KillHitStopDilation >= 1.f || UGameplayStatics::GetGlobalTimeDilation(World) < 1.f)
	{
		return;
	}

	UGameplayStatics::SetGlobalTimeDilation(World, KillHitStopDilation);
	UE_LOG(LogFirstPersonProject, Verbose, TEXT("ZombieHitStop start real=%.3f"), World->GetRealTimeSeconds());

	// World timers run on slowed and clamped time, which stretches the freeze at low frame rates: count real time
	const double EndTime = FPlatformTime::Seconds() + KillHitStopTime;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(World, [World, EndTime](float)
	{
		if (FPlatformTime::Seconds() < EndTime)
		{
			return true;
		}
		UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
		UE_LOG(LogFirstPersonProject, Verbose, TEXT("ZombieHitStop end real=%.3f"), World->GetRealTimeSeconds());
		return false;
	}));
}

void AZombieCharacter::UpdateFootsteps(float DeltaTime)
{
	if (!IsAlive() || bDormant || !GetCharacterMovement()->IsMovingOnGround())
	{
		return;
	}

	const float Speed = GetVelocity().Size2D();
	if (Speed < 20.f)
	{
		StrideDistance = 0.f;
		return;
	}

	const bool bCrawler = Type == EZombieType::Crawler;
	const TArray<TObjectPtr<USoundBase>>& Sounds = (bCrawler && !DragSounds.IsEmpty()) ? DragSounds : FootstepSounds;
	if (Sounds.IsEmpty())
	{
		return;
	}

	// Running strides are longer
	const float Stride = (bCrawler ? CrawlStrideLength : StrideLength) * GetActorScale3D().X * (Speed > 250.f ? 1.6f : 1.f);
	StrideDistance += Speed * DeltaTime;
	if (StrideDistance < Stride)
	{
		return;
	}
	StrideDistance = 0.f;

	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player || FVector::DistSquared(Player->GetActorLocation(), GetActorLocation()) > FMath::Square(FootstepMaxDistance))
	{
		return;
	}

	// Brutes stomp, stalkers barely make a sound
	float Volume = FootstepVolume * FMath::GetMappedRangeValueClamped(FVector2f(40.f, 400.f), FVector2f(0.7f, 1.3f), Speed);
	float Pitch = FMath::FRandRange(0.85f, 1.05f);
	switch (Type)
	{
	case EZombieType::Brute:
		Volume *= 1.7f;
		Pitch *= 0.75f;
		break;
	case EZombieType::Stalker:
		Volume *= 0.3f;
		break;
	default:
		break;
	}

	UE_LOG(LogFirstPersonProject, Verbose, TEXT("ZombieStep type=%d speed=%.0f volume=%.2f"), static_cast<int32>(Type), Speed, Volume);
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	UGameplayStatics::PlaySoundAtLocation(this, Sounds[FMath::RandRange(0, Sounds.Num() - 1)], Feet, Volume, Pitch, 0.f, SoundAttenuation);
}
