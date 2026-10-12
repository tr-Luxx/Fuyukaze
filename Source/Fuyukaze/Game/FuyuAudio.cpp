#include "FuyuAudio.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Sound/SoundWaveProcedural.h"

namespace
{
	constexpr int32 SampleRate = 44100;
	constexpr int32 VoiceCount = 8;

	/** Collects floating-point samples and converts them to 16-bit when done */
	struct FSynth
	{
		TArray<float> Samples;
		FRandomStream Rng;

		FSynth(float Seconds, int32 Seed)
			: Rng(Seed)
		{
			Samples.SetNumZeroed(FMath::CeilToInt32(Seconds * SampleRate));
		}

		float Noise()
		{
			return Rng.FRandRange(-1.0f, 1.0f);
		}

		/** Adds a decaying sine partial, the building block of the struck-metal sounds */
		void Partial(float Frequency, float Amplitude, float DecaySeconds, float StartSeconds = 0.0f, float Glide = 0.0f)
		{
			const int32 Start = FMath::RoundToInt32(StartSeconds * SampleRate);
			double Phase = 0.0;
			for (int32 i = Start; i < Samples.Num(); ++i)
			{
				const float T = (float)(i - Start) / SampleRate;
				const float F = Frequency * (1.0f + Glide * T);
				Phase += 2.0 * PI * F / SampleRate;
				Samples[i] += Amplitude * FMath::Exp(-T / DecaySeconds) * FMath::Sin(Phase);
			}
		}

		/** Adds noise through a resonant band-pass whose centre moves from StartHz to EndHz, shaped by a hump envelope */
		void Whoosh(float StartHz, float EndHz, float Amplitude, float Resonance)
		{
			float Low = 0.0f, Band = 0.0f;
			for (int32 i = 0; i < Samples.Num(); ++i)
			{
				const float T = (float)i / Samples.Num();
				const float Cutoff = FMath::Lerp(StartHz, EndHz, T);
				const float F = 2.0f * FMath::Sin(PI * Cutoff / SampleRate);
				const float High = Noise() - Low - Resonance * Band;
				Band += F * High;
				Low += F * Band;
				Samples[i] += Amplitude * Band * FMath::Pow(FMath::Sin(PI * T), 1.6f);
			}
		}

		/** Adds a burst of low-passed noise that dies away, for impacts */
		void Thump(float CutoffHz, float Amplitude, float DecaySeconds)
		{
			const float A = FMath::Exp(-2.0f * PI * CutoffHz / SampleRate);
			float State = 0.0f;
			for (int32 i = 0; i < Samples.Num(); ++i)
			{
				const float T = (float)i / SampleRate;
				State = A * State + (1.0f - A) * Noise();
				Samples[i] += Amplitude * State * FMath::Exp(-T / DecaySeconds);
			}
		}

		TArray<int16> Finish(float Gain = 1.0f)
		{
			// a few milliseconds of fade at each end removes clicks
			const int32 Fade = FMath::Min(220, Samples.Num() / 2);
			TArray<int16> Out;
			Out.SetNumUninitialized(Samples.Num());
			for (int32 i = 0; i < Samples.Num(); ++i)
			{
				const float Edge = FMath::Min(1.0f, FMath::Min((float)i, (float)(Samples.Num() - 1 - i)) / Fade);
				Out[i] = (int16)FMath::Clamp(Samples[i] * Gain * Edge * 32767.0f, -32767.0f, 32767.0f);
			}
			return Out;
		}
	};

