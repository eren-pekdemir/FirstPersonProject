#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Zombie/ZombieCharacter.h"
#include "Zombie/ZombiePickup.h"
#include "ZombieWaveDirector.generated.h"

class UAudioComponent;
class ULightComponent;
class USoundAttenuation;
class USoundBase;

UENUM(BlueprintType)
enum class EWavePhase : uint8
{
	/** Short calm before the first wave */
	Prepare,
	Intermission,
	Active,
	GameOver
};

/**
 *  Runs the survival game: waves of zombies that grow in number, speed and toughness.
 *  Spawns them out of the player's sight at spawn points (actors tagged ZombieSpawn), keeps a cap
 *  on how many are alive, leaves supplies (ammo, first aid, batteries) between waves and switches the music between
 *  calm and combat. The HUD reads its state.
 */
UCLASS()
class FIRSTPERSONPROJECT_API AZombieWaveDirector : public AActor
{
	GENERATED_BODY()

public:

	AZombieWaveDirector();

	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintPure, Category = "Waves")
	int32 GetWave() const { return Wave; }

	UFUNCTION(BlueprintPure, Category = "Waves")
	EWavePhase GetPhase() const { return Phase; }

	/** Zombies still to kill this wave (alive and not spawned yet) */
	UFUNCTION(BlueprintPure, Category = "Waves")
	int32 GetZombiesRemaining() const { return ToSpawn + AliveZombies.Num(); }

	UFUNCTION(BlueprintPure, Category = "Waves")
	float GetPhaseTimeLeft() const { return FMath::Max(0.f, PhaseEndTime - GetWorld()->GetTimeSeconds()); }

	UFUNCTION(BlueprintPure, Category = "Waves")
	int32 GetKills() const { return Kills; }

	UFUNCTION(BlueprintPure, Category = "Waves")
	int32 GetHeadshots() const { return Headshots; }

	/** Called when the player dies */
	void EndGame();

	/** Extra zombies this wave (a screamer's call) */
	void AddBonusZombies(int32 Count);

