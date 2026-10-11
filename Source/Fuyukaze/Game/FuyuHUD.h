#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FuyuHUD.generated.h"

class UFont;

/**
 *  A deliberately sparse HUD drawn straight to the canvas: health and resolve in the corner,
 *  the current objective, a reticle and posture read-out for the locked target, and centre-screen
 *  title cards. Press F1 to show or hide the controls.
 */
UCLASS()
class AFuyuHUD : public AHUD
{
	GENERATED_BODY()

public:

	virtual void DrawHUD() override;

	/** Floats a short piece of text up from a point in the world, e.g. damage dealt */
	static void Popup(const UObject* WorldContext, const FVector& Location, const FString& String, const FLinearColor& Color, float Size);

protected:

	void Text(const FString& String, float X, float Y, float Size, const FLinearColor& Color, bool bCentre = false, const TCHAR* Typeface = TEXT("Regular"), int32 LetterSpacing = 0);
	void Rect(float X, float Y, float W, float H, const FLinearColor& Color);
	void Diamond(float X, float Y, float Radius, const FLinearColor& Color);

	UPROPERTY(Transient)
	TObjectPtr<UFont> HudFont;

	/** Pixels per 1080p pixel, so the layout holds at any resolution */
	float Scale = 1.0f;

	bool bShowControls = true;
	bool bHelpKeyWasDown = false;

	/** Health shown a moment ago, so lost health drains visibly instead of vanishing */
	float TrailingHealth = 1.0f;

	struct FPopup
	{
		FVector Location;
		FString String;
		FLinearColor Color;
		float Size;
		float Born;

		/** Sideways drift in pixels, so numbers from a flurry of hits fan out instead of stacking */
		float Drift;
	};
	TArray<FPopup> Popups;
};
