#include "Public/Player/PlayerCharacter.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Public/Gun/GunBase.h"
#include "Public/Data/WeaponDataAsset.h"
#include "Public/UI/InteractPromptWidget.h"
#include "Blueprint/UserWidget.h"

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
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.Owner = this;
		SpawnInfo.Instigator = GetInstigator();
		AGunBase* SpawnedGun = GetWorld()->SpawnActor<AGunBase>(DefaultGun, FVector::ZeroVector, FRotator::ZeroRotator, SpawnInfo);
		EquipGun(SpawnedGun);
	}

	if (IsLocallyControlled())
	{
		if (CrosshairWidgetClass)
		{
			CrosshairWidget = CreateWidget<UUserWidget>(GetWorld(), CrosshairWidgetClass);
			if (CrosshairWidget)
			{
				CrosshairWidget->AddToViewport();
			}
		}

		if (InteractPromptWidgetClass)
		{
			InteractPromptWidget = CreateWidget<UInteractPromptWidget>(GetWorld(), InteractPromptWidgetClass);
			if (InteractPromptWidget)
			{
				InteractPromptWidget->AddToViewport();
				InteractPromptWidget->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
}

void APlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Ateş niyeti varsa sprint bastırılır → yürüme hızına in
	const bool bSprinting = bIsSprinting && !bWantsToFire;
	const float TargetSpeed = bSprinting ? SprintSpeed : WalkSpeed;
	GetCharacterMovement()->MaxWalkSpeed = FMath::FInterpTo(
		GetCharacterMovement()->MaxWalkSpeed, TargetSpeed, DeltaTime, SprintInterpSpeed);

	// Koşmuyorken (yürüme hızına indiyse) ateşe başla
	if (bWantsToFire && !bIsFiring && CurrentGun)
	{
		const bool bAtWalkSpeed = GetVelocity().Size() <= WalkSpeed + 50.f;
		if (bAtWalkSpeed)
		{
			APlayerController* PC = Cast<APlayerController>(GetController());
			CurrentGun->StartFire(PC);
			bIsFiring = true;
		}
	}

	if (IsLocallyControlled())
	{
		UpdateInteractTrace();
	}
}
void APlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EIC) return;

	if (MoveAction)
		EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Move);

	if (LookAction)
		EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &APlayerCharacter::Look);

	if (FireAction)
	{
		EIC->BindAction(FireAction, ETriggerEvent::Started, this, &APlayerCharacter::OnFirePressed);
		EIC->BindAction(FireAction, ETriggerEvent::Completed, this, &APlayerCharacter::OnFireReleased);
	}

	if (SprintAction)
	{
		EIC->BindAction(SprintAction, ETriggerEvent::Started, this, &APlayerCharacter::SprintStart);
		EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &APlayerCharacter::SprintEnd);
	}

	if (InteractAction)
	{
		EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &APlayerCharacter::OnInteractPressed);
	}
}

void APlayerCharacter::Move(const FInputActionValue& Value)
{
	
	FVector2D MovementVector = Value.Get<FVector2D>();
	
	const FRotator Rotation = Controller->GetControlRotation();
	const FRotator YawRotation(0, Rotation.Yaw, 0);
	
	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	
	AddMovementInput(ForwardDirection, MovementVector.Y);
	
	AddMovementInput(RightDirection, MovementVector.X);
}

void APlayerCharacter::Look(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (GetController())
	{
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void APlayerCharacter::OnFirePressed()
{
    bWantsToFire = true;
}

void APlayerCharacter::OnFireReleased()
{
	 bWantsToFire = false;
        if (CurrentGun && bIsFiring)
        {
            CurrentGun->StopFire();
            bIsFiring = false;
        }
}

void APlayerCharacter::SprintStart() { bIsSprinting = true; }
void APlayerCharacter::SprintEnd()   { bIsSprinting = false; }

void APlayerCharacter::UpdateInteractTrace()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	int32 SizeX, SizeY;
	PC->GetViewportSize(SizeX, SizeY);

	FVector WorldLocation, WorldDirection;
	if (!PC->DeprojectScreenPositionToWorld(SizeX * 0.5f, SizeY * 0.5f, WorldLocation, WorldDirection))
	{
		return;
	}

	const FVector TraceEnd = WorldLocation + WorldDirection * InteractTraceDistance;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);

	FHitResult Hit;
	GetWorld()->LineTraceSingleByChannel(Hit, WorldLocation, TraceEnd, ECC_Visibility, Params);

	AGunBase* HitGun = Cast<AGunBase>(Hit.GetActor());

	if (HitGun == FocusedGun) return;

	FocusedGun = HitGun;

	if (!InteractPromptWidget) return;

	if (FocusedGun)
	{
		InteractPromptWidget->SetPromptText(FocusedGun->PickupPromptText);
		InteractPromptWidget->SetVisibility(ESlateVisibility::Visible);
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

void APlayerCharacter::EquipGun(AGunBase* NewGun)
{
	if (!NewGun || NewGun == CurrentGun) return;

	const FTransform PickupTransform = NewGun->GetActorTransform();

	if (CurrentGun)
	{
		if (CurrentGun->WeaponDataAsset && CurrentGun->WeaponDataAsset->AnimLayerClass)
		{
			GetMesh()->UnlinkAnimClassLayers(CurrentGun->WeaponDataAsset->AnimLayerClass);
		}

		// Eldeki silahı, yeni silahın bulunduğu yere bırak (swap)
		CurrentGun->SetOwner(nullptr);   // bırakılan silahın sahipliğini kaldır
		CurrentGun->SetHeld(false);
		CurrentGun->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		CurrentGun->SetActorTransform(PickupTransform);
	}

	CurrentGun = NewGun;
	FocusedGun = nullptr;

	if (InteractPromptWidget)
	{
		InteractPromptWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	const FName SocketName = CurrentGun->WeaponDataAsset ? CurrentGun->WeaponDataAsset->AttachSocketName : FName("WeaponSocket");

	// KRİTİK: silahın sahibini bu karaktere ata.
	// Recoil (UpdateRecoil/ApplyRecoil) GetOwner() üzerinden çalışıyor;
	// bu satır olmadan yerden alınan silahta Owner null kalır ve recoil çalışmaz.
	CurrentGun->SetOwner(this);
	CurrentGun->SetInstigator(this);

	CurrentGun->SetHeld(true);
	CurrentGun->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, SocketName);

	if (CurrentGun->WeaponDataAsset)
	{
		if (CurrentGun->WeaponDataAsset->AnimLayerClass)
		{
			GetMesh()->LinkAnimClassLayers(CurrentGun->WeaponDataAsset->AnimLayerClass);
		}

		if (CurrentGun->WeaponDataAsset->EquipMontage)
		{
			PlayAnimMontage(CurrentGun->WeaponDataAsset->EquipMontage);
		}
	}
}