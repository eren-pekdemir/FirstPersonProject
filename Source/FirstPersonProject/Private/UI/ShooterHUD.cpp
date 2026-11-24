#include "UI/ShooterHUD.h"

#include "Data/WeaponDataAsset.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Gun/GunBase.h"
#include "Player/PlayerCharacter.h"
#include "Zombie/ZombieCoordinator.h"
#include "Zombie/ZombieWaveDirector.h"

namespace ShooterHUD
{
	constexpr float ReferenceHeight = 1080.f;
	constexpr float Margin = 48.f;

	constexpr float HitMarkerDuration = 0.22f;
	constexpr float HitMarkerInner = 9.f;
	constexpr float HitMarkerOuter = 20.f;

	constexpr float DamageNumberDuration = 0.9f;
	constexpr float DamageNumberRise = 60.f;

	/** Magazine fraction under which the low ammo warning shows */
	constexpr float LowAmmoFraction = 0.25f;

	constexpr float ReloadBarWidth = 120.f;
	constexpr float ReloadBarHeight = 4.f;
	constexpr float CenterTextOffset = 70.f;

	constexpr int32 RingSegments = 40;
	/** Crosshair opacity while reloading */
	constexpr float ReloadCrosshairAlpha = 0.35f;
	constexpr float CrosshairFadeSpeed = 14.f;
}

void AShooterHUD::DrawHUD()
{
	Super::DrawHUD();

	const APlayerCharacter* Player = Cast<APlayerCharacter>(GetOwningPawn());
	if (!Canvas || !Player)
	{
		return;
	}

	UIScale = Canvas->ClipY / ShooterHUD::ReferenceHeight;

	DrawDamageVignette(*Player);
	DrawDamageDirection(*Player);
	DrawHealth(*Player);
	DrawBattery(*Player);
	DrawWaveInfo(*Player);
	DrawPickupMessage(*Player);

	if (Player->IsDead())
	{
		return;
	}

	if (const AGunBase* Gun = Player->CurrentGun)
	{
		DrawAmmo(*Player, *Gun);
		DrawReloadState(*Gun);
	}

	DrawCrosshair(*Player, Player->CurrentGun);
	DrawWeaponSlots(*Player);
	DrawHitMarker(*Player);
	DrawDamageNumbers(*Player);
	DrawControlsHelp();
}

void AShooterHUD::DrawShadowedText(const FString& Text, const FLinearColor& Color, float X, float Y, UFont* Font, float Scale, float HorizontalAlign)
{
	float Width = 0.f;
	float Height = 0.f;
	GetTextSize(Text, Width, Height, Font, Scale);

	const float DrawX = X - Width * HorizontalAlign;
	const float ShadowOffset = FMath::Max(1.f, 2.f * UIScale);

	DrawText(Text, FLinearColor(0.f, 0.f, 0.f, Color.A * 0.6f), DrawX + ShadowOffset, Y + ShadowOffset, Font, Scale);
	DrawText(Text, Color, DrawX, Y, Font, Scale);
}

void AShooterHUD::DrawAmmo(const APlayerCharacter& Player, const AGunBase& Gun)
{
	UFont* LargeFont = GEngine->GetLargeFont();
	UFont* MediumFont = GEngine->GetMediumFont();

	const float Right = Canvas->ClipX - ShooterHUD::Margin * UIScale;
	const float Bottom = Canvas->ClipY - ShooterHUD::Margin * UIScale;

	const int32 Magazine = Gun.GetAmmoInMagazine();
	const int32 MagazineSize = FMath::Max(Gun.GetMagazineSize(), 1);
	const bool bLow = static_cast<float>(Magazine) / MagazineSize <= ShooterHUD::LowAmmoFraction;

	const FString Reserve = FString::Printf(TEXT("/ %d"), Gun.GetReserveAmmo());
	DrawShadowedText(Reserve, TextColor * FLinearColor(1.f, 1.f, 1.f, 0.7f), Right, Bottom - 40.f * UIScale, MediumFont, 1.4f * UIScale, 1.f);

	float ReserveWidth = 0.f;
	float ReserveHeight = 0.f;
	GetTextSize(Reserve, ReserveWidth, ReserveHeight, MediumFont, 1.4f * UIScale);

	DrawShadowedText(FString::FromInt(Magazine), bLow ? AccentColor : TextColor,
		Right - ReserveWidth - 10.f * UIScale, Bottom - 70.f * UIScale, LargeFont, 2.6f * UIScale, 1.f);

	DrawShadowedText(Gun.GetDisplayName().ToString().ToUpper(), TextColor, Right, Bottom - 100.f * UIScale, MediumFont, 1.1f * UIScale, 1.f);
}

