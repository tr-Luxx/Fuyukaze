// Herds of wild horses that gallop round the island's open ground, pull up to rest, and run again.

#include "FuyuWorld.h"
#include "FuyuGameState.h"
#include "FuyuProps.h"
#include "FuyuSamurai.h"
#include "FuyuTerrain.h"
#include "Algo/BinarySearch.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"

namespace
{
	// how long a herd runs before it pulls up, how long it stands, and how long it takes to get going or to stop
	const float HerdRunSeconds = 46.0f;
	const float HerdRestSeconds = 13.0f;
	const float HerdRampSeconds = 3.5f;

	/**
	 *  How far a herd has travelled by Time, and how hard it is running right now (0 standing, 1 full gallop).
	 *  A pure function of the clock, so every machine in a multiplayer game sees the herds in the same place
	 */
	double HerdDistance(double Time, float Speed, float& OutGait)
	{
		const double Period = HerdRunSeconds + HerdRestSeconds;
		const double PerRun = Speed * (HerdRunSeconds - HerdRampSeconds);
		const double Runs = FMath::FloorToDouble(Time / Period);
		const float T = (float)(Time - Runs * Period);

		float Within = 0.0f;
		if (T < HerdRampSeconds)
		{
			OutGait = T / HerdRampSeconds;
			Within = Speed * T * T / (2.0f * HerdRampSeconds);
		}
		else if (T < HerdRunSeconds - HerdRampSeconds)
		{
			OutGait = 1.0f;
			Within = Speed * (T - HerdRampSeconds * 0.5f);
		}
		else if (T < HerdRunSeconds)
		{
			const float Left = HerdRunSeconds - T;
			OutGait = Left / HerdRampSeconds;
			Within = (float)PerRun - Speed * Left * Left / (2.0f * HerdRampSeconds);
		}
		else
		{
			OutGait = 0.0f;
			Within = (float)PerRun;
		}
		return Runs * PerRun + Within;
	}

	/** Ground a horse can gallop over: dry, fairly level, clear of the sites and out of the trees */
	bool HerdCanCross(const FVector2D& P)
	{
		return FuyuTerrain::IsOpenGround(P.X, P.Y, 20.0f, 1800.0f) && FuyuTerrain::ForestMask(P.X, P.Y) < 0.3f && FuyuTerrain::Height(P.X, P.Y) > 160.0f;
	}
}