	TArray<int16> Synthesise(EFuyuSound Sound)
	{
		switch (Sound)
		{
		case EFuyuSound::Swing:
		{
			FSynth S(0.2f, 11);
			S.Whoosh(700.0f, 2600.0f, 0.9f, 0.5f);
			return S.Finish(0.5f);
		}
		case EFuyuSound::HeavySwing:
		{
			FSynth S(0.36f, 12);
			S.Whoosh(350.0f, 1700.0f, 1.0f, 0.4f);
			return S.Finish(0.6f);
		}
		case EFuyuSound::Hit:
		{
			FSynth S(0.26f, 13);
			S.Thump(900.0f, 2.4f, 0.045f);
			S.Partial(95.0f, 0.8f, 0.07f, 0.0f, -2.0f);
			return S.Finish(0.75f);
		}
		case EFuyuSound::Clang:
		{
			// inharmonic partials ring like struck steel
			FSynth S(0.9f, 14);
			S.Thump(6000.0f, 1.2f, 0.004f);
			S.Partial(1870.0f, 0.34f, 0.22f);
			S.Partial(2790.0f, 0.3f, 0.3f);
			S.Partial(4130.0f, 0.24f, 0.24f);
			S.Partial(5320.0f, 0.16f, 0.16f);
			S.Partial(6870.0f, 0.1f, 0.1f);
			return S.Finish(0.7f);
		}
		case EFuyuSound::Guard:
		{
			FSynth S(0.22f, 15);
			S.Thump(3200.0f, 1.3f, 0.01f);
			S.Partial(980.0f, 0.3f, 0.05f);
			S.Partial(1540.0f, 0.24f, 0.04f);
			S.Partial(2350.0f, 0.16f, 0.03f);
			return S.Finish(0.7f);
		}
		case EFuyuSound::Glint:
		{
			FSynth S(0.4f, 16);
			S.Partial(3140.0f, 0.3f, 0.09f);
			S.Partial(4710.0f, 0.18f, 0.07f);
			return S.Finish(0.45f);
		}
		case EFuyuSound::Drum:
		{
			// two beats of a big drum: a falling low tone under a soft skin slap
			FSynth S(1.7f, 17);
			for (float Start : { 0.0f, 0.55f })
			{
				S.Partial(78.0f, 0.95f, 0.3f, Start, -0.5f);
				S.Partial(156.0f, 0.3f, 0.12f, Start, -0.5f);
			}
			S.Thump(500.0f, 1.5f, 0.03f);
			return S.Finish(0.8f);
		}
		case EFuyuSound::Kill:
		{
			// a body blow with a long steel shimmer over a sub-bass drop
			FSynth S(1.1f, 19);
			S.Thump(1400.0f, 2.6f, 0.05f);
			S.Partial(62.0f, 1.0f, 0.22f, 0.0f, -1.2f);
			S.Partial(2210.0f, 0.2f, 0.4f);
			S.Partial(3330.0f, 0.16f, 0.5f);
			S.Partial(4980.0f, 0.1f, 0.35f);
			return S.Finish(0.8f);
		}
		case EFuyuSound::Break:
		{
			// a crack and a scatter of high partials, like something giving way
			FSynth S(0.7f, 20);
			S.Thump(5000.0f, 2.2f, 0.012f);
			S.Thump(700.0f, 1.6f, 0.06f);
			S.Partial(1320.0f, 0.3f, 0.12f);
			S.Partial(2470.0f, 0.26f, 0.2f, 0.03f);
			S.Partial(3890.0f, 0.2f, 0.26f, 0.06f);
			S.Partial(5540.0f, 0.14f, 0.2f, 0.09f);
			return S.Finish(0.8f);
		}
		case EFuyuSound::Charge:
		{
			FSynth S(0.9f, 21);
			S.Whoosh(180.0f, 4200.0f, 1.0f, 0.35f);
			S.Partial(110.0f, 0.5f, 0.6f, 0.0f, 2.2f);
			S.Partial(220.0f, 0.25f, 0.5f, 0.1f, 2.2f);
			return S.Finish(0.7f);
		}
		case EFuyuSound::Whistle:
		{
			FSynth S(0.75f, 22);
			S.Partial(1750.0f, 0.5f, 0.2f, 0.0f, 1.3f);
			S.Partial(2350.0f, 0.5f, 0.32f, 0.2f, 0.9f);
			S.Partial(4700.0f, 0.08f, 0.3f, 0.2f, 0.9f);
			return S.Finish(0.6f);
		}
		case EFuyuSound::Hoof:
		{
			FSynth S(0.22f, 23);
			S.Thump(240.0f, 2.4f, 0.05f);
			S.Thump(1400.0f, 0.5f, 0.012f);
			return S.Finish(0.9f);
		}
		default:
		{
			// a rising pair of soft tones
			FSynth S(0.8f, 18);
			S.Partial(523.0f, 0.25f, 0.3f, 0.0f, 0.5f);
			S.Partial(784.0f, 0.2f, 0.3f, 0.12f, 0.5f);
			S.Partial(1046.0f, 0.12f, 0.25f, 0.24f, 0.4f);
			return S.Finish(0.5f);
		}
		}
	}

	/** Eight seconds of wind that loops seamlessly: low-passed noise with slow swells whose periods divide the loop */
	TArray<int16> SynthesiseWind()
	{
		const float Seconds = 8.0f;
		const int32 N = FMath::RoundToInt32(Seconds * SampleRate);
		const int32 Overlap = SampleRate;

		FRandomStream Rng(99);
		TArray<float> Raw;
		Raw.SetNumUninitialized(N + Overlap);

		float Slow = 0.0f, Mid = 0.0f;
		for (int32 i = 0; i < Raw.Num(); ++i)
		{
			const float White = Rng.FRandRange(-1.0f, 1.0f);
			Slow = 0.994f * Slow + 0.006f * White;
			Mid = 0.93f * Mid + 0.07f * White;
			Raw[i] = Slow * 9.0f + Mid * 0.5f;
		}

		TArray<int16> Out;
		Out.SetNumUninitialized(N);
		for (int32 i = 0; i < N; ++i)
		{
			// cross-fade the tail into the head so the seam cannot be heard
			float V = Raw[i];
			if (i < Overlap)
			{
				const float A = (float)i / Overlap;
				V = Raw[i] * A + Raw[i + N] * (1.0f - A);
			}

			const float T = (float)i / N;
			const float Swell = 0.55f + 0.3f * FMath::Sin(2.0f * PI * T) + 0.15f * FMath::Sin(2.0f * PI * 3.0f * T + 1.3f);
			Out[i] = (int16)FMath::Clamp(V * Swell * 0.42f * 32767.0f, -32767.0f, 32767.0f);
		}
		return Out;
	}
}

