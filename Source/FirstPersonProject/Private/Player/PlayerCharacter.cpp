#include "Player/PlayerCharacter.h"

#include "Components/SpotLightComponent.h"
#include "Misc/CommandLine.h"

#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Gun/GunBase.h"
#include "InputActionValue.h"
#include "Player/CameraFeelComponent.h"
#include "Player/ViewmodelComponent.h"
#include "TimerManager.h"
#include "UI/InteractPromptWidget.h"

APlayerCharacter::APlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	CameraFeel = CreateDefaultSubobject<UCameraFeelComponent>(TEXT("CameraFeel"));
	Traversal = CreateDefaultSubobject<UTraversalComponent>(TEXT("Traversal"));
	Viewmodel = CreateDefaultSubobject<UViewmodelComponent>(TEXT("Viewmodel"));

	FirstPersonArms = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonArms"));
	FirstPersonArms->SetupAttachment(GetMesh());
	FirstPersonArms->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonArms->SetCastShadow(false);
	FirstPersonArms->SetOnlyOwnerSee(true);

	FirstPersonGloves = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonGloves"));
	FirstPersonGloves->SetupAttachment(FirstPersonArms);
	FirstPersonGloves->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonGloves->SetCastShadow(false);
	FirstPersonGloves->SetOnlyOwnerSee(true);

	// Flashlight held next to the weapon: the main light in the dark
	Flashlight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Flashlight"));
	Flashlight->SetupAttachment(GetRootComponent());
	Flashlight->SetIntensityUnits(ELightUnits::Lumens);
	Flashlight->SetIntensity(650.f);
	// Only a faint beam in the fog, otherwise the haze washes the screen out
	Flashlight->SetVolumetricScatteringIntensity(0.2f);
	Flashlight->SetInnerConeAngle(14.f);
	Flashlight->SetOuterConeAngle(30.f);
	Flashlight->SetAttenuationRadius(3200.f);
	Flashlight->SetLightColor(FLinearColor(1.f, 0.93f, 0.82f));
	Flashlight->SetCastShadows(true);
	Flashlight->SetVisibility(true);

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->NavAgentProps.bCanCrouch = true;
	Movement->bCanWalkOffLedgesWhenCrouching = true;
	Movement->SetCrouchedHalfHeight(55.f);
	Movement->MaxWalkSpeedCrouched = CrouchSpeed;
	Movement->AirControl = 0.45f;
	Movement->JumpZVelocity = 560.f;
	Movement->GravityScale = 1.35f;
	Movement->BrakingDecelerationFalling = 200.f;
	Movement->MaxAcceleration = 3000.f;
	Movement->BrakingDecelerationWalking = 2400.f;
	Movement->GroundFriction = 9.f;
}

void APlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->MaxWalkSpeedCrouched = CrouchSpeed;

	SavedGroundFriction = Movement->GroundFriction;
	SavedBrakingDeceleration = Movement->BrakingDecelerationWalking;
	SavedMaxAcceleration = Movement->MaxAcceleration;

	Viewmodel->Initialize(FirstPersonArms, FirstPersonGloves, FindComponentByClass<UCameraComponent>(), GetMesh(), ArmsSetup);

	Health = MaxHealth;
	if (UCameraComponent* Camera = FindComponentByClass<UCameraComponent>())
	{
		// Slightly right of and below the eye, like a light taped to the gun, so shadows show
		Flashlight->AttachToComponent(Camera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		Flashlight->SetRelativeLocation(FVector(10.f, 18.f, -14.f));
	}
	FlashlightBaseIntensity = Flashlight->Intensity;

	Traversal->OnTraversalStarted.AddUObject(this, &APlayerCharacter::OnTraversalStarted);
	Traversal->OnTraversalEnded.AddUObject(this, &APlayerCharacter::OnTraversalEnded);

	GiveStartingWeapons();

	if (IsLocallyControlled())
	{
		CreateHUDWidgets();
		StartViewmodelCapture();
		StartFeelCapture();
		bZombieCapture = FParse::Param(FCommandLine::Get(), TEXT("ZombieCapture"));
	}
}

void APlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopFiring();
	GetWorldTimerManager().ClearAllTimersForObject(this);

	for (AGunBase* Gun : Weapons)
	{
		UnbindGunEvents(Gun);
	}

	Super::EndPlay(EndPlayReason);
}

void APlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bFeelCapture)
	{
		TickFeelCapture(DeltaTime);
	}

	if (bZombieCapture)
	{
		TickZombieCapture(DeltaTime);
	}

	UpdateHealth(DeltaTime);
	UpdateFlashlight(DeltaTime);

	if (bDead)
	{
		return;
	}

	UpdateMovement(DeltaTime);
	UpdateAiming();
	UpdateFiring();

	if (IsLocallyControlled())
	{
		UpdateInteractTrace();
		UpdateCameraFeel();
	}
}

bool APlayerCharacter::IsTraversing() const
{
	return Traversal && Traversal->IsTraversing();
}

void APlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		return;
	}

	auto BindPressRelease = [this, Input](UInputAction* Action, void (APlayerCharacter::*Pressed)(), void (APlayerCharacter::*Released)())
	{
		if (!Action)
		{
			return;
		}

		Input->BindAction(Action, ETriggerEvent::Started, this, Pressed);
		if (Released)
		{
			Input->BindAction(Action, ETriggerEvent::Completed, this, Released);
		}
	};

	if (MoveAction)
	{
		Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Move);
		Input->BindActionValueLambda(MoveAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { LastMoveInput = FVector2D::ZeroVector; });
	}

	if (LookAction)
	{
		Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Look);
	}

	if (SwitchWeaponAction)
	{
		Input->BindAction(SwitchWeaponAction, ETriggerEvent::Started, this, &APlayerCharacter::OnSwitchWeapon);
	}

	BindPressRelease(JumpAction, &APlayerCharacter::OnJumpPressed, &APlayerCharacter::OnJumpReleased);
	BindPressRelease(FireAction, &APlayerCharacter::OnFirePressed, &APlayerCharacter::OnFireReleased);
	BindPressRelease(AimAction, &APlayerCharacter::OnAimPressed, &APlayerCharacter::OnAimReleased);
	BindPressRelease(ReloadAction, &APlayerCharacter::OnReloadPressed, nullptr);
	BindPressRelease(SprintAction, &APlayerCharacter::OnSprintPressed, &APlayerCharacter::OnSprintReleased);
	BindPressRelease(CrouchAction, &APlayerCharacter::OnCrouchPressed, &APlayerCharacter::OnCrouchReleased);
	BindPressRelease(InteractAction, &APlayerCharacter::OnInteractPressed, nullptr);

	PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &APlayerCharacter::ToggleFlashlight);

	for (int32 SlotIndex = 0; SlotIndex < SelectWeaponActions.Num(); ++SlotIndex)
	{
		if (SelectWeaponActions[SlotIndex])
		{
			Input->BindAction(SelectWeaponActions[SlotIndex], ETriggerEvent::Started, this, &APlayerCharacter::OnSelectWeapon, SlotIndex);
		}
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
// Look and move input

void APlayerCharacter::Move(const FInputActionValue& Value)
{
	if (!Controller)
	{
		return;
	}

	LastMoveInput = Value.Get<FVector2D>();

	// The slide and traversal own the movement direction
	if (bIsSliding || IsTraversing())
	{
		return;
	}

	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FRotationMatrix YawMatrix(YawRotation);

	AddMovementInput(YawMatrix.GetUnitAxis(EAxis::X), LastMoveInput.Y);
	AddMovementInput(YawMatrix.GetUnitAxis(EAxis::Y), LastMoveInput.X);
}

void APlayerCharacter::Look(const FInputActionValue& Value)
{
	// Zooming in lowers the sensitivity so aiming feels the same at any FOV
	const float Sensitivity = CameraFeel ? CameraFeel->GetFOVRatio() : 1.f;
	const FVector2D Input = Value.Get<FVector2D>() * Sensitivity;

	AddControllerYawInput(Input.X);
	AddControllerPitchInput(Input.Y);
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
	for (const AGunBase* Gun : Weapons)
	{
		Params.AddIgnoredActor(Gun);
	}

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
	if (FocusedGun && !IsTraversing())
	{
		PickUpGun(FocusedGun);
	}
}
