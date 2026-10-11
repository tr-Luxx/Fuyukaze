#pragma once

#include "CoreMinimal.h"
#include "CombatPlayerController.h"
#include "FuyuPlayerController.generated.h"

/**
 *  Player controller for Fuyukaze. Reuses the template controller's respawn handling and
 *  supplies the movement and look mappings in code, so no controller blueprint is needed.
 */
UCLASS()
class AFuyuPlayerController : public ACombatPlayerController
{
	GENERATED_BODY()

public:

	AFuyuPlayerController();

	/** In a shared game the world cannot be paused, so Esc opens a menu over it instead */
	bool IsMenuOpen() const { return bMenuOpen; }

protected:

	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;

	/** Esc (or Menu on a gamepad) pauses; while paused, Enter quits to the desktop */
	void TogglePause();
	void QuitIfPaused();

	bool bMenuOpen = false;
};