void AShooterHUD::DrawWeaponSlots(const APlayerCharacter& Player)
{
	UFont* SmallFont = GEngine->GetSmallFont();
	const TArray<TObjectPtr<AGunBase>>& Weapons = Player.GetWeapons();

	const float Right = Canvas->ClipX - ShooterHUD::Margin * UIScale;
	float Y = Canvas->ClipY - ShooterHUD::Margin * UIScale - 130.f * UIScale - Weapons.Num() * 22.f * UIScale;

	for (int32 Index = 0; Index < Weapons.Num(); ++Index)
	{
		const AGunBase* Gun = Weapons[Index];
		if (!Gun)
		{
			continue;
		}

		const bool bSelected = Gun == Player.CurrentGun;
		const FLinearColor Color = bSelected ? AccentColor : TextColor * FLinearColor(1.f, 1.f, 1.f, 0.5f);
		const FString Label = FString::Printf(TEXT("[%d] %s  %d"), Index + 1, *Gun->GetDisplayName().ToString(), Gun->GetAmmoInMagazine());

		DrawShadowedText(Label, Color, Right, Y, SmallFont, 1.2f * UIScale, 1.f);
		Y += 22.f * UIScale;
	}
}

void AShooterHUD::DrawReloadState(const AGunBase& Gun)
{
	UFont* SmallFont = GEngine->GetSmallFont();
	const float CenterX = Canvas->ClipX * 0.5f;
	const float Y = Canvas->ClipY * 0.5f + ShooterHUD::CenterTextOffset * UIScale;

	if (Gun.IsReloading())
	{
		const float Width = ShooterHUD::ReloadBarWidth * UIScale;
		const float Height = ShooterHUD::ReloadBarHeight * UIScale;
		const float Left = CenterX - Width * 0.5f;

		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), Left, Y, Width, Height);
		DrawRect(TextColor, Left, Y, Width * Gun.GetReloadProgress(), Height);
		DrawShadowedText(TEXT("RELOADING"), TextColor, CenterX, Y + 8.f * UIScale, SmallFont, 1.1f * UIScale, 0.5f);
		return;
	}

	const int32 Magazine = Gun.GetAmmoInMagazine();
	const float Fraction = static_cast<float>(Magazine) / FMath::Max(Gun.GetMagazineSize(), 1);

	if (Magazine == 0 && Gun.GetReserveAmmo() == 0)
	{
		DrawShadowedText(TEXT("NO AMMO"), KillColor, CenterX, Y, SmallFont, 1.2f * UIScale, 0.5f);
	}
	else if (Fraction <= ShooterHUD::LowAmmoFraction && Gun.GetReserveAmmo() > 0)
	{
		DrawShadowedText(TEXT("[R] RELOAD"), AccentColor, CenterX, Y, SmallFont, 1.2f * UIScale, 0.5f);
	}
}

float AShooterHUD::AngleToPixels(float HalfAngleDegrees) const
{
	const APlayerController* PlayerController = GetOwningPlayerController();
	const float FOV = (PlayerController && PlayerController->PlayerCameraManager) ? PlayerController->PlayerCameraManager->GetFOVAngle() : 90.f;
	const float HalfFOV = FMath::DegreesToRadians(FMath::Clamp(FOV, 10.f, 170.f) * 0.5f);
	const float HalfAngle = FMath::DegreesToRadians(FMath::Clamp(HalfAngleDegrees, 0.f, 80.f));
	return FMath::Tan(HalfAngle) / FMath::Tan(HalfFOV) * Canvas->ClipX * 0.5f;
}

