#include "FuyuTerrain.h"

#define LOCTEXT_NAMESPACE "Fuyukaze"

namespace
{
	/** Hermite ramp from 0 at Edge0 to 1 at Edge1. Unlike FMath::SmoothStep the edges may be given in descending order */
	float Smooth(float Edge0, float Edge1, float X)
	{
		const float T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float Fbm(float X, float Y, int32 Octaves, float Lacunarity = 2.03f, float Gain = 0.5f)
	{
		float Sum = 0.0f;
		float Amp = 1.0f;
		float Norm = 0.0f;

		for (int32 i = 0; i < Octaves; ++i)
		{
			Sum += Amp * FMath::PerlinNoise2D(FVector2D(X, Y));
			Norm += Amp;
			X = X * Lacunarity + 17.31f;
			Y = Y * Lacunarity - 9.77f;
			Amp *= Gain;
		}

		return Sum / Norm;
	}

	/** Terrain shape before sites are levelled */
	float RawHeight(float X, float Y)
	{
		using namespace FuyuTerrain;

		// rolling lowland hills, with a broader swell underneath
		float H = 1500.0f;
		H += 2600.0f * Fbm(X / 42000.0f + 3.1f, Y / 42000.0f - 1.7f, 4);
		H += 900.0f * Fbm(X / 9000.0f - 11.0f, Y / 9000.0f + 5.0f, 3);
		H += 140.0f * Fbm(X / 1700.0f, Y / 1700.0f, 2);

		// the lake basin dips below sea level so the single water plane fills it
		const float LakeD = FVector2D::Distance(FVector2D(X, Y), LakeCenter) + 2600.0f * Fbm(X / 12000.0f, Y / 12000.0f, 2);
		const float Lake = Smooth(15500.0f, 6500.0f, LakeD);
		H = FMath::Lerp(H, -650.0f, Lake);

		// the island sinks into the sea towards the edge of the map
		const float EdgeNoise = 9000.0f * Fbm(X / 30000.0f + 40.0f, Y / 30000.0f + 40.0f, 3);
		const float Radial = FVector2D(X, Y).Size() + EdgeNoise;
		const float Coast = Smooth(108000.0f, 136000.0f, Radial);
		H = FMath::Lerp(H, -1400.0f, Coast);

		// the volcano is added last so its northern flank runs straight down into the sea.
		// A broad skirt steepens gently towards the summit, which holds a shallow crater
		const float D = FVector2D::Distance(FVector2D(X, Y), MountainCenter);
		const float T = FMath::Clamp(1.0f - D / MountainRadius, 0.0f, 1.0f);
		float Cone = FMath::Pow(T, 1.75f) * MountainPeak;
		const float Crater = FMath::Clamp(1.0f - D / 8000.0f, 0.0f, 1.0f);
		Cone -= Crater * Crater * 4500.0f;

		// gullies run down the flanks and fade out near the base
		const float Angle = FMath::Atan2(Y - MountainCenter.Y, X - MountainCenter.X);
		const float Gully = Fbm(Angle * 3.3f + 20.0f, D / 26000.0f, 3);
		Cone *= 1.0f + 0.09f * Gully * FMath::Clamp(T * 2.5f, 0.0f, 1.0f);
		H += Cone;

		return H;
	}

	struct FSiteCache
	{
		TArray<FFuyuSite> Sites;
		TArray<float> Heights;

		FSiteCache()
		{
			auto Add = [this](const TCHAR* Id, const FText& Name, EFuyuSiteType Type, FVector2D Pos, float Radius, float Yaw)
			{
				FFuyuSite S;
				S.Id = Id;
				S.DisplayName = Name;
				S.Type = Type;
				S.Pos = Pos;
				S.Radius = Radius;
				S.Yaw = Yaw;
				Sites.Add(S);
			};

			Add(TEXT("Start"), LOCTEXT("SiteStart", "Windward Hill"), EFuyuSiteType::Start, FVector2D(-78000, -16000), 1400.0f, 10.0f);
			Add(TEXT("CampFarm"), LOCTEXT("SiteFarm", "Susuki Farmstead"), EFuyuSiteType::Camp, FVector2D(-47000, -30000), 3000.0f, 155.0f);
			Add(TEXT("CampLake"), LOCTEXT("SiteLake", "Lakeside Camp"), EFuyuSiteType::Camp, FVector2D(-27000, 38000), 3000.0f, 226.0f);
			Add(TEXT("CampRidge"), LOCTEXT("SiteRidge", "Ridge Watch"), EFuyuSiteType::Camp, FVector2D(-4000, -52000), 3000.0f, 153.0f);
			Add(TEXT("Shrine"), LOCTEXT("SiteShrine", "Shrine of the First Snow"), EFuyuSiteType::Shrine, FVector2D(24000, 4000), 3400.0f, 180.0f);
			Add(TEXT("Lookout"), LOCTEXT("SiteLookout", "Crane's Rest"), EFuyuSiteType::Lookout, FVector2D(-52000, 12000), 1500.0f, 0.0f);
			Add(TEXT("Memorial"), LOCTEXT("SiteMemorial", "Field of Remembrance"), EFuyuSiteType::Memorial, FVector2D(-18000, 8000), 2400.0f, 173.0f);

			for (const FFuyuSite& S : Sites)
			{
				Heights.Add(FMath::Max(RawHeight(S.Pos.X, S.Pos.Y), 260.0f));
			}

			// trails link the sites: from the start hill to each camp gate, and up through the gates to the shrine
			auto Find = [this](const TCHAR* Id) { return *Sites.FindByPredicate([Id](const FFuyuSite& S) { return S.Id == Id; }); };
			auto Gate = [&Find](const TCHAR* Id, float Distance)
			{
				const FFuyuSite& S = Find(Id);
				const float Rad = FMath::DegreesToRadians(S.Yaw);
				return S.Pos + FVector2D(FMath::Cos(Rad), FMath::Sin(Rad)) * Distance;
			};

			const FVector2D Start = Find(TEXT("Start")).Pos;
			const FVector2D Lookout = Find(TEXT("Lookout")).Pos;
			const FVector2D FarmGate = Gate(TEXT("CampFarm"), 3600.0f);
			const FVector2D LakeGate = Gate(TEXT("CampLake"), 3600.0f);
			const FVector2D RidgeGate = Gate(TEXT("CampRidge"), 3600.0f);
			const FVector2D ShrineFoot = Gate(TEXT("Shrine"), 7600.0f);

			Paths.Add({ Start, FarmGate });
			Paths.Add({ FarmGate, Find(TEXT("CampFarm")).Pos });
			Paths.Add({ FarmGate, RidgeGate });
			Paths.Add({ RidgeGate, Find(TEXT("CampRidge")).Pos });
			Paths.Add({ Start, Lookout });
			Paths.Add({ Lookout, LakeGate });
			Paths.Add({ LakeGate, Find(TEXT("CampLake")).Pos });
			Paths.Add({ Lookout, ShrineFoot });
			Paths.Add({ ShrineFoot, Find(TEXT("Shrine")).Pos });
		}

		struct FSegment
		{
			FVector2D A, B;
		};
		TArray<FSegment> Paths;
	};

	const FSiteCache& GetCache()
	{
		static const FSiteCache Cache;
		return Cache;
	}

	/** Returns how deep inside a site this point is (0 outside, 1 on the levelled ground) */
	float SiteInfluence(float X, float Y, float Margin = 0.0f, bool bClearedOnly = false)
	{
		float Best = 0.0f;
		for (const FFuyuSite& S : GetCache().Sites)
		{
			// the memorial field is levelled like any site, but its ground is left to the flowers
			if (bClearedOnly && S.Type == EFuyuSiteType::Memorial)
			{
				continue;
			}

			const float D = FVector2D::Distance(FVector2D(X, Y), S.Pos);
			Best = FMath::Max(Best, Smooth(S.Radius * 1.7f + Margin, S.Radius + Margin, D));
		}
		return Best;
	}
}

float FuyuTerrain::PathDistance(float X, float Y)
{
	// bend the query point with a little noise so trails wander instead of running ruler-straight
	const FVector2D P(X + 650.0f * Fbm(X / 8000.0f + 3.0f, Y / 8000.0f, 2), Y + 650.0f * Fbm(X / 8000.0f, Y / 8000.0f + 7.0f, 2));

	float Best = MAX_flt;
	for (const FSiteCache::FSegment& Seg : GetCache().Paths)
	{
		Best = FMath::Min(Best, (float)FVector2D::Distance(P, FMath::ClosestPointOnSegment2D(P, Seg.A, Seg.B)));
	}
	return Best;
}

const TArray<FFuyuSite>& FuyuTerrain::GetSites()
{
	return GetCache().Sites;
}

const FFuyuSite* FuyuTerrain::FindSite(FName Id)
{
	return GetCache().Sites.FindByPredicate([Id](const FFuyuSite& S) { return S.Id == Id; });
}

float FuyuTerrain::Height(float X, float Y)
{
	float H = RawHeight(X, Y);

	const FSiteCache& Cache = GetCache();
	for (int32 i = 0; i < Cache.Sites.Num(); ++i)
	{
		const FFuyuSite& S = Cache.Sites[i];
		const float D = FVector2D::Distance(FVector2D(X, Y), S.Pos);
		if (D < S.Radius * 1.7f)
		{
			H = FMath::Lerp(H, Cache.Heights[i], Smooth(S.Radius * 1.7f, S.Radius, D));
		}
	}

	return H;
}

FVector FuyuTerrain::Normal(float X, float Y)
{
	const float E = 150.0f;
	const float DX = Height(X + E, Y) - Height(X - E, Y);
	const float DY = Height(X, Y + E) - Height(X, Y - E);
	return FVector(-DX, -DY, 2.0f * E).GetSafeNormal();
}

FVector FuyuTerrain::Ground(const FVector2D& P)
{
	return FVector(P.X, P.Y, Height(P.X, P.Y));
}

namespace
{
	float FieldAmount(float X, float Y, int32& OutIndex);
}

float FuyuTerrain::ForestMask(float X, float Y)
{
	const float N = Fbm(X / 26000.0f + 90.0f, Y / 26000.0f - 60.0f, 3);
	float Mask = Smooth(-0.1f, 0.16f, N);

	// a ring of woodland wraps the foot of the mountain, and the start hill is kept open for the view
	const float D = FVector2D::Distance(FVector2D(X, Y), MountainCenter);
	Mask = FMath::Max(Mask, Smooth(122000.0f, 110000.0f, D) * Smooth(70000.0f, 84000.0f, D) * 0.9f);

	const FFuyuSite* Start = FindSite(TEXT("Start"));
	if (Start)
	{
		Mask *= Smooth(5000.0f, 12000.0f, (float)FVector2D::Distance(FVector2D(X, Y), Start->Pos));
	}

	Mask *= Smooth(350.0f, 1100.0f, PathDistance(X, Y));

	// flower fields are open ground, with only the odd tree left standing in them
	int32 Field;
	Mask *= 1.0f - 0.93f * FieldAmount(X, Y, Field);

	return Mask * (1.0f - SiteInfluence(X, Y, 300.0f));
}

namespace
{
	struct FFlowerField
	{
		FVector2D Pos;
		float Radius;
		FLinearColor Color;
		FLinearColor Accent;
	};

