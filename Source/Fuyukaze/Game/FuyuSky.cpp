// The sky's clock: the sun and moon's passage across it, and the stars and Milky Way that come out after dark.

#include "FuyuWorld.h"
#include "FuyuGameState.h"
#include "FuyuProps.h"
#include "Async/ParallelFor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	// the night sky is painted once into a panorama: longitude across, straight up at the top edge
	const int32 SkyMapWidth = 4096;
	const int32 SkyMapHeight = 2048;

	FVector SkyDirection(float AzimuthDegrees, float ElevationDegrees)
	{
		return FRotator(ElevationDegrees, AzimuthDegrees, 0.0f).Vector();
	}

	/** Layered noise over a direction, roughly -0.5 to 0.5. Sampling in 3D keeps the panorama free of seams */
	float SkyNoise(const FVector& P, int32 Octaves)
	{
		float Sum = 0.0f, Amplitude = 0.5f;
		FVector Q = P;
		for (int32 i = 0; i < Octaves; ++i)
		{
			Sum += Amplitude * FMath::PerlinNoise3D(Q);
			Q = Q * 2.03 + FVector(17.1, 9.2, 4.7);
			Amplitude *= 0.5f;
		}
		return Sum;
	}

	uint8 SkyEncode(float Linear)
	{
		return (uint8)FMath::RoundToInt32(255.0f * FMath::Pow(FMath::Clamp(Linear, 0.0f, 1.0f), 1.0f / 2.2f));
	}

	float SkyDecode(uint8 Value)
	{
		return FMath::Pow(Value / 255.0f, 2.2f);
	}

	float SkyStep(float A, float B, float X)
	{
		const float T = FMath::Clamp((X - A) / (B - A), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}
}

