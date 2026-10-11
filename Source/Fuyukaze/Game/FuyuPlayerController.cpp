#include "FuyuPlayerController.h"
#include "CombatCharacter.h"
#include "InputMappingContext.h"
#include "Components/InputComponent.h"
#include "Kismet/KismetSystemLibrary.h"

AFuyuPlayerController::AFuyuPlayerController()
{
	bShowMouseCursor = false;
}

void AFuyuPlayerController::SetupInputComponent()
{
	// movement, jump and look come from the shared template mappings; combat keys are added by the samurai
	if (DefaultMappingContexts.Num() == 0)
	{
		if (UInputMappingContext* Default = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default")))
		{
			DefaultMappingContexts.Add(Default);
		}
	}

	if (MobileExcludedMappingContexts.Num() == 0)
	{
		if (UInputMappingContext* MouseLook = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook")))
		{
			MobileExcludedMappingContexts.Add(MouseLook);
		}
	}

	Super::SetupInputComponent();

	// plain key bindings rather than input actions, because these must keep working while the game is paused
	for (const FKey& Key : { EKeys::Escape, EKeys::Gamepad_Special_Right })
	{
		InputComponent->BindKey(Key, IE_Pressed, this, &AFuyuPlayerController::TogglePause).bExecuteWhenPaused = true;
	}
	for (const FKey& Key : { EKeys::Enter, EKeys::Gamepad_FaceButton_Top })
	{
		InputComponent->BindKey(Key, IE_Pressed, this, &AFuyuPlayerController::QuitIfPaused).bExecuteWhenPaused = true;
	}
}

void AFuyuPlayerController::TogglePause()
{
	SetPause(!IsPaused());
}

void AFuyuPlayerController::QuitIfPaused()
{
	if (IsPaused())
	{
		UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
	}
}

void AFuyuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// respawn as the samurai rather than the template's stock character
	if (UClass* SamuraiClass = LoadClass<ACombatCharacter>(nullptr, TEXT("/Game/Fuyukaze/Blueprints/BP_FuyuSamurai.BP_FuyuSamurai_C")))
	{
		CharacterClass = SamuraiClass;
	}

	SetInputMode(FInputModeGameOnly());
}