	/** The island's named flower fields: white below the start hill, blue at the memorial, violet under the pagoda */
	const FFlowerField GFlowerFields[] = {
		{ FVector2D(-73500, -14500), 5600.0f, FLinearColor(0.92f, 0.92f, 0.86f), FLinearColor(0.95f, 0.8f, 0.15f) },
		{ FVector2D(-18000, 8000),   7200.0f, FLinearColor(0.16f, 0.4f, 1.0f),   FLinearColor(0.55f, 0.75f, 1.0f) },
		{ FVector2D(-57500, 5000),   6800.0f, FLinearColor(0.45f, 0.17f, 0.85f), FLinearColor(1.0f, 0.78f, 0.08f) },
	};

	/** Strength of the strongest named field at this point, and which one it is */
	float FieldAmount(float X, float Y, int32& OutIndex)
	{
		const float Wobble = 1300.0f * Fbm(X / 5200.0f + 12.0f, Y / 5200.0f - 4.0f, 2);
		float Best = 0.0f;
		OutIndex = INDEX_NONE;

		for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(GFlowerFields); ++i)
		{
			const float D = FVector2D::Distance(FVector2D(X, Y), GFlowerFields[i].Pos) + Wobble;
			const float A = Smooth(GFlowerFields[i].Radius, GFlowerFields[i].Radius * 0.72f, D);
			if (A > Best)
			{
				Best = A;
				OutIndex = i;
			}
		}
		return Best;
	}
}