void AFuyuWorld::AddHerd(const FVector2D& Center, float Radius, int32 Count, int32 Seed)
{
	FRandomStream Rng(Seed);

	// a rough ring of waypoints about the centre. Any that land in forest, water, a camp or on a steep slope are
	// pulled in towards the middle until they are on good ground, and dropped if there is none to be had
	TArray<FVector2D> Waypoints;
	const int32 NumWaypoints = 11;
	const float Twist = Rng.FRandRange(0.0f, UE_TWO_PI);
	for (int32 i = 0; i < NumWaypoints; ++i)
	{
		const float Angle = Twist + i * UE_TWO_PI / NumWaypoints;
		const FVector2D Out(FMath::Cos(Angle), FMath::Sin(Angle) * 0.8f);
		for (float Reach = Radius * Rng.FRandRange(0.8f, 1.15f); Reach > Radius * 0.3f; Reach -= Radius * 0.08f)
		{
			if (HerdCanCross(Center + Out * Reach))
			{
				Waypoints.Add(Center + Out * Reach);
				break;
			}
		}
	}

	if (Waypoints.Num() < 5)
	{
		UE_LOG(LogTemp, Display, TEXT("Fuyukaze: no room for a herd at %.0f, %.0f"), Center.X, Center.Y);
		return;
	}

	// round the corners off into a closed curve, kept as a run of short straight pieces with the distance to each
	FHerd Herd;
	const int32 N = Waypoints.Num();
	for (int32 i = 0; i < N; ++i)
	{
		const FVector P0(Waypoints[(i + N - 1) % N], 0.0), P1(Waypoints[i], 0.0), P2(Waypoints[(i + 1) % N], 0.0), P3(Waypoints[(i + 2) % N], 0.0);
		for (int32 s = 0; s < 16; ++s)
		{
			Herd.Path.Add(FVector2D(FMath::CubicCRSplineInterp(P0, P1, P2, P3, 0.0f, 1.0f, 2.0f, 3.0f, 1.0f + s / 16.0f)));
		}
	}

	Herd.Along.Add(0.0f);
	for (int32 i = 0; i < Herd.Path.Num(); ++i)
	{
		Herd.Along.Add(Herd.Along.Last() + FVector2D::Distance(Herd.Path[i], Herd.Path[(i + 1) % Herd.Path.Num()]));
	}
	Herd.Length = Herd.Along.Last();
	Herd.Speed = Rng.FRandRange(820.0f, 940.0f);
	Herd.TimeOffset = Rng.FRandRange(0.0f, HerdRunSeconds + HerdRestSeconds);
	Herd.First = HorseState.Num();
	Herd.Count = Count;

	if (!Horses)
	{
		Horses = NewObject<UInstancedStaticMeshComponent>(this);
		Horses->SetStaticMesh(FuyuProps::Get(FuyuProps::EProp::Horse));
		Horses->SetMobility(EComponentMobility::Movable);
		Horses->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Horses->SetCastShadow(true);

		// per horse: where it is in its stride, and how hard it is running. The material does the rest
		Horses->SetNumCustomDataFloats(2);

		// the gallop reaches outside the standing horse's bounds
		Horses->SetBoundsScale(1.6f);
		Horses->SetupAttachment(Root);
		Horses->RegisterComponent();
	}

	// the herd runs three abreast, strung out behind its leader
	for (int32 i = 0; i < Count; ++i)
	{
		FHorse Horse;
		Horse.Back = (i / 3) * 430.0f + (i % 3) * 140.0f + Rng.FRandRange(0.0f, 160.0f);
		Horse.Side = i == 0 ? 0.0f : ((i % 3) - 1) * 340.0f + Rng.FRandRange(-90.0f, 90.0f);
		Horse.Scale = Rng.FRandRange(0.9f, 1.1f);
		Horse.Phase = Rng.FRand();
		HorseState.Add(Horse);

		const int32 Index = Horses->AddInstance(FTransform(FVector(Center, 0.0)), true);
		const float Data[2] = { Horse.Phase, 0.0f };
		Horses->SetCustomData(Index, MakeArrayView(Data, 2), false);
	}

	Herds.Add(MoveTemp(Herd));
	UE_LOG(LogTemp, Display, TEXT("Fuyukaze: a herd of %d runs a %.0f m circuit about %.0f, %.0f"), Count, Herds.Last().Length / 100.0f, Center.X, Center.Y);
}

void AFuyuWorld::BuildHorses()
{
	// no horse model, no herds (see Tools/convert_horse.py)
	if (!FuyuProps::Get(FuyuProps::EProp::Horse))
	{
		return;
	}

	// the meadows between the start and the first camp, the heart of the island, and the country round the lake
	AddHerd(FVector2D(-66000, -8000), 7500.0f, 7, 501);
	AddHerd(FVector2D(-36000, -6000), 8500.0f, 6, 502);
	AddHerd(FVector2D(-10000, 24000), 7500.0f, 5, 503);
	AddHerd(FVector2D(-48000, 26000), 6500.0f, 6, 504);
}

bool AFuyuWorld::GetHerdLeader(int32 Herd, FTransform& OutTransform) const
{
	if (!Herds.IsValidIndex(Herd))
	{
		return false;
	}
	OutTransform = Herds[Herd].Leader;
	return true;
}

