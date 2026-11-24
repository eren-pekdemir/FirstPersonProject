#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Player/TraversalComponent.h"
#include "Player/ViewmodelComponent.h"
#include "PlayerCharacter.generated.h"

class AGunBase;
class UCameraComponent;
class UCameraFeelComponent;
class UViewmodelComponent;
class UCameraShakeBase;
class UInputAction;
class UInteractPromptWidget;
class USoundBase;
class USpotLightComponent;
class UUserWidget;
struct FGunShotResult;
struct FInputActionValue;

/** A damaging hit, kept around for hit markers and floating damage numbers on the HUD */
USTRUCT(BlueprintType)
struct FPlayerHitEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	float Damage = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float Time = 0.f;

	UPROPERTY(BlueprintReadOnly)
	bool bHeadshot = false;

	UPROPERTY(BlueprintReadOnly)
	bool bKilled = false;
};

/**
 *  First person player character.
 *  Fast, Apex-like movement (sprint, slide, slide jump, mantle, vault, coyote time) and a weapon
 *  inventory with ADS, reloading and hit feedback.
 *  Implementation is split in PlayerCharacter.cpp (core), PlayerCharacterMovement.cpp and PlayerCharacterWeapons.cpp.
 */
UCLASS()
class FIRSTPERSONPROJECT_API APlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:

	APlayerCharacter();

	virtual void Tick(float DeltaTime) override;

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	virtual void Landed(const FHitResult& Hit) override;

	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;

	virtual bool CanJumpInternal_Implementation() const override;

	// ---------------------------------------------------------------------------------------------
	// State queries (HUD, animation)

	/** True while a weapon is held. Use it to drive weapon-only anim nodes such as the left hand IK. */
	UFUNCTION(BlueprintPure, Category = "Weapon", meta = (BlueprintThreadSafe))
	bool HasGun() const { return CurrentGun != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	bool IsAiming() const { return bIsAiming; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsSprinting() const { return bIsSprinting; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsSliding() const { return bIsSliding; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsTraversing() const;

	const TArray<TObjectPtr<AGunBase>>& GetWeapons() const { return Weapons; }

	int32 GetMaxWeapons() const { return MaxWeapons; }

	const TArray<FPlayerHitEvent>& GetRecentHits() const { return RecentHits; }

	/** Weapon currently held, or null when unarmed */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<AGunBase> CurrentGun;

	// ---------------------------------------------------------------------------------------------
	// Health (PlayerCharacterHealth.cpp)

	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealthPercent() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return bDead; }

	/** Back to full health (between waves) */
	void RestoreHealth();

	/** Heals by Amount. Returns false when already at full health. */
	bool AddHealth(float Amount);

	/** Charges the flashlight battery by a fraction of a full one. Returns false when already full. */
	bool AddBattery(float Fraction);

	UFUNCTION(BlueprintPure, Category = "Flashlight")
	float GetBatteryPercent() const { return Battery; }

	bool IsFlashlightOn() const;

	/** Short message on the HUD (a picked up item) */
	void ShowPickupMessage(const FString& Text, const FLinearColor& Color);

	const FString& GetPickupMessage() const { return PickupMessage; }
	const FLinearColor& GetPickupMessageColor() const { return PickupMessageColor; }
	float GetTimeSincePickupMessage() const;

	/** 1 right after being hurt, fading to 0, for the HUD */
	float GetDamageFlash() const;

	/** Where the last hit came from, as a yaw relative to the view (degrees), for the HUD indicator */
	float GetDamageDirectionYaw() const;

	float GetTimeSinceDamaged() const;

protected:

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ---------------------------------------------------------------------------------------------
	// Components

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraFeelComponent> CameraFeel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTraversalComponent> Traversal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UViewmodelComponent> Viewmodel;

	/** First person arms holding the weapon, only visible to the owner. Driven by Viewmodel. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> FirstPersonArms;

	/** Gloves or sleeves following the arms pose */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkeletalMeshComponent> FirstPersonGloves;

	/** Meshes and default animation of the first person arms */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person")
	FFirstPersonArmsSetup ArmsSetup;

	// ---------------------------------------------------------------------------------------------
	// Weapons

	/** Weapons spawned and given on BeginPlay. The first one is equipped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TArray<TSubclassOf<AGunBase>> StartingWeapons;

	/** Legacy single starting weapon, used when StartingWeapons is empty */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TSubclassOf<AGunBase> DefaultGun;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "1", ClampMax = "9"))
	int32 MaxWeapons = 3;

	/** Played when a shot damages something */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USoundBase> HitMarkerSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USoundBase> HeadshotSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USoundBase> KillSound;

	/** Delay before an empty weapon reloads by itself */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0", Units = "s"))
	float AutoReloadDelay = 0.25f;

	// ---------------------------------------------------------------------------------------------
	// Movement

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float WalkSpeed = 420.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float SprintSpeed = 680.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float CrouchSpeed = 220.f;

	/** How fast the max walk speed blends between speeds */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0"))
	float SprintInterpSpeed = 6.f;

	/** Time after walking off a ledge during which jumping still works */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Jump", meta = (ClampMin = "0", Units = "s"))
	float CoyoteTime = 0.12f;

	/** A jump pressed this long before landing is performed on landing */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Jump", meta = (ClampMin = "0", Units = "s"))
	float JumpBufferTime = 0.15f;

	/** Minimum horizontal speed to start a slide from a sprint */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Slide", meta = (ClampMin = "0", Units = "cm/s"))
	float SlideMinStartSpeed = 500.f;

	/** Speed added when a slide starts (not when chained within the cooldown) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Slide", meta = (ClampMin = "0", Units = "cm/s"))
	float SlideBoost = 260.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Slide", meta = (ClampMin = "0", Units = "cm/s"))
	float SlideMaxSpeed = 1150.f;

	/** Deceleration while sliding on flat ground */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Slide", meta = (ClampMin = "0"))
	float SlideFriction = 380.f;

	/** How strongly slopes accelerate a slide (multiplier on gravity) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Slide", meta = (ClampMin = "0"))
	float SlideGravityScale = 1.4f;

	/** Steering strength during a slide, as an acceleration */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Slide", meta = (ClampMin = "0"))
	float SlideSteering = 250.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Slide", meta = (ClampMin = "0", Units = "s"))
	float SlideBoostCooldown = 1.2f;

	/** How far the body and camera drop while crouching or sliding */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement|Slide", meta = (ClampMin = "0", Units = "cm"))
	float CrouchCameraDrop = 45.f;

	// ---------------------------------------------------------------------------------------------
	// Feedback

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback")
	TArray<TObjectPtr<USoundBase>> FootstepSounds;

	/** Distance between footsteps at walk speed. Shorter at sprint speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback", meta = (ClampMin = "10", Units = "cm"))
	float FootstepStride = 170.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<USoundBase> JumpSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<USoundBase> LandSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<USoundBase> SlideSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback")
	TObjectPtr<USoundBase> MantleSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback")
	TSubclassOf<UCameraShakeBase> LandCameraShake;

	/** Fall speed at which landing feedback is at full strength */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Feedback", meta = (ClampMin = "1", Units = "cm/s"))
	float HardLandingSpeed = 1100.f;

	// ---------------------------------------------------------------------------------------------
	// Input

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> AimAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> ReloadAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> CrouchAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	/** Axis: positive selects the next weapon, negative the previous one */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> SwitchWeaponAction;

	/** One action per slot: index 0 selects the first weapon */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TArray<TObjectPtr<UInputAction>> SelectWeaponActions;

	// ---------------------------------------------------------------------------------------------
	// Interaction / UI

	/** Max distance from the camera at which a weapon can be picked up */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction", meta = (ClampMin = "0", Units = "cm"))
	float InteractTraceDistance = 250.f;

	/** Weapon under the crosshair that would be picked up by the interact input */
	UPROPERTY(BlueprintReadOnly, Category = "Interaction")
	TObjectPtr<AGunBase> FocusedGun;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> CrosshairWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UInteractPromptWidget> InteractPromptWidgetClass;

	// ---------------------------------------------------------------------------------------------
	// Health

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health", meta = (ClampMin = "1"))
	float MaxHealth = 100.f;

	/** Seconds without damage before health comes back */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health", meta = (ClampMin = "0", Units = "s"))
	float RegenDelay = 5.f;

	/** Health per second once regenerating */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health", meta = (ClampMin = "0"))
	float RegenRate = 12.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health")
	TArray<TObjectPtr<USoundBase>> HurtSounds;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health")
	TObjectPtr<USoundBase> DeathSound;

	/** Heartbeat looping while health is low */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health")
	TObjectPtr<USoundBase> LowHealthSound;

	// ---------------------------------------------------------------------------------------------
	// Flashlight

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USpotLightComponent> Flashlight;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Flashlight")
	TObjectPtr<USoundBase> FlashlightClickSound;

	/** Seconds a full battery lasts with the light on */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Flashlight", meta = (ClampMin = "1", Units = "s"))
	float BatteryLife = 200.f;

	/** Charge under which the light dims and keeps failing */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Flashlight", meta = (ClampMin = "0", ClampMax = "1"))
	float LowBatteryFraction = 0.25f;

	/** Played when the battery dies */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Flashlight")
	TObjectPtr<USoundBase> BatteryDeadSound;

private:

	// Health (PlayerCharacterHealth.cpp) ------------------------------------------------------------

	void UpdateHealth(float DeltaTime);
	void Die();
	void ToggleFlashlight();
	void UpdateFlashlight(float DeltaTime);

	float Health = 100.f;
	bool bDead = false;
	float LastDamageTime = -100.f;
	FVector LastDamageOrigin = FVector::ZeroVector;
	float FlashlightFlickerTime = 0.f;
	float FlashlightBaseIntensity = 0.f;

	/** Flashlight charge, 0..1 */
	float Battery = 1.f;

	FString PickupMessage;
	FLinearColor PickupMessageColor = FLinearColor::White;
	float PickupMessageTime = -100.f;

	UPROPERTY()
	TObjectPtr<class UAudioComponent> LowHealthAudio;


	// Core (PlayerCharacter.cpp) -----------------------------------------------------------------

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

	void OnInteractPressed();

	void CreateHUDWidgets();

	/** Looks for a weapon under the crosshair and updates the interaction prompt */
	void UpdateInteractTrace();

	void SetFocusedGun(AGunBase* NewFocusedGun);

	// Movement (PlayerCharacterMovement.cpp) ------------------------------------------------------

	void OnSprintPressed() { bWantsToSprint = true; }
	void OnSprintReleased() { bWantsToSprint = false; }

	void OnJumpPressed();
	void OnJumpReleased();

	void OnCrouchPressed();
	void OnCrouchReleased();

	void UpdateMovement(float DeltaTime);
	void UpdateSprint(float DeltaTime);

	void StartSlide();
	void StopSlide();
	void UpdateSlide(float DeltaTime);

	/** Moves the body (and the camera attached to it) down while crouched */
	void UpdateCrouchOffset(float DeltaTime);

	void UpdateFootsteps(float DeltaTime);
	void UpdateCameraFeel();

	void OnTraversalStarted(ETraversalType Type, float Height);
	void OnTraversalEnded(ETraversalType Type);

	void PlaySoundAtFeet(USoundBase* Sound, float VolumeMultiplier = 1.f) const;

	// Weapons (PlayerCharacterWeapons.cpp) ---------------------------------------------------------

	void OnFirePressed();
	void OnFireReleased();
	void OnAimPressed();
	void OnAimReleased();
	void OnReloadPressed();
	void OnSwitchWeapon(const FInputActionValue& Value);
	void OnSelectWeapon(int32 SlotIndex);

	void GiveStartingWeapons();

	/** Adds a weapon to the inventory. When the inventory is full it replaces the held weapon, which is dropped. */
	void PickUpGun(AGunBase* NewGun);

	/** Makes the inventory weapon at SlotIndex the held one */
	void EquipSlot(int32 SlotIndex);

	void AttachGunToHand(AGunBase* Gun);
	void DropGun(AGunBase* Gun, const FTransform& DropTransform);

	void BindGunEvents(AGunBase* Gun);
	void UnbindGunEvents(AGunBase* Gun);

	void HandleGunFired(const AGunBase& Gun);
	void HandleGunHit(const FGunShotResult& Result);
	void HandleGunEmpty(AGunBase& Gun);

	/** Starts firing once nothing blocks it (sprint slow-down, traversal, equip) */
	void UpdateFiring();

	void UpdateAiming();

	void StopFiring();

	void TryReload();

	bool CanUseWeapon() const;

	// Debug (PlayerCharacterDebug.cpp) -----------------------------------------------------------

	/** With -ViewmodelCapture on the command line: equips every weapon in the level and takes screenshots of each pose */
	void StartViewmodelCapture();
	void RunViewmodelCaptureStep();

	int32 ViewmodelCaptureStep = 0;
	FTimerHandle ViewmodelCaptureTimer;

	/** Debug view from outside the head, to check how the hands hold the weapon */
	void SetOutsideView(bool bOutside, float SideOffset = -110.f);
	TWeakObjectPtr<AActor> OutsideViewCamera;

	/** With -FeelCapture: scripted look, move, sprint and fire sequence with screenshots and a per frame log */
	void StartFeelCapture();

	/** With -ZombieCapture: fights the waves by itself (aims at the nearest zombie and fires), logs the horde and takes screenshots */
	void TickZombieCapture(float DeltaTime);
	bool bZombieCapture = false;
	float ZombieCaptureTime = 0.f;
	int32 ZombieCaptureShot = 0;
	float ZombieCaptureLogTimer = 0.f;
	void TickFeelCapture(float DeltaTime);
	int32 FindWeaponSlot(const TCHAR* ClassNamePart) const;

	bool bFeelCapture = false;
	int32 FeelSegment = 0;
	int32 FeelShot = 0;
	float FeelTime = 0.f;
	float FeelTotalTime = 0.f;

	// State ---------------------------------------------------------------------------------------

	UPROPERTY()
	TArray<TObjectPtr<AGunBase>> Weapons;

	UPROPERTY()
	TObjectPtr<UUserWidget> CrosshairWidget;

	UPROPERTY()
	TObjectPtr<UInteractPromptWidget> InteractPromptWidget;

	UPROPERTY()
	TArray<FPlayerHitEvent> RecentHits;

	FTimerHandle AutoReloadTimer;

	FVector2D LastMoveInput = FVector2D::ZeroVector;

	bool bWantsToSprint = false;
	bool bIsSprinting = false;
	bool bWantsToFire = false;
	bool bIsFiring = false;
	bool bWantsToAim = false;
	bool bIsAiming = false;
	bool bWantsToCrouch = false;
	bool bIsSliding = false;

	float SprintAlpha = 0.f;
	float LastGroundedTime = 0.f;
	float LastJumpPressTime = -1.f;
	float LastSlideTime = -100.f;
	bool bJumpedSinceGrounded = false;

	float CrouchOffset = 0.f;
	float FootstepDistance = 0.f;

	/** Movement settings overridden while sliding, restored afterwards */
	float SavedGroundFriction = 8.f;
	float SavedBrakingDeceleration = 2048.f;
	float SavedMaxAcceleration = 2048.f;
};