float FuyuTerrain::FlowerMask(float X, float Y)
{
	const float N = Fbm(X / 15000.0f - 31.0f, Y / 15000.0f + 77.0f, 3);
	return Smooth(0.12f, 0.3f, N) * (1.0f - ForestMask(X, Y));
}

float FuyuTerrain::Flowers(float X, float Y, FLinearColor& OutColor)
{
	int32 Index;
	const float Field = FieldAmount(X, Y, Index);

	if (Field > 0.02f)
	{
		// within a field the accent colour runs through in drifts, like the yellow band across the violet slope
		const FFlowerField& F = GFlowerFields[Index];
		const float Drift = Fbm(X / 2300.0f + Index * 9.0f, Y / 900.0f, 2);
		OutColor = Drift > 0.24f ? F.Accent : F.Color;
		return Field;
	}

	// elsewhere, loose meadows of white with the odd patch of yellow
	const float Patch = Fbm(X / 3100.0f + 2.0f, Y / 3100.0f + 8.0f, 2);
	OutColor = Patch > 0.2f ? FLinearColor(0.98f, 0.82f, 0.12f) : FLinearColor(0.93f, 0.93f, 0.88f);
	return FlowerMask(X, Y) * 0.55f;
}

float FuyuTerrain::GrassDensity(float X, float Y, float H, float NormalZ)
{
	float Density = Smooth(160.0f, 420.0f, H);
	Density *= Smooth(RockLine + 1500.0f, RockLine - 2500.0f, H);
	Density *= Smooth(0.72f, 0.86f, NormalZ);
	Density *= 1.0f - 0.92f * SiteInfluence(X, Y, 0.0f, true);
	Density *= 1.0f - 0.45f * ForestMask(X, Y);
	Density *= Smooth(170.0f, 420.0f, PathDistance(X, Y));
	return Density;
}