void AFuyuWorld::UpdateHorses()
{
	if (!Horses || Herds.Num() == 0)
	{
		return;
	}

	const AFuyuGameState* State = GetWorld()->GetGameState<AFuyuGameState>();
	const double Now = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();

	TArray<FTransform> Transforms;
	Transforms.Reserve(HorseState.Num());

	for (FHerd& Herd : Herds)
	{
		float Gait = 0.0f;
		const double Travelled = HerdDistance(Now + Herd.TimeOffset, Herd.Speed, Gait);

		for (int32 i = 0; i < Herd.Count; ++i)
		{
			const FHorse& Horse = HorseState[Herd.First + i];

			// find the stretch of the circuit this horse is on
			const float Where = (float)FMath::Fmod(Travelled - Horse.Back + Herd.Length * 4.0, (double)Herd.Length);
			const int32 Piece = FMath::Clamp(Algo::UpperBound(Herd.Along, Where) - 1, 0, Herd.Path.Num() - 1);
			const FVector2D A = Herd.Path[Piece], B = Herd.Path[(Piece + 1) % Herd.Path.Num()];
			const float Span = FMath::Max(Herd.Along[Piece + 1] - Herd.Along[Piece], 1.0f);
			const FVector2D Heading = (B - A).GetSafeNormal();
			const FVector2D Right(-Heading.Y, Heading.X);

			// each keeps its place in the herd, drifting a little from side to side
			const float Drift = 110.0f * FMath::Sin((float)FMath::Fmod(Now * 0.37, 1000.0) + Horse.Phase * 40.0f);
			const FVector2D P = FMath::Lerp(A, B, (Where - Herd.Along[Piece]) / Span) + Right * (Horse.Side + Drift);

			// it stands on the ground and leans with the slope it is crossing
			const float Reach = 110.0f * Horse.Scale;
			const float Ahead = FuyuTerrain::Height(P.X + Heading.X * Reach, P.Y + Heading.Y * Reach);
			const float Behind = FuyuTerrain::Height(P.X - Heading.X * Reach, P.Y - Heading.Y * Reach);
			const FRotator Rotation(FMath::RadiansToDegrees(FMath::Atan2(Ahead - Behind, 2.0f * Reach)), FMath::RadiansToDegrees(FMath::Atan2(Heading.Y, Heading.X)), 0.0f);
			const FTransform Transform(Rotation, FVector(P.X, P.Y, (Ahead + Behind) * 0.5f), FVector(Horse.Scale));
			Transforms.Add(Transform);

			if (i == 0)
			{
				Herd.Leader = Transform;
			}
		}

		// the stride only needs sending again while the herd is speeding up or slowing down
		if (!FMath::IsNearlyEqual(Gait, Herd.Gait, 0.004f))
		{
			Herd.Gait = Gait;
			for (int32 i = 0; i < Herd.Count; ++i)
			{
				Horses->SetCustomDataValue(Herd.First + i, 1, Gait, false);
			}
			Horses->MarkRenderStateDirty();
		}
	}

	// a horse a player has taken for her own is no longer with its herd
	HorsePlaces = Transforms;
	HorsesTaken.Reset();
	for (TActorIterator<AFuyuSamurai> It(GetWorld()); It; ++It)
	{
		if (Transforms.IsValidIndex(It->GetTakenHorse()))
		{
			HorsesTaken.Add(It->GetTakenHorse());
			Transforms[It->GetTakenHorse()] = FTransform(FRotator::ZeroRotator, FVector(0.0, 0.0, -100000.0), FVector(0.001));
		}
	}

	Horses->BatchUpdateInstancesTransforms(0, Transforms, true, true, true);
}

bool AFuyuWorld::FindHerdHorse(const FVector& Near, float Reach, int32& OutIndex, FTransform& OutTransform) const
{
	float Best = Reach;
	OutIndex = INDEX_NONE;
	for (int32 i = 0; i < HorsePlaces.Num(); ++i)
	{
		const float Distance = FVector::Dist2D(HorsePlaces[i].GetLocation(), Near);
		if (Distance < Best && !HorsesTaken.Contains(i))
		{
			Best = Distance;
			OutIndex = i;
			OutTransform = HorsePlaces[i];
		}
	}
	return OutIndex != INDEX_NONE;
}