void AShooterHUD::DrawOutlinedRect(float CenterX, float CenterY, float Width, float Height, float Alpha)
{
	const float Edge = FMath::Max(1.f, UIScale);
	FLinearColor Outline = CrosshairOutlineColor;
	Outline.A *= Alpha;
	FLinearColor Fill = CrosshairColor;
	Fill.A *= Alpha;

	DrawRect(Outline, CenterX - Width * 0.5f - Edge, CenterY - Height * 0.5f - Edge, Width + Edge * 2.f, Height + Edge * 2.f);
	DrawRect(Fill, CenterX - Width * 0.5f, CenterY - Height * 0.5f, Width, Height);
}

void AShooterHUD::DrawCrosshair(const APlayerCharacter& Player, const AGunBase* Gun)
{
	const float Now = GetWorld()->GetTimeSeconds();
	const float DeltaTime = FMath::Clamp(Now - LastDrawTime, 0.f, 0.1f);
	LastDrawTime = Now;

	// Sights replace the crosshair, and a sprinting weapon is not pointed anywhere
	const bool bCrosshairHidden = !Gun || Player.IsAiming() || Player.IsSprinting();
	const float TargetAlpha = bCrosshairHidden ? 0.f : (Gun->IsReloading() ? ShooterHUD::ReloadCrosshairAlpha : 1.f);
	CrosshairAlpha = FMath::FInterpTo(CrosshairAlpha, TargetAlpha, DeltaTime, ShooterHUD::CrosshairFadeSpeed);
	if (CrosshairAlpha <= 0.01f || !Gun)
	{
		return;
	}

	const UWeaponDataAsset* WeaponData = Gun->GetWeaponData();
	const bool bPellets = WeaponData && WeaponData->PelletsPerShot > 1;

	// The gap shows the real cone the shots leave in
	const float SpreadAngle = WeaponData ? Gun->GetCurrentSpread() * WeaponData->SpreadToAngle + (bPellets ? WeaponData->PelletConeAngle : 0.f) : 0.f;
	const float TargetGap = FMath::Max(CrosshairMinGap * UIScale, AngleToPixels(SpreadAngle));
	CrosshairGap = CrosshairGap <= 0.f ? TargetGap : FMath::FInterpTo(CrosshairGap, TargetGap, DeltaTime, CrosshairGapSpeed);

	const float CenterX = FMath::RoundToFloat(Canvas->ClipX * 0.5f);
	const float CenterY = FMath::RoundToFloat(Canvas->ClipY * 0.5f);
	const float Thickness = FMath::Max(1.f, FMath::RoundToFloat(CrosshairThickness * UIScale));
	const float Length = CrosshairLineLength * UIScale;

	// Center dot
	DrawOutlinedRect(CenterX, CenterY, Thickness, Thickness, CrosshairAlpha);

	if (bPellets)
	{
		// Pellet weapons: a thin ring around the pattern with four short ticks on it
		FLinearColor Outline = CrosshairOutlineColor;
		Outline.A *= CrosshairAlpha;
		FLinearColor Fill = CrosshairColor;
		Fill.A *= CrosshairAlpha * 0.8f;

		const float RingThickness = FMath::Max(1.f, Thickness * 0.75f);
		for (int32 Segment = 0; Segment < ShooterHUD::RingSegments; ++Segment)
		{
			const float A0 = UE_TWO_PI * Segment / ShooterHUD::RingSegments;
			const float A1 = UE_TWO_PI * (Segment + 1) / ShooterHUD::RingSegments;
			const FVector2D P0(CenterX + FMath::Cos(A0) * CrosshairGap, CenterY + FMath::Sin(A0) * CrosshairGap);
			const FVector2D P1(CenterX + FMath::Cos(A1) * CrosshairGap, CenterY + FMath::Sin(A1) * CrosshairGap);
			DrawLine(P0.X, P0.Y, P1.X, P1.Y, Outline, RingThickness + 2.f * FMath::Max(1.f, UIScale));
			DrawLine(P0.X, P0.Y, P1.X, P1.Y, Fill, RingThickness);
		}

		const float TickLength = Length * 0.6f;
		const float TickCenter = CrosshairGap + TickLength * 0.5f;
		DrawOutlinedRect(CenterX - TickCenter, CenterY, TickLength, Thickness, CrosshairAlpha);
		DrawOutlinedRect(CenterX + TickCenter, CenterY, TickLength, Thickness, CrosshairAlpha);
		DrawOutlinedRect(CenterX, CenterY - TickCenter, Thickness, TickLength, CrosshairAlpha);
		DrawOutlinedRect(CenterX, CenterY + TickCenter, Thickness, TickLength, CrosshairAlpha);
		return;
	}

	// Four lines that open with the spread
	const float LineCenter = CrosshairGap + Length * 0.5f;
	DrawOutlinedRect(CenterX - LineCenter, CenterY, Length, Thickness, CrosshairAlpha);
	DrawOutlinedRect(CenterX + LineCenter, CenterY, Length, Thickness, CrosshairAlpha);
	DrawOutlinedRect(CenterX, CenterY - LineCenter, Thickness, Length, CrosshairAlpha);
	DrawOutlinedRect(CenterX, CenterY + LineCenter, Thickness, Length, CrosshairAlpha);
}