FLinearColor FuyuTerrain::GroundColor(float X, float Y, float H, float NormalZ)
{
	// autumn palette: straw-gold grass, with greener hollows and darker forest floor
	const FLinearColor GrassGold(0.29f, 0.225f, 0.08f);
	const FLinearColor GrassGreen(0.16f, 0.22f, 0.055f);
	const FLinearColor ForestFloor(0.105f, 0.085f, 0.04f);
	const FLinearColor Meadow(0.21f, 0.27f, 0.075f);
	const FLinearColor Sand(0.52f, 0.44f, 0.30f);
	const FLinearColor Rock(0.17f, 0.16f, 0.155f);
	const FLinearColor DarkRock(0.085f, 0.08f, 0.082f);
	const FLinearColor Snow(0.86f, 0.89f, 0.93f);
	const FLinearColor PackedEarth(0.17f, 0.125f, 0.08f);

	const float Variation = Fbm(X / 6000.0f + 5.0f, Y / 6000.0f + 5.0f, 3);
	FLinearColor C = FMath::Lerp(GrassGold, GrassGreen, Smooth(-0.15f, 0.35f, Variation));

	C = FMath::Lerp(C, Meadow, FlowerMask(X, Y) * 0.8f);
	C = FMath::Lerp(C, ForestFloor, ForestMask(X, Y) * 0.75f);

	// flower fields: lush green underneath, washed with the colour of the bloom so the field still reads from a hilltop away
	FLinearColor Bloom;
	const float Blooming = Flowers(X, Y, Bloom);
	C = FMath::Lerp(C, FLinearColor(0.085f, 0.17f, 0.045f), FMath::Min(Blooming * 1.2f, 0.9f));
	C = FMath::Lerp(C, Bloom * 0.5f, Blooming * 0.5f);

	C = FMath::Lerp(C, PackedEarth, SiteInfluence(X, Y, 0.0f, true) * 0.85f);

	// terrain vertices are 7.5 m apart, so the trail is painted wide and soft; the gap in the grass gives it its edge
	C = FMath::Lerp(C, PackedEarth, Smooth(1100.0f, 250.0f, PathDistance(X, Y)) * 0.75f);

	// beaches and lake shore
	C = FMath::Lerp(C, Sand, Smooth(420.0f, 150.0f, H));

	// steep ground and high ground show rock, with strata picked out by a fine noise
	const float Strata = 0.5f + 0.5f * Fbm(X / 900.0f, H / 260.0f, 2);
	const FLinearColor RockCol = FMath::Lerp(DarkRock, Rock, Strata);
	const float Steep = Smooth(0.84f, 0.68f, NormalZ);
	const float High = Smooth(RockLine - 2500.0f, RockLine + 3000.0f, H + 1800.0f * Variation);

	// the mountain's flanks are clothed in dark forest right up to the snow; only the steep upper cone shows bare rock
	const float Ribs = 0.5f + 0.5f * Fbm(FMath::Atan2(Y - MountainCenter.Y, X - MountainCenter.X) * 9.0f, H / 9000.0f, 2);
	const FLinearColor MountainForest = FMath::Lerp(FLinearColor(0.022f, 0.05f, 0.022f), FLinearColor(0.05f, 0.105f, 0.035f), Ribs);
	C = FMath::Lerp(C, MountainForest, High * 0.92f);

	const float Rockiness = Steep;
	C = FMath::Lerp(C, RockCol, Rockiness);

	// snow settles on the summit and clings less to steep faces
	// the snow line is ragged: tongues of snow reach down the gullies and bare ribs of rock climb between them
	const float Around = FMath::Atan2(Y - MountainCenter.Y, X - MountainCenter.X);
	const float Ragged = 24000.0f * Fbm(Around * 5.0f + 4.0f, H / 40000.0f, 3) + 6000.0f * Variation;
	const float SnowAmount = Smooth(SnowLine - 3000.0f, SnowLine + 3000.0f, H + Ragged) * Smooth(0.45f, 0.7f, NormalZ);
	C = FMath::Lerp(C, Snow, SnowAmount);

	// alpha tells the terrain material how rocky the ground is, so it can pick between soil and stone textures
	C.A = Rockiness;
	return C;
}

bool FuyuTerrain::IsOpenGround(float X, float Y, float MaxSlopeDeg, float SiteMargin)
{
	if (FMath::Abs(X) > HalfSize - 2000.0f || FMath::Abs(Y) > HalfSize - 2000.0f)
	{
		return false;
	}

	if (Height(X, Y) < 220.0f)
	{
		return false;
	}

	if (Normal(X, Y).Z < FMath::Cos(FMath::DegreesToRadians(MaxSlopeDeg)))
	{
		return false;
	}

	for (const FFuyuSite& S : GetCache().Sites)
	{
		if (FVector2D::Distance(FVector2D(X, Y), S.Pos) < S.Radius + SiteMargin)
		{
			return false;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
