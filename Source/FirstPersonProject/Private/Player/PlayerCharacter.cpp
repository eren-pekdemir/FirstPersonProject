#include "Player/PlayerCharacter.h"

#include "Blueprint/UserWidget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Gun/GunBase.h"
#include "InputActionValue.h"
#include "UI/InteractPromptWidget.h"

namespace PlayerCharacter
{
	/** Speed tolerance above WalkSpeed under which the character counts as no longer sprinting */
	constexpr float FireSpeedTolerance = 50.f;
}

APlayerCharacter::APlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
}

void APlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;

	if (DefaultGun)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		EquipGun(GetWorld()->SpawnActor<AGunBase>(DefaultGun, GetActorTransform(), SpawnParams));
	}

	if (IsLocallyControlled())
	{
		CreateHUDWidgets();
	}
}

void APlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopFiring();

	Super::EndPlay(EndPlayReason);
}

void APlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateMovementAndFiring(DeltaTime);

	if (IsLocallyControlled())
	{
		UpdateInteractTrace();
	}
}

void APlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		return;
	}

	if (MoveAction)
	{
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Move);
	}

	if (LookAction)
	{
		EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Look);
	}

	if (FireAction)
	{
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Started, this, &APlayerCharacter::OnFirePressed);
		EnhancedInput->BindAction(FireAction, ETriggerEvent::Completed, this, &APlayerCharacter::OnFireReleased);
	}

	if (SprintAction)
	{
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &APlayerCharacter::OnSprintPressed);
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this, &APlayerCharacter::OnSprintReleased);
	}

	if (InteractAction)
	{
		EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &APlayerCharacter::OnInteractPressed);
	}
}

void APlayerCharacter::CreateHUDWidgets()
{
	APlayerController* PlayerController = GetController<APlayerController>();
	if (!PlayerController)
	{
		return;
	}

	if (CrosshairWidgetClass)
	{
		CrosshairWidget = CreateWidget<UUserWidget>(PlayerController, CrosshairWidgetClass);
		if (CrosshairWidget)
		{
			CrosshairWidget->AddToViewport();
		}
	}

	if (InteractPromptWidgetClass)
	{
		InteractPromptWidget = CreateWidget<UInteractPromptWidget>(PlayerController, InteractPromptWidgetClass);
		if (InteractPromptWidget)
		{
			InteractPromptWidget->AddToViewport();
			InteractPromptWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

// -------------------------------------------------------------------------------------------------
// Movement

void APlayerCharacter::Move(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}

	const FVector2D Input = Value.Get<FVector2D>();
	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FRotationMatrix YawMatrix(YawRotation);

	AddMovementInput(YawMatrix.GetUnitAxis(EAxis::X), Input.Y);
	AddMovementInput(YawMatrix.GetUnitAxis(EAxis::Y), Input.X);
}

void APlayerCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();

	AddControllerYawInput(Input.X);
	AddControllerPitchInput(Input.Y);
}

void APlayerCharacter::UpdateMovementAndFiring(float DeltaTime)
{
	// Holding the trigger cancels the sprint
	const bool bSprinting = bWantsToSprint && !bWantsToFire;
	const float TargetSpeed = bSprinting ? SprintSpeed : WalkSpeed;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = FMath::FInterpTo(Movement->MaxWalkSpeed, TargetSpeed, DeltaTime, SprintInterpSpeed);

	// Weapons can't be fired at sprint speed: wait until the character has slowed down
	if (bWantsToFire && !bIsFiring && CurrentGun)
	{
		if (GetVelocity().Size() <= WalkSpeed + PlayerCharacter::FireSpeedTolerance)
		{
			CurrentGun->StartFire();
			bIsFiring = true;
		}
	}
}

// -------------------------------------------------------------------------------------------------
// Firing

void APlayerCharacter::OnFirePressed()
{
	bWantsToFire = true;
}

void APlayerCharacter::OnFireReleased()
{
	bWantsToFire = false;
	StopFiring();
}

void APlayerCharacter::StopFiring()
{
	if (CurrentGun && bIsFiring)
	{
		CurrentGun->StopFire();
	}

	bIsFiring = false;
}

// -------------------------------------------------------------------------------------------------
// Interaction

void APlayerCharacter::UpdateInteractTrace()
{
	const APlayerController* PlayerController = GetController<APlayerController>();
	if (!PlayerController)
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * InteractTraceDistance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(InteractTrace), false, this);

	FHitResult Hit;
	GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, TraceEnd, ECC_Visibility, Params);

	SetFocusedGun(Cast<AGunBase>(Hit.GetActor()));
}

void APlayerCharacter::SetFocusedGun(AGunBase* NewFocusedGun)
{
	if (NewFocusedGun == FocusedGun)
	{
		return;
	}

	FocusedGun = NewFocusedGun;

	if (!InteractPromptWidget)
	{
		return;
	}

	if (FocusedGun)
	{
		InteractPromptWidget->SetPromptText(FocusedGun->GetPickupPromptText());
		InteractPromptWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		InteractPromptWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void APlayerCharacter::OnInteractPressed()
{
	if (FocusedGun)
	{
		EquipGun(FocusedGun);
	}
}

// -------------------------------------------------------------------------------------------------
// Weapon handling

void APlayerCharacter::EquipGun(AGunBase* NewGun)
{
	if (!NewGun || NewGun == CurrentGun)
	{
		return;
	}

	if (CurrentGun)
	{
		DropCurrentGun(NewGun->GetActorTransform());
	}

	SetFocusedGun(nullptr);

	CurrentGun = NewGun;

	// Recoil and spread read the owner, so it must be set before the gun is used
	CurrentGun->SetOwner(this);
	CurrentGun->SetInstigator(this);
	CurrentGun->SetHeld(true);

	const UWeaponDataAsset* WeaponData = CurrentGun->GetWeaponData();
	const FName AttachSocket = WeaponData ? WeaponData->AttachSocketName : FName(TEXT("WeaponSocket"));
	CurrentGun->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, AttachSocket);

	if (WeaponData)
	{
		if (WeaponData->AnimLayerClass)
		{
			GetMesh()->LinkAnimClassLayers(WeaponData->AnimLayerClass);
		}

		PlayAnimMontage(WeaponData->EquipMontage);
	}

}

void APlayerCharacter::DropCurrentGun(const FTransform& DropTransform)
{
	StopFiring();

	if (const UWeaponDataAsset* WeaponData = CurrentGun->GetWeaponData())
	{
		if (WeaponData->AnimLayerClass)
		{
			GetMesh()->UnlinkAnimClassLayers(WeaponData->AnimLayerClass);
		}
	}

	CurrentGun->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	CurrentGun->SetActorTransform(DropTransform);
	CurrentGun->SetHeld(false);
	CurrentGun->SetOwner(nullptr);
	CurrentGun->SetInstigator(nullptr);

	CurrentGun = nullptr;
}