void AShooterHUD::DrawHitMarker(const APlayerCharacter& Player)
{
	const TArray<FPlayerHitEvent>& Hits = Player.GetRecentHits();
	if (Hits.IsEmpty())
	{
		return;
	}

	const FPlayerHitEvent& LastHit = Hits.Last();
	const float Age = GetWorld()->GetTimeSeconds() - LastHit.Time;
	if (Age > ShooterHUD::HitMarkerDuration)
	{
		return;
	}

	const float Alpha = 1.f - Age / ShooterHUD::HitMarkerDuration;
	FLinearColor Color = LastHit.bKilled ? KillColor : (LastHit.bHeadshot ? HeadshotColor : TextColor);
	Color.A = Alpha;

	// Marker pops out slightly as it fades
	const float Pop = 1.f + (1.f - Alpha) * 0.3f;
	const float Inner = ShooterHUD::HitMarkerInner * UIScale * Pop;
	const float Outer = ShooterHUD::HitMarkerOuter * UIScale * Pop * (LastHit.bKilled ? 1.3f : 1.f);
	const float Thickness = (LastHit.bKilled ? 3.f : 2.f) * UIScale;

	const FVector2D Center(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	for (const FVector2D Diagonal : { FVector2D(1.f, 1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, -1.f) })
	{
		const FVector2D Direction = Diagonal.GetSafeNormal();
		const FVector2D Start = Center + Direction * Inner;
		const FVector2D End = Center + Direction * Outer;
		DrawLine(Start.X, Start.Y, End.X, End.Y, Color, Thickness);
	}
}

void AShooterHUD::DrawDamageNumbers(const APlayerCharacter& Player)
{
	UFont* MediumFont = GEngine->GetMediumFont();
	const float Now = GetWorld()->GetTimeSeconds();

	for (const FPlayerHitEvent& Hit : Player.GetRecentHits())
	{
		const float Age = Now - Hit.Time;
		if (Age > ShooterHUD::DamageNumberDuration)
		{
			continue;
		}

		const FVector ScreenLocation = Project(Hit.Location, true);
		if (ScreenLocation.Z <= 0.f)
		{
			continue;
		}

		const float Progress = Age / ShooterHUD::DamageNumberDuration;
		FLinearColor Color = Hit.bKilled ? KillColor : (Hit.bHeadshot ? HeadshotColor : TextColor);
		Color.A = 1.f - Progress * Progress;

		const float Rise = ShooterHUD::DamageNumberRise * UIScale * FMath::InterpEaseOut(0.f, 1.f, Progress, 2.f);
		const float Scale = (Hit.bHeadshot ? 1.6f : 1.3f) * UIScale;

		DrawShadowedText(FString::FromInt(FMath::RoundToInt(Hit.Damage)), Color, ScreenLocation.X, ScreenLocation.Y - Rise, MediumFont, Scale, 0.5f);
	}
}

void AShooterHUD::DrawControlsHelp()
{
	const float Time = GetWorld()->GetTimeSeconds();
	if (Time > ControlsHelpDuration)
	{
		return;
	}

	const float Alpha = FMath::Clamp(ControlsHelpDuration - Time, 0.f, 1.f) * 0.8f;
	UFont* SmallFont = GEngine->GetSmallFont();

	static const TCHAR* Lines[] =
	{
		TEXT("WASD move   SHIFT sprint   SPACE jump / mantle / vault"),
		TEXT("CTRL or C crouch (while sprinting: slide)"),
		TEXT("LMB fire   RMB aim   R reload   E pick up   F flashlight"),
		TEXT("1 / 2 / 3 or mouse wheel: switch weapon"),
	};

	float Y = ShooterHUD::Margin * UIScale;
	for (const TCHAR* Line : Lines)
	{
		DrawShadowedText(Line, FLinearColor(1.f, 1.f, 1.f, Alpha), ShooterHUD::Margin * UIScale, Y, SmallFont, 1.15f * UIScale);
		Y += 22.f * UIScale;
	}
}

// -------------------------------------------------------------------------------------------------
// Survival

namespace SurvivalHUD
{
	constexpr int32 VignetteSteps = 14;
	constexpr float HealthBarWidth = 260.f;
	constexpr float HealthBarHeight = 6.f;
	constexpr float DirectionRadius = 150.f;
	constexpr float DirectionShowTime = 1.2f;
	constexpr float BatteryBarWidth = 120.f;
	constexpr float BatteryBarHeight = 4.f;
	constexpr float PickupMessageTime = 1.8f;
	constexpr float MaxCountdown = 3600.f;
}

void AShooterHUD::DrawDamageVignette(const APlayerCharacter& Player)
{
	// Red creeping in from the edges: stronger the lower the health, pulsing right after a hit
	const float Missing = 1.f - Player.GetHealthPercent();
	const float Pulse = Player.GetDamageFlash();
	float Strength = FMath::Clamp(Missing * Missing * 0.9f + Pulse * 0.45f, 0.f, 0.95f);
	if (Player.IsDead())
	{
		Strength = 0.9f;
	}
	if (Strength <= 0.01f)
	{
		return;
	}

	// Heartbeat throb when low
	if (Missing > 0.6f)
	{
		Strength *= 0.85f + 0.15f * FMath::Sin(GetWorld()->GetTimeSeconds() * 7.f);
	}

	const float Width = Canvas->ClipX;
	const float Height = Canvas->ClipY;
	const float Depth = Height * (0.18f + 0.22f * Strength);
	const float Step = Depth / SurvivalHUD::VignetteSteps;

	for (int32 Index = 0; Index < SurvivalHUD::VignetteSteps; ++Index)
	{
		const float Falloff = 1.f - static_cast<float>(Index) / SurvivalHUD::VignetteSteps;
		const FLinearColor Color(0.35f, 0.f, 0.f, Strength * Falloff * Falloff * 0.35f);
		const float Inset = Index * Step;
		DrawRect(Color, 0.f, Inset, Width, Step);								// top
		DrawRect(Color, 0.f, Height - Inset - Step, Width, Step);				// bottom
		DrawRect(Color, Inset, 0.f, Step, Height);								// left
		DrawRect(Color, Width - Inset - Step, 0.f, Step, Height);				// right
	}

	if (Player.IsDead())
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, Width, Height);
	}
}

