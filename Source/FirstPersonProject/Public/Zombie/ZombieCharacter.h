#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ZombieCharacter.generated.h"

class AZombieCharacter;
class UAnimSequence;
class UMaterialInterface;
class UNiagaraSystem;
class USoundAttenuation;
class USoundBase;
class UZombieAnimInstance;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnZombieDied, AZombieCharacter* /*Zombie*/, bool /*bHeadshot*/);

UENUM(BlueprintType)
enum class EZombieType : uint8
{
	/** Slow, shambling, comes in numbers */
	Walker,
	/** Fast and aggressive, lunges at the player */
	Runner,
	/** Big and tough, hits hard, charges from a distance */
	Brute,
	/** Drags itself on the ground, hard to see in the grass and the fog */
	Crawler,
	/** Keeps its distance and shrieks: speeds up the horde around it and calls more */
	Screamer,
	/** Tall and silent, only moves when you are not looking at it, strikes from behind */
	Stalker
};

/** One attack animation and the part of it that is played */
USTRUCT(BlueprintType)
struct FZombieAttack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> Anim;

	/** Where in the sequence the swing starts */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (Units = "s"))
	float StartTime = 0.f;

	/** Where in the sequence the blow lands */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (Units = "s"))
	float HitTime = 0.5f;

	/** Where in the sequence it hands back to locomotion. Zero: the sequence end. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (Units = "s"))
	float EndTime = 0.f;
};

/** Tuning of one zombie type */
USTRUCT(BlueprintType)
struct FZombieTypeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1"))
	float Health = 120.f;

	/** Speed while it does not know where the player is */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (Units = "cm/s"))
	float WanderSpeed = 70.f;

	/** Speed while chasing */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (Units = "cm/s"))
	float ChaseSpeed = 150.f;

	/** Short burst speed when it closes in (lunge) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (Units = "cm/s"))
	float LungeSpeed = 260.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0"))
	float AttackDamage = 18.f;

	/** Seconds between two attacks */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.1", Units = "s"))
	float AttackCooldown = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.1"))
	float Scale = 1.f;

	/** Damage taken in a short time that makes it stagger */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0"))
	float StaggerDamage = 45.f;

	/** Play rate of the attack animation */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.1"))
	float AttackRate = 1.f;

	/** Own idle and movement cycle (crawling). Empty: the zombie defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UAnimSequence> MoveAnim;

	/** Ground speed of MoveAnim at rate 1 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (Units = "cm/s"))
	float MoveAnimSpeed = 0.f;

	/** Own attacks. Empty: the zombie defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FZombieAttack> Attacks;

	/** Capsule half height for low bodies (crawlers). Zero: the default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (Units = "cm"))
	float CapsuleHalfHeight = 0.f;

	/** Body proportions on top of Scale (tall and thin stalkers) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector MeshScale = FVector::OneVector;

	/** Glowing eyes in the dark. Alpha 0: normal eyes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FLinearColor EyeGlow = FLinearColor(0.f, 0.f, 0.f, 0.f);

	/** Color laid over the whole body (blood, mud, a black silhouette). Alpha is the strength, 0: none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FLinearColor SkinTint = FLinearColor(0.f, 0.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float VoicePitch = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float VoiceVolume = 1.f;

	/** Makes no groans, only whispers now and then */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bSilent = false;
};

/**
 *  Zombie body: health, hit reactions, melee attack, sounds and ragdoll death.
 *  Decisions are taken by AZombieAIController; this class only does what it is told.
 *  Bullets hit the skeletal mesh (its physics bodies), so "head" hits count as headshots.
 */
UCLASS()
class FIRSTPERSONPROJECT_API AZombieCharacter : public ACharacter
{
	GENERATED_BODY()

public:

	AZombieCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaTime) override;

	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Picks the type, scales health by Difficulty (1 = base) and sets speed, size and animations */
	void SetupType(EZombieType InType, float HealthScale = 1.f);

	UFUNCTION(BlueprintPure, Category = "Zombie")
	bool IsAlive() const { return Health > 0.f; }

	UFUNCTION(BlueprintPure, Category = "Zombie")
	EZombieType GetZombieType() const { return Type; }

	const FZombieTypeSettings& GetSettings() const;

	/** Melee attack on Target. Returns false while it cannot attack yet. */
	bool StartAttack(AActor* Target);

	bool IsAttacking() const { return bAttacking; }

	bool CanAttack() const;

	/** Alert scream when it spots the player. Returns its duration. */
	float Scream();

	bool IsBusy() const;

	bool IsStaggered() const;

	/** Moves at chase, wander or lunge speed */
	void SetMoveSpeed(float Speed);

	/** Random groan, called by the AI now and then */
	void Groan();

	/** Lies on the ground like a corpse until woken */
	void SetDormant();

	bool IsDormant() const { return bDormant; }

	/** Gets up from the ground. Returns how long it takes. */
	float WakeUp();

	/** Damages and knocks back Target (charges) */
	void HitTarget(AActor* Target, float Damage, float Knockback);

	/** Faster and fiercer for a while (a screamer's call) */
	void SetFrenzied(float Duration);

	bool IsFrenzied() const;

	/** Seconds since the zombie was last hurt */
	float GetTimeSinceDamaged() const;

	FOnZombieDied OnDied;

