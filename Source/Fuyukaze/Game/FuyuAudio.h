#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FuyuAudio.generated.h"

class UAudioComponent;
class USoundWaveProcedural;

UENUM()
enum class EFuyuSound : uint8
{
	/** A quick cut through the air */
	Swing,
	/** A slower, weightier cut */
	HeavySwing,
	/** A blade landing on a body */
	Hit,
	/** Steel turned aside by a perfect parry */
	Clang,
	/** A blow caught on a guard */
	Guard,
	/** The chime that accompanies an enemy's warning glint */
	Glint,
	/** A drum for a camp freed or a duel won */
	Drum,
	/** Resolve spent to recover */
	Heal,
	/** The deep, ringing blow that ends a foe */
	Kill,
	/** A guard shattering */
	Break,
	/** Breath drawn in before the ultimate: a rising rush of air over a climbing tone */
	Charge,
	/** Two rising notes to call a horse */
	Whistle,
	/** A hoof striking turf */
	Hoof,
	Count
};

/**
 *  The project ships no audio assets, so every sound is synthesised when first needed and played
 *  through a small pool of procedural voices. A looping bed of wind runs underneath.
 */
UCLASS()
class UFuyuAudio : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:

	/** Plays a sound at a point in the world. Sounds are spatialised, so distant fights are quieter */
	static void Play(const UObject* WorldContext, EFuyuSound Sound, const FVector& Location, float Volume = 1.0f, float Pitch = 0.0f);

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

protected:

	void PlayInternal(EFuyuSound Sound, const FVector& Location, float Volume, float Pitch);
	UAudioComponent* MakeVoice(USoundWaveProcedural*& OutWave, bool bSpatial);
	const TArray<int16>& GetBuffer(EFuyuSound Sound);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> Voices;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundWaveProcedural>> Waves;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> WindVoice;

	UPROPERTY(Transient)
	TObjectPtr<USoundWaveProcedural> WindWave;

	TArray<int16> Buffers[(int32)EFuyuSound::Count];
	TArray<int16> WindLoop;
	int32 NextVoice = 0;
};