void AShooterHUD::DrawDamageDirection(const APlayerCharacter& Player)
{
	const float Age = Player.GetTimeSinceDamaged();
	if (Age > SurvivalHUD::DirectionShowTime || Player.IsDead())
	{
		return;
	}

	// An arc pointing at whoever hit us
	const float Alpha = 1.f - Age / SurvivalHUD::DirectionShowTime;
	const float Yaw = FMath::DegreesToRadians(Player.GetDamageDirectionYaw());
	const FVector2D Center(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	const float Radius = SurvivalHUD::DirectionRadius * UIScale;
	const FLinearColor Color(0.9f, 0.08f, 0.05f, Alpha * 0.85f);

	constexpr int32 Segments = 8;
	constexpr float HalfArc = 0.35f;
	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		const float A0 = Yaw - HalfArc + 2.f * HalfArc * Segment / Segments;
		const float A1 = Yaw - HalfArc + 2.f * HalfArc * (Segment + 1) / Segments;
		// Screen up is forward: x = sin, y = -cos
		const FVector2D P0 = Center + FVector2D(FMath::Sin(A0), -FMath::Cos(A0)) * Radius;
		const FVector2D P1 = Center + FVector2D(FMath::Sin(A1), -FMath::Cos(A1)) * Radius;
		DrawLine(P0.X, P0.Y, P1.X, P1.Y, Color, 5.f * UIScale);
	}
}