UTexture2D* AFuyuWorld::BuildStarMap()
{
	const double StartTime = FPlatformTime::Seconds();

	// the galaxy's plane: its bright core hangs over the mountain, and the band climbs from there across the top of the sky
	const FVector Core = SkyDirection(16.0f, 31.0f);
	const FVector Pole = (Core ^ SkyDirection(112.0f, 76.0f)).GetSafeNormal();
	const FVector Along = Pole ^ Core;

	TArray64<uint8> Pixels;
	Pixels.SetNumZeroed((int64)SkyMapWidth * SkyMapHeight * 4);

	ParallelFor(SkyMapHeight, [&](int32 Y)
	{
		const float Colatitude = (Y + 0.5f) / SkyMapHeight * UE_PI;
		const float SinC = FMath::Sin(Colatitude), CosC = FMath::Cos(Colatitude);
		uint8* Row = Pixels.GetData() + (int64)Y * SkyMapWidth * 4;

		for (int32 X = 0; X < SkyMapWidth; ++X)
		{
			const float Longitude = ((X + 0.5f) / SkyMapWidth - 0.5f) * UE_TWO_PI;
			const FVector D(SinC * FMath::Cos(Longitude), SinC * FMath::Sin(Longitude), CosC);

			// position relative to the galaxy: B is the angle off its plane and L the angle round from the core
			const float B = FMath::Asin(FMath::Clamp((float)(D | Pole), -1.0f, 1.0f));
			if (FMath::Abs(B) > 0.8f)
			{
				continue;
			}
			const float L = FMath::Atan2((float)(D | Along), (float)(D | Core));

			const float Broad = SkyNoise(D * 3.1, 4);
			const float Mid = SkyNoise(D * 7.3 + FVector(5.0), 4);
			const float Fine = SkyNoise(D * 19.0 + FVector(11.0), 3);

			// a ragged band of unresolved stars, wider and far brighter around the core
			const float NearCore = FMath::Exp(-FMath::Square(L / 0.85f));
			const float Width = FMath::Lerp(0.105f, 0.2f, NearCore);
			const float Band = FMath::Exp(-FMath::Square((B + Broad * 0.1f) / Width));
			const float Bulge = FMath::Exp(-FMath::Square(L / 0.5f) - FMath::Square(B / 0.15f));
			float Bright = Band * (0.3f + 0.55f * FMath::Exp(-FMath::Square(L / 1.35f))) * (0.75f + 1.1f * Mid) + Bulge * 0.7f;

			// dark lanes of dust run along the middle of the band and break it into clouds
			const float Rift = FMath::Exp(-FMath::Square((B - 0.02f + Broad * 0.09f) / (0.03f + 0.03f * NearCore)));
			const float Dust = FMath::Clamp(Rift * (0.5f + 2.2f * Mid + 1.6f * Fine), 0.0f, 1.0f);
			const float Mottle = FMath::Clamp((Fine + Mid * 0.6f) * 2.4f - 0.2f, 0.0f, 1.0f);
			Bright = FMath::Max(Bright, 0.0f) * (1.0f - 0.88f * Dust) * (1.0f - 0.4f * Mottle);

			// the grain of countless faint stars
			const uint32 Hash = HashCombineFast(GetTypeHash(X * 7919 + 13), GetTypeHash(Y * 104729 + 71));
			const float Grain = FMath::Pow((Hash & 0xffff) / 65535.0f, 9.0f);
			Bright += Grain * Band * (1.0f - 0.7f * Dust) * 0.55f;

			// warm at the core, cold blue-white along the arms, with a blush of pink nebula here and there
			const float Warm = FMath::Clamp(Bulge * 1.3f + Band * NearCore * 0.55f, 0.0f, 1.0f);
			FLinearColor Colour = FMath::Lerp(FLinearColor(0.56f, 0.69f, 1.0f), FLinearColor(1.0f, 0.8f, 0.58f), Warm);
			const float Pink = FMath::Clamp(SkyNoise(D * 5.5 + FVector(31.0), 3) * 3.4f - 0.5f, 0.0f, 1.0f) * Band;
			Colour += FLinearColor(0.55f, 0.08f, 0.26f) * Pink * 0.5f;

			Row[X * 4 + 0] = SkyEncode(Colour.B * Bright * 0.34f);
			Row[X * 4 + 1] = SkyEncode(Colour.G * Bright * 0.34f);
			Row[X * 4 + 2] = SkyEncode(Colour.R * Bright * 0.34f);
			Row[X * 4 + 3] = 255;
		}
	});

	// individual stars: most scattered evenly, the rest crowding towards the galaxy's plane
	FRandomStream Rng(1905);
	const FLinearColor Tints[] = {
		FLinearColor(0.72f, 0.82f, 1.0f), FLinearColor(0.86f, 0.91f, 1.0f), FLinearColor(1.0f, 1.0f, 1.0f),
		FLinearColor(1.0f, 0.95f, 0.82f), FLinearColor(1.0f, 0.84f, 0.62f), FLinearColor(1.0f, 0.7f, 0.5f) };

	for (int32 i = 0; i < 38000; ++i)
	{
		FVector D;
		if (Rng.FRand() < 0.55f)
		{
			D = Rng.VRand();
		}
		else
		{
			const float B = (Rng.FRand() + Rng.FRand() + Rng.FRand() - 1.5f) * 0.3f;
			const float L = Rng.FRand() < 0.4f ? (Rng.FRand() + Rng.FRand() - 1.0f) * 1.4f : Rng.FRandRange(-UE_PI, UE_PI);
			D = (Core * FMath::Cos(L) + Along * FMath::Sin(L)) * FMath::Cos(B) + Pole * FMath::Sin(B);
		}

		// a handful of bright stars among thousands of faint ones
		const float Magnitude = FMath::Pow(Rng.FRand(), 7.0f);
		const float Bright = 0.16f + 2.2f * Magnitude;
		const float Sigma = 0.36f + 0.34f * Magnitude;
		const FLinearColor Tint = Tints[Rng.RandRange(0, UE_ARRAY_COUNT(Tints) - 1)];

		const float Colatitude = FMath::Acos(FMath::Clamp((float)D.Z, -1.0f, 1.0f));
		const float PX = (FMath::Atan2((float)D.Y, (float)D.X) / UE_TWO_PI + 0.5f) * SkyMapWidth;
		const float PY = Colatitude / UE_PI * SkyMapHeight;

		// the panorama stretches towards its top and bottom edges, so stars there are drawn wider to stay round
		const float Stretch = FMath::Min(1.0f / FMath::Max(FMath::Sin(Colatitude), 0.02f), 24.0f);
		const int32 RadiusY = FMath::CeilToInt32(Sigma * 2.6f), RadiusX = FMath::CeilToInt32(Sigma * 2.6f * Stretch);

		for (int32 Y = FMath::FloorToInt32(PY) - RadiusY; Y <= FMath::FloorToInt32(PY) + RadiusY; ++Y)
		{
			if (Y < 0 || Y >= SkyMapHeight)
			{
				continue;
			}
			for (int32 X = FMath::FloorToInt32(PX) - RadiusX; X <= FMath::FloorToInt32(PX) + RadiusX; ++X)
			{
				const float DX = (X + 0.5f - PX) / Stretch, DY = Y + 0.5f - PY;
				const float Glow = Bright * FMath::Exp(-(DX * DX + DY * DY) / (2.0f * Sigma * Sigma));
				if (Glow < 0.004f)
				{
					continue;
				}

				uint8* Pixel = Pixels.GetData() + ((int64)Y * SkyMapWidth + ((X % SkyMapWidth) + SkyMapWidth) % SkyMapWidth) * 4;
				Pixel[0] = SkyEncode(SkyDecode(Pixel[0]) + Tint.B * Glow);
				Pixel[1] = SkyEncode(SkyDecode(Pixel[1]) + Tint.G * Glow);
				Pixel[2] = SkyEncode(SkyDecode(Pixel[2]) + Tint.R * Glow);
			}
		}
	}

	UTexture2D* Map = UTexture2D::CreateTransient(SkyMapWidth, SkyMapHeight, PF_B8G8R8A8, NAME_None, Pixels);
	if (Map)
	{
		Map->SRGB = true;
		Map->Filter = TF_Bilinear;
		Map->AddressX = TA_Wrap;
		Map->AddressY = TA_Clamp;
		Map->UpdateResource();
	}

	UE_LOG(LogTemp, Display, TEXT("Fuyukaze: painted the night sky in %.2f s"), FPlatformTime::Seconds() - StartTime);
	return Map;
}