protected:

	virtual void BeginPlay() override;

	// Animation ------------------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Animation")
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Animation")
	TObjectPtr<UAnimSequence> WalkAnim;

	/** Optional faster movement cycle for runners. The walk cycle played faster when empty. */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Animation")
	TObjectPtr<UAnimSequence> RunAnim;

	/** Ground speed of WalkAnim / RunAnim at rate 1, so the feet stick to the ground */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Animation", meta = (Units = "cm/s"))
	float WalkAnimSpeed = 48.f;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Animation", meta = (Units = "cm/s"))
	float RunAnimSpeed = 185.f;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Animation")
	TArray<FZombieAttack> Attacks;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Animation")
	TArray<TObjectPtr<UAnimSequence>> ScreamAnims;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Animation")
	TArray<TObjectPtr<UAnimSequence>> HitAnims;

	/** Lying on the ground, playing dead */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Animation")
	TObjectPtr<UAnimSequence> DormantAnim;

	/** Played when it gets up from the ground */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Animation")
	TObjectPtr<UAnimSequence> WakeAnim;

	// Look -----------------------------------------------------------------------------------------

	/** Emissive material for glowing eyes, with a "GlowColor" vector parameter */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Look")
	TObjectPtr<UMaterialInterface> EyeGlowMaterial;

	/** Translucent overlay with a "Tint" vector parameter (alpha = opacity), for the type's SkinTint */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Look")
	TObjectPtr<UMaterialInterface> TintOverlayMaterial;

	/** Material slots of the eyes */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Look")
	TArray<FName> EyeSlots = { TEXT("EyeballRight"), TEXT("EyeballLeft") };

	/** Hair materials picked at random */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Look")
	TArray<TObjectPtr<UMaterialInterface>> HairVariants;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Look")
	FName HairSlot = TEXT("Material #34");

	// Combat ----------------------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Combat")
	TMap<EZombieType, FZombieTypeSettings> TypeSettings;

	/** Reach of the blow from the zombie's center */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Combat", meta = (Units = "cm"))
	float AttackReach = 170.f;

	/** Forward speed while it swings, so it keeps pressing */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Combat", meta = (Units = "cm/s"))
	float AttackStepSpeed = 40.f;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Combat", meta = (Units = "s"))
	float StaggerTime = 0.6f;

	/** Strength of the push a bullet gives to the hit body part */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Combat")
	float HitReactionImpulse = 2500.f;

	/** Bodies under this bone flinch physically when shot */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Combat")
	FName HitReactionRootBone = TEXT("spine_01");

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Combat")
	float DeathImpulse = 9000.f;

	/** Killing blows stronger than this throw the body further (shotgun at close range) */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Combat")
	float HeavyKillDamage = 90.f;

	/** A head shot dealing at least this much in one burst takes the head off */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Combat")
	float HeadPopDamage = 70.f;

	// Gore -----------------------------------------------------------------------------------------

	/** Blood spray where a bullet hits */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Gore")
	TObjectPtr<UNiagaraSystem> BloodHitEffect;

	/** Big burst when the head comes off or a blast kills */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Gore")
	TObjectPtr<UNiagaraSystem> BloodBurstEffect;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Gore")
	float BloodEffectScale = 1.f;

	/** Splatter left on the ground and walls behind the zombie */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Gore")
	TArray<TObjectPtr<UMaterialInterface>> BloodDecals;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Gore", meta = (Units = "s"))
	float BloodDecalLifeSpan = 40.f;

	/** Limbs that come off. A hit on a child bone (hand, foot, twist) counts for the limb above it. */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Gore")
	TArray<FName> SeverableLimbs = { TEXT("lowerarm_l"), TEXT("lowerarm_r"), TEXT("upperarm_l"), TEXT("upperarm_r"),
		TEXT("calf_l"), TEXT("calf_r"), TEXT("thigh_l"), TEXT("thigh_r") };

	/** Damage on one limb in one burst that tears it off (arms while alive, any limb once dead, 70% of it on the dead) */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Gore")
	float LimbSeverDamage = 55.f;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Gore")
	int32 MaxSeveredLimbs = 2;

	// Kill feel ------------------------------------------------------------------------------------

	/** Time scale for a split second after a brutal kill (shotgun blast, head off, limb off). 1: off. */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Combat", meta = (ClampMin = "0.01", ClampMax = "1"))
	float KillHitStopDilation = 0.08f;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Combat", meta = (Units = "s"))
	float KillHitStopTime = 0.07f;

	/** Seconds the body stays after death */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Combat", meta = (Units = "s"))
	float CorpseLifeSpan = 30.f;

	// Sound -----------------------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	TArray<TObjectPtr<USoundBase>> GroanSounds;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	TArray<TObjectPtr<USoundBase>> ScreamSounds;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	TArray<TObjectPtr<USoundBase>> AttackSounds;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	TArray<TObjectPtr<USoundBase>> PainSounds;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	TArray<TObjectPtr<USoundBase>> DeathSounds;

	/** Flesh being hit by a bullet, played where it lands */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	TArray<TObjectPtr<USoundBase>> FleshHitSounds;

	/** Bullet cracking into the skull */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	TArray<TObjectPtr<USoundBase>> HeadHitSounds;

	/** Wet crunch of a head shot off */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	TObjectPtr<USoundBase> HeadPopSound;

	/** Wet tear when a limb comes off. Empty: HeadPopSound. */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	TObjectPtr<USoundBase> LimbSeverSound;

	/** Steps on the forest floor */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	TArray<TObjectPtr<USoundBase>> FootstepSounds;

	/** Body dragged over the ground (crawlers) */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	TArray<TObjectPtr<USoundBase>> DragSounds;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	float FootstepVolume = 0.55f;

	/** Distance between two steps at walking pace */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound", meta = (Units = "cm"))
	float StrideLength = 75.f;

	/** Distance between two pulls of a crawler */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound", meta = (Units = "cm"))
	float CrawlStrideLength = 55.f;

	/** Steps further than this from the player are not played */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound", meta = (Units = "cm"))
	float FootstepMaxDistance = 2800.f;

	/** Quiet whispers of silent types */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	TArray<TObjectPtr<USoundBase>> WhisperSounds;

	/** Stinger when a zombie hits the player from out of his view */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	TObjectPtr<USoundBase> JumpscareSound;

	/** Played on the victim when a blow lands */
	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	TObjectPtr<USoundBase> HitPlayerSound;

	UPROPERTY(EditDefaultsOnly, Category = "Zombie|Sound")
	TObjectPtr<USoundAttenuation> SoundAttenuation;