void AShooterHUD::DrawHealth(const APlayerCharacter& Player)
{
	const float Left = ShooterHUD::Margin * UIScale;
	const float Bottom = Canvas->ClipY - ShooterHUD::Margin * UIScale;
	const float Width = SurvivalHUD::HealthBarWidth * UIScale;
	const float Height = SurvivalHUD::HealthBarHeight * UIScale;
	const float Percent = Player.GetHealthPercent();

	const FLinearColor BarColor = FLinearColor::LerpUsingHSV(FLinearColor(0.85f, 0.08f, 0.05f), FLinearColor(0.92f, 0.92f, 0.9f), FMath::Clamp(Percent * 1.6f - 0.2f, 0.f, 1.f));

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), Left - UIScale, Bottom - Height - UIScale, Width + 2.f * UIScale, Height + 2.f * UIScale);
	DrawRect(BarColor, Left, Bottom - Height, Width * Percent, Height);

	DrawShadowedText(FString::Printf(TEXT("%d"), FMath::CeilToInt(Percent * 100.f)), BarColor,
		Left, Bottom - Height - 30.f * UIScale, GEngine->GetMediumFont(), 1.4f * UIScale);
}

void AShooterHUD::DrawWaveInfo(const APlayerCharacter& Player)
{
	const UZombieCoordinator* Coordinator = GetWorld()->GetSubsystem<UZombieCoordinator>();
	const AZombieWaveDirector* Director = Coordinator ? Coordinator->GetDirector() : nullptr;
	if (!Director)
	{
		return;
	}

	UFont* LargeFont = GEngine->GetLargeFont();
	UFont* MediumFont = GEngine->GetMediumFont();
	const float CenterX = Canvas->ClipX * 0.5f;
	const float Top = ShooterHUD::Margin * UIScale;
	const FLinearColor Blood(0.8f, 0.07f, 0.05f, 1.f);

	if (Player.IsDead())
	{
		DrawShadowedText(TEXT("YOU DIED"), Blood, CenterX, Canvas->ClipY * 0.38f, LargeFont, 4.f * UIScale, 0.5f);
		DrawShadowedText(FString::Printf(TEXT("Survived %d waves    Kills %d    Headshots %d"), FMath::Max(Director->GetWave() - 1, 0), Director->GetKills(), Director->GetHeadshots()),
			TextColor, CenterX, Canvas->ClipY * 0.38f + 90.f * UIScale, MediumFont, 1.5f * UIScale, 0.5f);
		return;
	}

	switch (Director->GetPhase())
	{
	case EWavePhase::Prepare:
	case EWavePhase::Intermission:
	{
		const int32 NextWave = Director->GetWave() + 1;
		const float TimeLeft = Director->GetPhaseTimeLeft();
		// The test setups never start a wave
		if (TimeLeft > SurvivalHUD::MaxCountdown)
		{
			break;
		}
		const FString Title = Director->GetPhase() == EWavePhase::Prepare ? TEXT("THEY ARE COMING") : FString::Printf(TEXT("WAVE %d SURVIVED"), Director->GetWave());
		DrawShadowedText(Title, Blood, CenterX, Canvas->ClipY * 0.22f, LargeFont, 2.6f * UIScale, 0.5f);
		DrawShadowedText(FString::Printf(TEXT("Wave %d in %d"), NextWave, FMath::CeilToInt(TimeLeft)), TextColor,
			CenterX, Canvas->ClipY * 0.22f + 60.f * UIScale, MediumFont, 1.4f * UIScale, 0.5f);
		break;
	}

	case EWavePhase::Active:
		DrawShadowedText(FString::Printf(TEXT("WAVE %d"), Director->GetWave()), Blood, CenterX, Top, LargeFont, 2.f * UIScale, 0.5f);
		DrawShadowedText(FString::Printf(TEXT("%d infected left"), Director->GetZombiesRemaining()), TextColor * FLinearColor(1.f, 1.f, 1.f, 0.8f),
			CenterX, Top + 44.f * UIScale, MediumFont, 1.1f * UIScale, 0.5f);
		break;

	default:
		break;
	}

	DrawShadowedText(FString::Printf(TEXT("KILLS %d"), Director->GetKills()), TextColor, ShooterHUD::Margin * UIScale,
		Canvas->ClipY - ShooterHUD::Margin * UIScale - 80.f * UIScale, GEngine->GetSmallFont(), 1.3f * UIScale);
}

