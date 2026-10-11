#include "FuyuHUD.h"
#include "FuyuEnemy.h"
#include "FuyuGameMode.h"
#include "FuyuSamurai.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Font.h"
#include "Fonts/SlateFontInfo.h"

namespace
{
	const FLinearColor Ink(0.0f, 0.0f, 0.0f, 0.55f);
	const FLinearColor Paper(0.96f, 0.93f, 0.86f, 1.0f);
	const FLinearColor Blood(0.78f, 0.1f, 0.07f, 1.0f);
	const FLinearColor Gold(0.95f, 0.72f, 0.22f, 1.0f);

	FLinearColor Fade(const FLinearColor& C, float Alpha)
	{
		return FLinearColor(C.R, C.G, C.B, C.A * Alpha);
	}
}

void AFuyuHUD::Text(const FString& String, float X, float Y, float Size, const FLinearColor& Color, bool bCentre, const TCHAR* Typeface, int32 LetterSpacing)
{
	// canvas text needs a font asset behind the Slate font description, or it silently draws nothing
	if (!HudFont)
	{
		HudFont = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
	}

	FSlateFontInfo Font(HudFont, FMath::Max(Size * Scale, 6.0f), FName(Typeface));
	Font.LetterSpacing = LetterSpacing;

	FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(String), Font, Color);
	Item.bCentreX = bCentre;
	Item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f * Color.A), FVector2D(1.5f * Scale, 1.5f * Scale));
	Canvas->DrawItem(Item);
}