TStatId UFuyuAudio::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFuyuAudio, STATGROUP_Tickables);
}

bool UFuyuAudio::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFuyuAudio::Play(const UObject* WorldContext, EFuyuSound Sound, const FVector& Location, float Volume, float Pitch)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull))
	{
		if (UFuyuAudio* Audio = World->GetSubsystem<UFuyuAudio>())
		{
			Audio->PlayInternal(Sound, Location, Volume, Pitch);
		}
	}
}

UAudioComponent* UFuyuAudio::MakeVoice(USoundWaveProcedural*& OutWave, bool bSpatial)
{
	OutWave = NewObject<USoundWaveProcedural>(this);
	OutWave->SetSampleRate(SampleRate);
	OutWave->NumChannels = 1;
	OutWave->Duration = INDEFINITELY_LOOPING_DURATION;
	OutWave->SoundGroup = SOUNDGROUP_Default;
	OutWave->bLooping = false;

	UAudioComponent* Voice = NewObject<UAudioComponent>(GetWorld()->GetWorldSettings());
	Voice->bAutoDestroy = false;
	Voice->bAutoActivate = false;
	Voice->bAllowSpatialization = bSpatial;
	Voice->SetSound(OutWave);

	if (bSpatial)
	{
		Voice->bOverrideAttenuation = true;
		Voice->AttenuationOverrides.bAttenuate = true;
		Voice->AttenuationOverrides.bSpatialize = true;
		Voice->AttenuationOverrides.AttenuationShapeExtents = FVector(500.0f);
		Voice->AttenuationOverrides.FalloffDistance = 4500.0f;
	}

	Voice->RegisterComponentWithWorld(GetWorld());
	return Voice;
}

const TArray<int16>& UFuyuAudio::GetBuffer(EFuyuSound Sound)
{
	TArray<int16>& Buffer = Buffers[(int32)Sound];
	if (Buffer.Num() == 0)
	{
		Buffer = Synthesise(Sound);
	}
	return Buffer;
}

void UFuyuAudio::PlayInternal(EFuyuSound Sound, const FVector& Location, float Volume, float Pitch)
{
	UWorld* World = GetWorld();
	if (!World || !World->GetAudioDeviceRaw() || Sound == EFuyuSound::Count)
	{
		return;
	}

	if (Voices.Num() == 0)
	{
		for (int32 i = 0; i < VoiceCount; ++i)
		{
			USoundWaveProcedural* Wave = nullptr;
			Voices.Add(MakeVoice(Wave, true));
			Waves.Add(Wave);
		}
	}

	// take the next voice in the ring; with eight of them a sound is only ever cut off by the ninth after it
	NextVoice = (NextVoice + 1) % Voices.Num();
	UAudioComponent* Voice = Voices[NextVoice];
	USoundWaveProcedural* Wave = Waves[NextVoice];

	const TArray<int16>& Buffer = GetBuffer(Sound);
	Wave->ResetAudio();
	Wave->QueueAudio((const uint8*)Buffer.GetData(), Buffer.Num() * sizeof(int16));

	Voice->SetWorldLocation(Location);
	Voice->SetVolumeMultiplier(Volume);
	// a pitch of zero asks for a little random variation, so repeated sounds do not machine-gun
	Voice->SetPitchMultiplier(Pitch > 0.0f ? Pitch : FMath::FRandRange(0.94f, 1.06f));
	if (!Voice->IsPlaying())
	{
		Voice->Play();
	}
}

void UFuyuAudio::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay() || !World->GetAudioDeviceRaw())
	{
		return;
	}

	if (!WindVoice)
	{
		USoundWaveProcedural* Wave = nullptr;
		WindVoice = MakeVoice(Wave, false);
		WindWave = Wave;
		WindLoop = SynthesiseWind();
		WindVoice->SetVolumeMultiplier(0.5f);
		WindVoice->Play();
	}

	// keep a loop and a bit queued; the wave drains it at playback speed
	if (WindWave->GetAvailableAudioByteCount() < WindLoop.Num() * (int32)sizeof(int16) / 2)
	{
		WindWave->QueueAudio((const uint8*)WindLoop.GetData(), WindLoop.Num() * sizeof(int16));
	}
}