void AShooterHUD::DrawBattery(const APlayerCharacter& Player)
{
	if (Player.IsDead())
	{
		return;
	}

	// Small bar under the health bar: flashlight charge, blinking red when almost empty
	const float Left = ShooterHUD::Margin * UIScale;
	const float Top = Canvas->ClipY - ShooterHUD::Margin * UIScale + 8.f * UIScale;
	const float Width = SurvivalHUD::BatteryBarWidth * UIScale;
	const float Height = SurvivalHUD::BatteryBarHeight * UIScale;
	const float Charge = Player.GetBatteryPercent();

	const bool bLow = Charge < 0.2f;
	const float Blink = bLow ? 0.5f + 0.5f * FMath::Sin(GetWorld()->GetRealTimeSeconds() * 8.f) : 1.f;
	FLinearColor Color = bLow ? FLinearColor(0.9f, 0.15f, 0.08f) : FLinearColor(0.35f, 0.7f, 1.f);
	Color.A = (Player.IsFlashlightOn() ? 0.95f : 0.55f) * Blink;

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), Left - UIScale, Top - UIScale, Width + 2.f * UIScale, Height + 2.f * UIScale);
	DrawRect(Color, Left, Top, Width * Charge, Height);
	DrawShadowedText(TEXT("LIGHT"), Color, Left + Width + 8.f * UIScale, Top - 6.f * UIScale, GEngine->GetSmallFont(), 0.9f * UIScale);
}

void AShooterHUD::DrawPickupMessage(const APlayerCharacter& Player)
{
	const float Age = Player.GetTimeSincePickupMessage();
	if (Player.IsDead() || Player.GetPickupMessage().IsEmpty() || Age > SurvivalHUD::PickupMessageTime)
	{
		return;
	}

	// Rises a little and fades out
	FLinearColor Color = Player.GetPickupMessageColor();
	Color.A = FMath::Clamp((SurvivalHUD::PickupMessageTime - Age) / 0.5f, 0.f, 1.f);
	const float Y = Canvas->ClipY * 0.62f - Age * 18.f * UIScale;
	DrawShadowedText(Player.GetPickupMessage(), Color, Canvas->ClipX * 0.5f, Y, GEngine->GetMediumFont(), 1.3f * UIScale, 0.5f);
}