void AFuyuWorld::BuildSky()
{
	// without the sky material the atmosphere still draws itself, there are just no stars
	UMaterialInterface* Base = FFuyuMaterials::Get().Sky;
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Base || !Sphere)
	{
		return;
	}

	StarMap = BuildStarMap();
	SkyMaterial = UMaterialInstanceDynamic::Create(Base, this);
	SkyMaterial->SetTextureParameterValue(TEXT("Stars"), StarMap);
	SkyMaterial->SetScalarParameterValue(TEXT("StarGain"), StarBrightness);

	// a shell far beyond the island that travels with the camera, seen from the inside
	SkyDome = NewObject<UStaticMeshComponent>(this);
	SkyDome->SetStaticMesh(Sphere);
	SkyDome->SetMaterial(0, SkyMaterial);
	SkyDome->SetMobility(EComponentMobility::Movable);
	SkyDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyDome->SetCastShadow(false);
	SkyDome->SetWorldScale3D(FVector(30000.0f));
	SkyDome->SetupAttachment(Root);
	SkyDome->RegisterComponent();
}

void AFuyuWorld::UpdateSky(float DeltaSeconds, const FVector& ViewLocation)
{
	// every machine reads the same clock from the game state. When time is passed the sky sweeps forward
	// to catch up with it instead of jumping
	const float Cycle = AFuyuGameState::CycleSeconds;
	const AFuyuGameState* State = GetWorld()->GetGameState<AFuyuGameState>();
	const float Target = State ? State->GetCycleSeconds() : FMath::Max(SkyClock, 0.0f);
	const float Ahead = FMath::Fmod(Target - SkyClock + Cycle, Cycle);
	if (SkyClock < 0.0f || Ahead < 3.0f || Ahead > Cycle - 30.0f)
	{
		SkyClock = Target;
	}
	else
	{
		SkyClock = FMath::Fmod(SkyClock + FMath::Min(Ahead, DeltaSeconds * FMath::Max(70.0f, Ahead * 0.9f)), Cycle);
	}
	SkyHour = AFuyuGameState::CycleToHour(SkyClock);

	// the sun climbs from the eastern horizon at six, leans over to one side at noon and sets at eighteen.
	// The moon rides half a day behind it on a lower arc, so it is up for the whole of the night
	const float Angle = (SkyHour - 6.0f) / 24.0f * UE_TWO_PI;
	const FVector East = FRotator(0.0f, SunriseHeading, 0.0f).Vector();
	const FVector Across = FRotator(0.0f, SunriseHeading + 90.0f, 0.0f).Vector();
	auto OnArc = [&](float A, float TiltDegrees)
	{
		const float Tilt = FMath::DegreesToRadians(TiltDegrees);
		return East * FMath::Cos(A) + (FVector::UpVector * FMath::Cos(Tilt) + Across * FMath::Sin(Tilt)) * FMath::Sin(A);
	};
	const FVector ToSun = OnArc(Angle, SunTilt);
	const FVector ToMoon = OnArc(Angle + UE_PI, MoonTilt);
	const float SunHeight = FMath::RadiansToDegrees(FMath::Asin((float)ToSun.Z));
	const float MoonHeight = FMath::RadiansToDegrees(FMath::Asin((float)ToMoon.Z));

	// the sun goes on lighting the air for a while after it has set, which is the dusk; the stars follow it out
	const float SunLight = SkyStep(-18.0f, -8.0f, SunHeight);
	const float Night = 1.0f - SkyStep(-14.0f, -4.0f, SunHeight);
	const float MoonLight = Night * SkyStep(0.0f, 9.0f, MoonHeight);

	Sun->SetWorldRotation((-ToSun).Rotation());
	Sun->SetIntensity(SunIntensity * SunLight);
	Sun->SetVisibility(SunLight > 0.001f);

	Moon->SetWorldRotation((-ToMoon).Rotation());
	Moon->SetIntensity(MoonIntensity * MoonLight);
	Moon->SetVisibility(MoonLight > 0.001f);

	if (SkyDome)
	{
		SkyDome->SetWorldLocation(ViewLocation);

		// the stars wheel slowly about the pole through the night
		SkyMaterial->SetScalarParameterValue(TEXT("Night"), Night);
		SkyMaterial->SetScalarParameterValue(TEXT("Spin"), 0.45f * FMath::Sin(SkyHour / 24.0f * UE_TWO_PI));
		SkyMaterial->SetVectorParameterValue(TEXT("MoonDir"), FLinearColor(ToMoon.X, ToMoon.Y, ToMoon.Z, SkyStep(-3.0f, 1.5f, MoonHeight) * (0.2f + 0.8f * Night)));
	}

	// the rest only needs touching as dusk and dawn pass
	if (FMath::Abs(Night - NightAmount) > 0.004f || (Night != NightAmount && (Night == 0.0f || Night == 1.0f)))
	{
		NightAmount = Night;

		// by day this stands in for light bounced off the golden ground; by night there is next to none
		SkyLight->SetLowerHemisphereColor(FMath::Lerp(FLinearColor(0.2f, 0.15f, 0.08f), FLinearColor(0.002f, 0.003f, 0.006f), Night));
		SkyLight->SetIntensity(FMath::Lerp(1.5f, NightSkyLight, Night));
		ApplyGrade();
	}
}
