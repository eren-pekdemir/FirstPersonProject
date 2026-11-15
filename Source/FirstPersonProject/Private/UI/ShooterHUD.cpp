#include "UI/ShooterHUD.h"

#include "Data/WeaponDataAsset.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Gun/GunBase.h"
#include "Player/PlayerCharacter.h"

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

	if (const AGunBase* Gun = Player->CurrentGun)
	{
		DrawAmmo(*Player, *Gun);
		DrawReloadState(*Gun);
	}

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
		TEXT("LMB fire   RMB aim   R reload   E pick up"),
		TEXT("1 / 2 / 3 or mouse wheel: switch weapon"),
	};

	float Y = ShooterHUD::Margin * UIScale;
	for (const TCHAR* Line : Lines)
	{
		DrawShadowedText(Line, FLinearColor(1.f, 1.f, 1.f, Alpha), ShooterHUD::Margin * UIScale, Y, SmallFont, 1.15f * UIScale);
		Y += 22.f * UIScale;
	}
}
