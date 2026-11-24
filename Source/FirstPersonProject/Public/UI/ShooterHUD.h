#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ShooterHUD.generated.h"

class AGunBase;
class APlayerCharacter;
class UFont;
struct FPlayerHitEvent;

/**
 *  Canvas HUD for the shooter: ammo counter, weapon slots, reload bar, low ammo warning,
 *  hit markers and floating damage numbers. Drawn in code so it needs no widget assets.
 */
UCLASS()
class FIRSTPERSONPROJECT_API AShooterHUD : public AHUD
{
	GENERATED_BODY()

public:

	virtual void DrawHUD() override;

protected:

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	FLinearColor TextColor = FLinearColor(0.95f, 0.95f, 0.95f, 1.f);

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	FLinearColor AccentColor = FLinearColor(1.f, 0.55f, 0.1f, 1.f);

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	FLinearColor HeadshotColor = FLinearColor(1.f, 0.85f, 0.2f, 1.f);

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	FLinearColor KillColor = FLinearColor(1.f, 0.2f, 0.15f, 1.f);

	UPROPERTY(EditDefaultsOnly, Category = "Crosshair")
	FLinearColor CrosshairColor = FLinearColor(1.f, 1.f, 1.f, 0.9f);

	/** Dark edge around the crosshair so it reads on bright and dark backgrounds */
	UPROPERTY(EditDefaultsOnly, Category = "Crosshair")
	FLinearColor CrosshairOutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.55f);

	UPROPERTY(EditDefaultsOnly, Category = "Crosshair", meta = (ClampMin = "0"))
	float CrosshairLineLength = 8.f;

	UPROPERTY(EditDefaultsOnly, Category = "Crosshair", meta = (ClampMin = "0"))
	float CrosshairThickness = 2.f;

	/** Smallest distance of the lines from the center */
	UPROPERTY(EditDefaultsOnly, Category = "Crosshair", meta = (ClampMin = "0"))
	float CrosshairMinGap = 7.f;

	/** How fast the crosshair opens and closes with the spread */
	UPROPERTY(EditDefaultsOnly, Category = "Crosshair")
	float CrosshairGapSpeed = 18.f;

	/** How long the controls help stays on screen */
	UPROPERTY(EditDefaultsOnly, Category = "HUD", meta = (Units = "s"))
	float ControlsHelpDuration = 12.f;

private:

	void DrawAmmo(const APlayerCharacter& Player, const AGunBase& Gun);
	void DrawWeaponSlots(const APlayerCharacter& Player);
	void DrawReloadState(const AGunBase& Gun);
	void DrawHitMarker(const APlayerCharacter& Player);
	void DrawCrosshair(const APlayerCharacter& Player, const AGunBase* Gun);

	// Survival (zombie mode)
	void DrawDamageVignette(const APlayerCharacter& Player);
	void DrawDamageDirection(const APlayerCharacter& Player);
	void DrawHealth(const APlayerCharacter& Player);
	void DrawBattery(const APlayerCharacter& Player);
	void DrawPickupMessage(const APlayerCharacter& Player);
	void DrawWaveInfo(const APlayerCharacter& Player);

	/** Rectangle with an outline, centered on X/Y */
	void DrawOutlinedRect(float CenterX, float CenterY, float Width, float Height, float Alpha);

	/** Screen pixels from the center for a cone half angle at the current field of view */
	float AngleToPixels(float HalfAngleDegrees) const;
	void DrawDamageNumbers(const APlayerCharacter& Player);
	void DrawControlsHelp();

	/** Text with a drop shadow, X/Y in screen pixels. Alignment: 0 = left, 0.5 = center, 1 = right. */
	void DrawShadowedText(const FString& Text, const FLinearColor& Color, float X, float Y, UFont* Font, float Scale, float HorizontalAlign = 0.f);

	float UIScale = 1.f;

	/** Crosshair state smoothed between frames */
	float CrosshairGap = 0.f;
	float CrosshairAlpha = 1.f;
	float LastDrawTime = 0.f;
};