private:

	void LandAttack();
	void EndAttack();
	void Die(const FVector& ImpulseDirection, FName HitBone, bool bHeadshot);
	void StartHitReaction(FName Bone, const FVector& Direction, float Damage);
	void UpdateHitReaction(float DeltaTime);
	void PlaySound(const TArray<TObjectPtr<USoundBase>>& Sounds, float VolumeMultiplier = 1.f) const;

	UZombieAnimInstance* GetZombieAnim() const;
	const TArray<FZombieAttack>& GetAttacks() const;
	void ApplyLook();
	void PlayJumpscareIfUnseen(const AActor* Victim) const;
	void SpawnBloodHit(const FVector& Location, const FVector& ShotDirection, float Damage, bool bHead = false);
	void SpawnBloodDecal(const FVector& Location, const FVector& Direction, float Size) const;
	void PopHead(const FVector& ShotDirection);

	/** Limb of SeverableLimbs that Bone belongs to, or none */
	FName FindLimb(FName Bone) const;
	/** Adds a pellet's damage to its limb and tears the limb off when the burst is strong enough */
	void TrackLimbDamage(FName HitBone, float Damage, const FVector& ShotDirection);
	void SeverLimb(FName Limb, const FVector& ShotDirection);
	void StartKillHitStop() const;
	void UpdateFootsteps(float DeltaTime);
	/** Starts a new burst when the frame changed */
	void BeginFrameDamage();

	EZombieType Type = EZombieType::Walker;
	float Health = 100.f;
	float MaxHealth = 100.f;

	bool bAttacking = false;
	float LastAttackTime = -100.f;
	float LastDamageTime = -100.f;
	float StaggerUntil = 0.f;
	float BusyUntil = 0.f;

	/** Damage taken recently, decays over time, staggers when it passes the threshold */
	float RecentDamage = 0.f;

	float HitReactionWeight = 0.f;
	bool bHitReactionActive = false;
	bool bDormant = false;
	bool bHeadPopped = false;

	/** Damage taken during the current frame, so a shotgun blast counts as one hit */
	float FrameDamage = 0.f;
	uint64 FrameDamageFrame = 0;
	float LastFleshSoundTime = -100.f;
	float FrenzyUntil = 0.f;

	/** Damage per limb during the current frame */
	TMap<FName, float> FrameLimbDamage;
	TArray<FName> SeveredLimbs;
	bool bKilledByPlayer = false;

	/** Distance walked since the last footstep */
	float StrideDistance = 0.f;

	TWeakObjectPtr<AActor> AttackTarget;
	FTimerHandle AttackHitTimer;
	FTimerHandle AttackEndTimer;
};