protected:

	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, Category = "Waves")
	TSubclassOf<AZombieCharacter> ZombieClass;

	UPROPERTY(EditAnywhere, Category = "Waves", meta = (Units = "s"))
	float PrepareTime = 6.f;

	UPROPERTY(EditAnywhere, Category = "Waves", meta = (Units = "s"))
	float IntermissionTime = 12.f;

	UPROPERTY(EditAnywhere, Category = "Waves")
	int32 FirstWaveCount = 6;

	UPROPERTY(EditAnywhere, Category = "Waves")
	int32 ExtraPerWave = 4;

	/** Zombies alive at once on wave 1, grows by MaxAliveGrowth each wave up to MaxAliveCap */
	UPROPERTY(EditAnywhere, Category = "Waves")
	int32 FirstWaveMaxAlive = 6;

	UPROPERTY(EditAnywhere, Category = "Waves")
	int32 MaxAliveGrowth = 2;

	UPROPERTY(EditAnywhere, Category = "Waves")
	int32 MaxAliveCap = 22;

	UPROPERTY(EditAnywhere, Category = "Waves", meta = (Units = "s"))
	FVector2D SpawnInterval = FVector2D(0.8f, 2.2f);

	/** Health multiplier added per wave */
	UPROPERTY(EditAnywhere, Category = "Waves")
	float HealthPerWave = 0.08f;

	/** Share of runners on wave 2, grows each wave */
	UPROPERTY(EditAnywhere, Category = "Waves")
	float RunnerChancePerWave = 0.08f;

	UPROPERTY(EditAnywhere, Category = "Waves")
	float MaxRunnerChance = 0.45f;

	/** First wave with brutes */
	UPROPERTY(EditAnywhere, Category = "Waves")
	int32 BruteFirstWave = 3;

	UPROPERTY(EditAnywhere, Category = "Waves")
	float BruteChance = 0.1f;

	UPROPERTY(EditAnywhere, Category = "Waves")
	int32 CrawlerFirstWave = 2;

	UPROPERTY(EditAnywhere, Category = "Waves")
	float CrawlerChance = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Waves")
	int32 ScreamerFirstWave = 3;

	UPROPERTY(EditAnywhere, Category = "Waves")
	float ScreamerChance = 0.12f;

	UPROPERTY(EditAnywhere, Category = "Waves")
	int32 StalkerFirstWave = 4;

	UPROPERTY(EditAnywhere, Category = "Waves")
	float StalkerChance = 0.1f;

	/** Zombies lying among the corpses, playing dead, from this wave on */
	UPROPERTY(EditAnywhere, Category = "Waves")
	int32 DormantFirstWave = 2;

	UPROPERTY(EditAnywhere, Category = "Waves")
	int32 MaxDormant = 4;

	// Horror ----------------------------------------------------------------------------------------

	/** Seconds between two scares (distant screams, flickering lights, whispers) */
	UPROPERTY(EditAnywhere, Category = "Horror", meta = (Units = "s"))
	FVector2D ScareInterval = FVector2D(9.f, 22.f);

	/** Screams heard far away in the forest */
	UPROPERTY(EditAnywhere, Category = "Horror")
	TArray<TObjectPtr<USoundBase>> DistantScreams;

	/** Whispers and stingers played close to the player */
	UPROPERTY(EditAnywhere, Category = "Horror")
	TArray<TObjectPtr<USoundBase>> Whispers;

	UPROPERTY(EditAnywhere, Category = "Horror")
	TObjectPtr<USoundAttenuation> DistantAttenuation;

	/** Played when a light dies for a moment */
	UPROPERTY(EditAnywhere, Category = "Horror")
	TObjectPtr<USoundBase> LightFlickerSound;

	/** Spawn points closer than this to the player are avoided */
	UPROPERTY(EditAnywhere, Category = "Waves", meta = (Units = "cm"))
	float MinSpawnDistance = 1400.f;

	UPROPERTY(EditAnywhere, Category = "Waves")
	FName SpawnPointTag = TEXT("ZombieSpawn");

	// Supplies --------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, Category = "Supplies")
	TSubclassOf<AZombiePickup> PickupClass;

	/** Chances that a dead zombie leaves something behind (one roll) */
	UPROPERTY(EditAnywhere, Category = "Supplies", meta = (ClampMin = "0", ClampMax = "1"))
	float AmmoDropChance = 0.14f;

	UPROPERTY(EditAnywhere, Category = "Supplies", meta = (ClampMin = "0", ClampMax = "1"))
	float HealthDropChance = 0.05f;

	UPROPERTY(EditAnywhere, Category = "Supplies", meta = (ClampMin = "0", ClampMax = "1"))
	float BatteryDropChance = 0.05f;

	/** Seconds a dropped item stays */
	UPROPERTY(EditAnywhere, Category = "Supplies", meta = (Units = "s"))
	float DropLifeSpan = 35.f;

	/** Left around the player after each wave */
	UPROPERTY(EditAnywhere, Category = "Supplies")
	int32 AmmoSuppliesPerWave = 2;

	UPROPERTY(EditAnywhere, Category = "Supplies")
	int32 HealthSuppliesPerWave = 1;

	UPROPERTY(EditAnywhere, Category = "Supplies")
	int32 BatterySuppliesPerWave = 1;

	UPROPERTY(EditAnywhere, Category = "Supplies", meta = (Units = "s"))
	float SupplyLifeSpan = 150.f;

	/** Supplies are placed this far from the player */
	UPROPERTY(EditAnywhere, Category = "Supplies", meta = (Units = "cm"))
	FVector2D SupplyDistance = FVector2D(350.f, 1100.f);

	// Sound -----------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, Category = "Sound")
	TObjectPtr<USoundBase> CalmMusic;

	UPROPERTY(EditAnywhere, Category = "Sound")
	TObjectPtr<USoundBase> CombatMusic;

	UPROPERTY(EditAnywhere, Category = "Sound")
	TObjectPtr<USoundBase> WaveStartSound;

	UPROPERTY(EditAnywhere, Category = "Sound")
	TObjectPtr<USoundBase> WaveEndSound;

	/** Background ambience, loops the whole game */
	UPROPERTY(EditAnywhere, Category = "Sound")
	TObjectPtr<USoundBase> Ambience;

	UPROPERTY(EditAnywhere, Category = "Sound")
	float MusicVolume = 0.55f;

private:

	void StartWave();
	void EndWave();
	void SpawnZombie();
	EZombieType PickType() const;
	bool PickSpawnTransform(FTransform& OutTransform) const;
	void HandleZombieDied(AZombieCharacter* Zombie, bool bHeadshot);
	void PlayMusic(USoundBase* Music);
	void SpawnSupplies() const;
	AZombiePickup* SpawnPickup(EZombiePickupType Type, const FVector& Location, float LifeSpan) const;
	void DropLoot(const AZombieCharacter& Zombie) const;
	void SpawnDormantZombies();
	AZombieCharacter* SpawnZombieAt(const FTransform& SpawnTransform, EZombieType ZombieType);
	void UpdateScares(float DeltaTime);
	void PlayScare();
	void FlickerLight(ULightComponent* Light);

	APawn* GetPlayerPawn() const;

	EWavePhase Phase = EWavePhase::Prepare;
	int32 Wave = 0;
	int32 ToSpawn = 0;
	int32 Kills = 0;
	int32 Headshots = 0;
	float PhaseEndTime = 0.f;
	float NextSpawnTime = 0.f;

	UPROPERTY()
	TArray<TObjectPtr<AZombieCharacter>> AliveZombies;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> SpawnPoints;

	UPROPERTY()
	TObjectPtr<UAudioComponent> MusicComponent;

	UPROPERTY()
	TObjectPtr<UAudioComponent> AmbienceComponent;

	/** Lights of the level that can flicker */
	UPROPERTY()
	TArray<TObjectPtr<ULightComponent>> LevelLights;

	float NextScareTime = 0.f;
};