void AFuyuHUD::Rect(float X, float Y, float W, float H, const FLinearColor& Color)
{
	FCanvasTileItem Item(FVector2D(X, Y), FVector2D(W, H), Color);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void AFuyuHUD::Diamond(float X, float Y, float Radius, const FLinearColor& Color)
{
	FCanvasNGonItem Item(FVector2D(X, Y), FVector2D(Radius, Radius), 4, Color);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void AFuyuHUD::Popup(const UObject* WorldContext, const FVector& Location, const FString& String, const FLinearColor& Color, float Size)
{
	const APlayerController* PC = UGameplayStatics::GetPlayerController(WorldContext, 0);
	if (AFuyuHUD* HUD = PC ? Cast<AFuyuHUD>(PC->GetHUD()) : nullptr)
	{
		FPopup& P = HUD->Popups.AddDefaulted_GetRef();
		P.Location = Location;
		P.String = String;
		P.Color = Color;
		P.Size = Size;
		P.Born = HUD->GetWorld()->GetRealTimeSeconds();
		P.Drift = FMath::FRandRange(-46.0f, 46.0f);
	}
}

void AFuyuHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas || !PlayerOwner)
	{
		return;
	}


	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	Scale = H / 1080.0f;
	const float S = Scale;

	const AFuyuGameMode* GameMode = GetWorld()->GetAuthGameMode<AFuyuGameMode>();
	const AFuyuSamurai* Samurai = Cast<AFuyuSamurai>(PlayerOwner->GetPawn());

	const bool bHelpKeyDown = PlayerOwner->IsInputKeyDown(EKeys::F1);
	if (bHelpKeyDown && !bHelpKeyWasDown)
	{
		bShowControls = !bShowControls;
	}
	bHelpKeyWasDown = bHelpKeyDown;

	// --- title cards ---------------------------------------------------------------------------
	if (GameMode)
	{
		const float Age = GameMode->GetBannerAge();
		const float Alpha = FMath::Clamp(Age / 0.7f, 0.0f, 1.0f) * FMath::Clamp((6.5f - Age) / 1.5f, 0.0f, 1.0f);
		if (Alpha > 0.0f && !GameMode->GetBanner().IsEmpty())
		{
			const float Y = H * 0.2f;
			Rect(W * 0.5f - 330.0f * S, Y + 78.0f * S, 660.0f * S, 2.0f * S, Fade(Paper, Alpha * 0.8f));
			Text(GameMode->GetBanner().ToString(), W * 0.5f, Y, 46.0f, Fade(Paper, Alpha), true, TEXT("Light"), 400);
			Text(GameMode->GetBannerSubtitle().ToString(), W * 0.5f, Y + 94.0f * S, 19.0f, Fade(Paper, Alpha * 0.9f), true, TEXT("Italic"), 60);
		}

		// objective
		Diamond(66.0f * S, 70.0f * S, 7.0f * S, Gold);
		FString Objective = GameMode->GetObjectiveText().ToString();
		if (Samurai)
		{
			const float Metres = FVector::Dist2D(GameMode->GetObjectiveLocation(), Samurai->GetActorLocation()) / 100.0f;
			Objective += FString::Printf(TEXT("   %d m"), FMath::RoundToInt32(Metres));
		}
		Text(Objective, 88.0f * S, 56.0f * S, 18.0f, Paper);
		Text(TEXT("G  follow the wind"), 88.0f * S, 84.0f * S, 13.0f, Fade(Paper, 0.6f));
	}

	if (!Samurai)
	{
		return;
	}

	// --- vitals --------------------------------------------------------------------------------
	{
		const float X = 60.0f * S, Y = H - 104.0f * S;
		const float BarW = 380.0f * S, BarH = 12.0f * S;
		const float Health = Samurai->GetHealthFraction();

		TrailingHealth = Health > TrailingHealth ? Health : FMath::FInterpConstantTo(TrailingHealth, Health, GetWorld()->GetDeltaSeconds(), 0.35f);

		Rect(X - 2.0f * S, Y - 2.0f * S, BarW + 4.0f * S, BarH + 4.0f * S, Ink);
		Rect(X, Y, BarW * TrailingHealth, BarH, Fade(Paper, 0.75f));
		Rect(X, Y, BarW * Health, BarH, Blood);

		// resolve: one diamond per point, the next one filling as it is earned
		for (int32 i = 0; i < Samurai->GetMaxResolve(); ++i)
		{
			const float Fill = FMath::Clamp(Samurai->GetResolve() - i, 0.0f, 1.0f);
			const float CX = X + 12.0f * S + i * 30.0f * S, CY = Y - 22.0f * S;
			Diamond(CX, CY, 11.0f * S, Ink);
			Diamond(CX, CY, 8.5f * S * FMath::Max(Fill, 0.0f), Fill >= 1.0f ? Gold : Fade(Gold, 0.55f));
		}
		Text(TEXT("F  heal"), X + 14.0f * S + Samurai->GetMaxResolve() * 30.0f * S, Y - 32.0f * S, 13.0f, Fade(Paper, 0.6f));

		// stance wheel
		const EFuyuStance Stances[] = { EFuyuStance::Stone, EFuyuStance::Wind, EFuyuStance::Moon };
		const TCHAR* Foes[] = { TEXT("swords"), TEXT("spears"), TEXT("brutes") };
		float SX = X;
		for (int32 i = 0; i < 3; ++i)
		{
			const bool bActive = Samurai->GetStance() == Stances[i];
			const FString Label = FString::Printf(TEXT("%d  %s"), i + 1, FuyuCombat::StanceName(Stances[i]));
			Text(Label, SX, Y + 24.0f * S, 15.0f, bActive ? Gold : Fade(Paper, 0.45f), false, bActive ? TEXT("Bold") : TEXT("Regular"), 120);
			if (bActive)
			{
				Text(FString::Printf(TEXT("breaks %s"), Foes[i]), SX, Y + 46.0f * S, 12.0f, Fade(Paper, 0.6f));
			}
			SX += 128.0f * S;
		}
	}

	// --- enemies -------------------------------------------------------------------------------
	const AFuyuEnemy* Locked = Samurai->GetLockTarget();

	for (TActorIterator<AFuyuEnemy> It(GetWorld()); It; ++It)
	{
		const AFuyuEnemy* Enemy = *It;
		if (Enemy->IsDead() || !Enemy->IsAggressive() || FVector::Dist(Enemy->GetActorLocation(), Samurai->GetActorLocation()) > 2400.0f)
		{
			continue;
		}

		// posture sits under each fighter's floating health bar; it flashes when the guard is broken
		const FVector Screen = Canvas->Project(Enemy->GetActorLocation() + FVector(0, 0, 118.0f * Enemy->GetActorScale3D().Z));
		if (Screen.Z <= 0.0f)
		{
			continue;
		}

		const float Posture = Enemy->GetPostureFraction();
		if (Posture > 0.01f)
		{
			const float BW = 70.0f * S;
			const bool bBroken = Enemy->IsStaggered();
			const float Pulse = bBroken ? 0.6f + 0.4f * FMath::Sin(GetWorld()->GetTimeSeconds() * 16.0f) : 1.0f;
			Rect(Screen.X - BW * 0.5f - S, Screen.Y - S, BW + 2.0f * S, 5.0f * S, Ink);
			Rect(Screen.X - BW * 0.5f * Posture, Screen.Y, BW * Posture, 3.0f * S, Fade(bBroken ? Paper : Gold, Pulse));
		}
	}

	if (Locked && !Locked->IsDead())
	{
		const FVector Screen = Canvas->Project(Locked->GetActorLocation() + FVector(0, 0, 30.0f));
		if (Screen.Z > 0.0f)
		{
			Diamond(Screen.X, Screen.Y, 9.0f * S, Fade(Paper, 0.25f));
			Diamond(Screen.X, Screen.Y, 4.0f * S, Paper);
		}

		// the locked target gets a named bar at the top of the screen, as a duel deserves
		const float BW = 520.0f * S, BX = W * 0.5f - BW * 0.5f, BY = 110.0f * S;
		Text(Locked->GetDisplayName().ToString(), W * 0.5f, BY - 34.0f * S, 18.0f, Paper, true, TEXT("Regular"), 160);
		Rect(BX - 2.0f * S, BY - 2.0f * S, BW + 4.0f * S, 14.0f * S, Ink);
		Rect(BX, BY, BW * Locked->GetHealthFraction(), 10.0f * S, Blood);
		Rect(W * 0.5f - BW * 0.35f * Locked->GetPostureFraction(), BY + 16.0f * S, BW * 0.7f * Locked->GetPostureFraction(), 4.0f * S, Gold);

		const EFuyuStance Counter = FuyuCombat::CounterStance(Locked->GetEnemyType());
		if (Counter != Samurai->GetStance())
		{
			Text(FString::Printf(TEXT("%s stance breaks this guard"), FuyuCombat::StanceName(Counter)), W * 0.5f, BY + 28.0f * S, 13.0f, Fade(Gold, 0.85f), true);
		}
	}

	// --- hit feedback --------------------------------------------------------------------------
	const float RealNow = GetWorld()->GetRealTimeSeconds();

	// numbers jump out large, settle, then drift up and fade
	for (int32 i = Popups.Num() - 1; i >= 0; --i)
	{
		const FPopup& P = Popups[i];
		const float Age = RealNow - P.Born;
		if (Age > 0.9f)
		{
			Popups.RemoveAtSwap(i);
			continue;
		}

		const FVector Screen = Canvas->Project(P.Location);
		if (Screen.Z <= 0.0f)
		{
			continue;
		}

		const float T = Age / 0.9f;
		const float Pop = 1.0f + 0.9f * FMath::Square(FMath::Max(1.0f - Age / 0.14f, 0.0f));
		const float Rise = (1.0f - FMath::Square(1.0f - T)) * 70.0f * S;
		Text(P.String, Screen.X + P.Drift * T * S, Screen.Y - Rise, P.Size * Pop, Fade(P.Color, FMath::Min(1.0f, (1.0f - T) * 2.5f)), true, TEXT("Bold"));
	}

	// the hit chain: a big number that thumps on every hit and drains away as the window closes
	const int32 Chain = Samurai->GetHitChain();
	const float SinceHit = Samurai->GetTimeSinceHit();
	if (Chain >= 2 && SinceHit < AFuyuSamurai::HitChainWindow)
	{
		const float Left = 1.0f - SinceHit / AFuyuSamurai::HitChainWindow;
		const float Thump = 1.0f + 0.55f * FMath::Square(FMath::Max(1.0f - SinceHit / 0.16f, 0.0f));
		const FLinearColor Color = Chain >= 10 ? FLinearColor(1.0f, 0.32f, 0.12f) : (Chain >= 5 ? Gold : Paper);
		const float Alpha = FMath::Min(1.0f, Left * 3.0f);
		const float X = W - 190.0f * S, Y = H * 0.36f;

		Text(FString::FromInt(Chain), X, Y - 30.0f * S * (Thump - 1.0f), 64.0f * Thump, Fade(Color, Alpha), true, TEXT("Bold"));
		Text(TEXT("HITS"), X, Y + 98.0f * S, 16.0f, Fade(Color, Alpha * 0.9f), true, TEXT("Regular"), 500);

		// a draining bar shows how long there is to land the next one
		Rect(X - 50.0f * S, Y + 128.0f * S, 100.0f * S, 3.0f * S, Fade(Ink, Alpha));
		Rect(X - 50.0f * S * Left, Y + 128.0f * S, 100.0f * S * Left, 3.0f * S, Fade(Color, Alpha));

		if (Chain % 5 == 0 && SinceHit < 1.2f)
		{
			Text(TEXT("+ resolve"), X, Y + 140.0f * S, 14.0f, Fade(Gold, 1.0f - SinceHit / 1.2f), true, TEXT("Italic"));
		}
	}

	// a kill whites the frame for an instant
	const float SinceKill = Samurai->GetTimeSinceKill();
	if (SinceKill < 0.22f)
	{
		Rect(0, 0, W, H, FLinearColor(1.0f, 0.97f, 0.9f, 0.14f * FMath::Square(1.0f - SinceKill / 0.22f)));
	}

	// --- feedback ------------------------------------------------------------------------------
	const float SinceParry = Samurai->GetTimeSinceParry();
	if (SinceParry < 0.5f)
	{
		const float Alpha = 1.0f - SinceParry / 0.5f;
		Rect(0, 0, W, H, FLinearColor(1.0f, 1.0f, 1.0f, 0.22f * Alpha * Alpha));
		Text(TEXT("PARRY"), W * 0.5f, H * 0.62f, 26.0f, Fade(Paper, Alpha), true, TEXT("Light"), 600);
	}

	if (Samurai->GetHealthFraction() < 0.3f && !Samurai->IsDead())
	{
		const float Pulse = 0.5f + 0.5f * FMath::Sin(GetWorld()->GetTimeSeconds() * 5.0f);
		const float Edge = 90.0f * S;
		const FLinearColor Red(0.6f, 0.0f, 0.0f, 0.1f + 0.1f * Pulse);
		Rect(0, 0, W, Edge, Red);
		Rect(0, H - Edge, W, Edge, Red);
		Rect(0, Edge, Edge, H - 2.0f * Edge, Red);
		Rect(W - Edge, Edge, Edge, H - 2.0f * Edge, Red);
	}

	if (Samurai->IsDead())
	{
		Rect(0, 0, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
		Text(TEXT("YOU HAVE FALLEN"), W * 0.5f, H * 0.42f, 44.0f, Blood, true, TEXT("Light"), 500);
		Text(TEXT("The wind carries you back."), W * 0.5f, H * 0.42f + 84.0f * S, 18.0f, Fade(Paper, 0.85f), true, TEXT("Italic"));
	}

	if (PlayerOwner->IsPaused())
	{
		Rect(0, 0, W, H, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
		Text(TEXT("PAUSED"), W * 0.5f, H * 0.4f, 46.0f, Paper, true, TEXT("Light"), 600);
		Text(TEXT("Esc  resume          Enter  quit to desktop"), W * 0.5f, H * 0.4f + 92.0f * S, 17.0f, Fade(Paper, 0.85f), true);
	}

	// --- controls ------------------------------------------------------------------------------
	if (bShowControls)
	{
		// two columns of key and action, laid out by hand so they line up in a proportional font
		const TCHAR* Lines[][4] = {
			{ TEXT("WASD"), TEXT("move"),          TEXT("Shift"), TEXT("sprint") },
			{ TEXT("LMB"),  TEXT("quick cut"),     TEXT("RMB"),   TEXT("heavy cut (hold)") },
			{ TEXT("Q"),    TEXT("guard / parry"), TEXT("Alt"),   TEXT("dodge") },
			{ TEXT("Tab"),  TEXT("lock on"),       TEXT("1 2 3"), TEXT("stance") },
			{ TEXT("F"),    TEXT("heal"),          TEXT("G"),     TEXT("guiding wind") },
			{ TEXT("K"),    TEXT("cinema mode"),   TEXT("Esc"),   TEXT("pause") },
		};

		const float X = W - 470.0f * S;
		float Y = H - 196.0f * S;
		Rect(X - 18.0f * S, Y - 14.0f * S, 448.0f * S, 172.0f * S, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
		for (const auto& Line : Lines)
		{
			Text(Line[0], X, Y, 13.0f, Gold, false, TEXT("Bold"));
			Text(Line[1], X + 62.0f * S, Y, 13.0f, Fade(Paper, 0.85f));
			Text(Line[2], X + 212.0f * S, Y, 13.0f, Gold, false, TEXT("Bold"));
			Text(Line[3], X + 274.0f * S, Y, 13.0f, Fade(Paper, 0.85f));
			Y += 24.0f * S;
		}

		Text(TEXT("F1  hide"), W - 104.0f * S, H - 222.0f * S, 11.0f, Fade(Paper, 0.45f));
	}
	else
	{
		Text(TEXT("F1  controls"), W - 150.0f * S, H - 50.0f * S, 12.0f, Fade(Paper, 0.4f));
	}
}
