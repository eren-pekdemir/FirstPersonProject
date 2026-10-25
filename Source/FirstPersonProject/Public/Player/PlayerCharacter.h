#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PlayerCharacter.generated.h"

class AGunBase;
class UInputAction;
class UInteractPromptWidget;
class UUserWidget;
struct FInputActionValue;

/**
 *  First person player character.
 *  Handles movement and sprinting, looking at / picking up weapons and forwarding fire input to the held gun.
 */
UCLASS()
class FIRSTPERSONPROJECT_API APlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:

	APlayerCharacter();

	virtual void Tick(float DeltaTime) override;

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** True while a weapon is held. Use it to drive weapon-only anim nodes such as the left hand IK. */
	UFUNCTION(BlueprintPure, Category = "Weapon", meta = (BlueprintThreadSafe))
	bool HasGun() const { return CurrentGun != nullptr; }

	/** Weapon currently held, or null when unarmed */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<AGunBase> CurrentGun;

protected:

	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Weapon spawned and equipped on BeginPlay. Leave empty to start unarmed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TSubclassOf<AGunBase> DefaultGun;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float WalkSpeed = 300.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0", Units = "cm/s"))
	float SprintSpeed = 600.f;

	/** How fast the max walk speed blends between walking and sprinting */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0"))
	float SprintInterpSpeed = 5.f;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

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

private:

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

	void OnSprintPressed() { bWantsToSprint = true; }
	void OnSprintReleased() { bWantsToSprint = false; }

	void OnFirePressed();
	void OnFireReleased();

	void OnInteractPressed();

	void CreateHUDWidgets();

	/** Blends the max walk speed and starts firing once the character has slowed down from a sprint */
	void UpdateMovementAndFiring(float DeltaTime);

	void StopFiring();

	/** Looks for a weapon under the crosshair and updates the interaction prompt */
	void UpdateInteractTrace();

	void SetFocusedGun(AGunBase* NewFocusedGun);

	/** Equips NewGun. A currently held gun is dropped where NewGun was lying (weapon swap). */
	void EquipGun(AGunBase* NewGun);

	void DropCurrentGun(const FTransform& DropTransform);

	UPROPERTY()
	TObjectPtr<UUserWidget> CrosshairWidget;

	UPROPERTY()
	TObjectPtr<UInteractPromptWidget> InteractPromptWidget;

	bool bWantsToSprint = false;
	bool bWantsToFire = false;
	bool bIsFiring = false;
};
