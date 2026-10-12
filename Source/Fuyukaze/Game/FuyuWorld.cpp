#include "FuyuWorld.h"
#include "FuyuTerrain.h"
#include "ProceduralMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Animation/TransformProviderData.h"
#include "Engine/SkinnedAsset.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Async/ParallelFor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

using FuyuProps::EProp;

namespace
{
	constexpr float GrassCellSize = 1600.0f;
	constexpr int32 TerrainChunks1D = 8;
	constexpr int32 StreakCount = 44;

	/** Deterministic 0..1 hash so the same blade of grass grows in the same place every visit */
	uint32 HashCell(int32 X, int32 Y, uint32 Salt)
	{
		return HashCombine(HashCombine(GetTypeHash(X), GetTypeHash(Y)), Salt);
	}
}

struct FFuyuGrassCell
{
	FIntPoint Coord = FIntPoint::ZeroValue;
	bool bNear = false;
	bool bBuilt = false;
	TMap<uint8, UInstancedStaticMeshComponent*> Components;
};

AFuyuWorld::AFuyuWorld()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	SetRootComponent(Root);

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->Intensity = 8.5f;
	Sun->LightColor = FColor(255, 238, 214);
	Sun->bAtmosphereSunLight = true;
	Sun->ForwardShadingPriority = 1;
	Sun->bEnableLightShaftBloom = true;
	Sun->BloomScale = 0.12f;
	Sun->BloomThreshold = 0.05f;
	Sun->VolumetricScatteringIntensity = 1.6f;
	Sun->SetRelativeRotation(FRotator(-24.0f, 52.0f, 0.0f));

	// moonlight is plain cold light: it does not light the air, or the night sky would turn as blue as the day's
	Moon = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moon"));
	Moon->SetupAttachment(Root);
	Moon->SetMobility(EComponentMobility::Movable);
	Moon->Intensity = 0.0f;
	Moon->LightColor = FColor(150, 182, 255);
	Moon->bAtmosphereSunLight = false;
	Moon->VolumetricScatteringIntensity = 1.2f;
	Moon->SetRelativeRotation(FRotator(-40.0f, 200.0f, 0.0f));
	Moon->SetVisibility(false);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyLight->Intensity = 1.5f;

	// stand-in for light bounced off the golden ground, which the dynamic sky capture cannot see
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->LowerHemisphereColor = FLinearColor(0.2f, 0.15f, 0.08f);

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(Root);
	Atmosphere->MieScatteringScale = 0.012f;

	// distant hills fade into blue haze sooner, which is most of what gives the island its sense of depth
	Atmosphere->AerialPespectiveViewDistanceScale = 2.4f;

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Root);
	Fog->SetRelativeLocation(FVector(0, 0, 300));
	Fog->FogDensity = 0.014f;

	// a second, shallow layer of fog hugs the lowest ground: the lake shore, the coast and the valley floors
	Fog->SecondFogData.FogDensity = 0.035f;
	Fog->SecondFogData.FogHeightFalloff = 1.1f;
	Fog->SecondFogData.FogHeightOffset = 500.0f;
	Fog->FogHeightFalloff = 0.045f;
	Fog->FogMaxOpacity = 0.92f;
	Fog->StartDistance = 1200.0f;
	Fog->bEnableVolumetricFog = true;
	Fog->VolumetricFogScatteringDistribution = 0.72f;
	Fog->VolumetricFogExtinctionScale = 1.4f;
	Fog->VolumetricFogDistance = 9000.0f;

	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(Root);

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(Root);
	PostProcess->bUnbound = true;

	Water = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Water"));
	Water->SetupAttachment(Root);
	Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Water->SetCastShadow(false);
	Water->SetRelativeScale3D(FVector(9000.0f, 9000.0f, 1.0f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		Water->SetStaticMesh(PlaneMesh.Object);
	}
}

AFuyuWorld* AFuyuWorld::Get(const UObject* WorldContext)
{
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull))
	{
		for (TActorIterator<AFuyuWorld> It(World); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

FVector AFuyuWorld::SitePoint(const FFuyuSite& Site, float Forward, float Right)
{
	const FVector F = FRotator(0, Site.Yaw, 0).RotateVector(FVector::ForwardVector);
	const FVector R = FRotator(0, Site.Yaw, 0).RotateVector(FVector::RightVector);
	const FVector P = FVector(Site.Pos.X, Site.Pos.Y, 0) + F * Forward + R * Right;
	return FVector(P.X, P.Y, FuyuTerrain::Height(P.X, P.Y));
}

void AFuyuWorld::BeginPlay()
{
	Super::BeginPlay();

	const double StartTime = FPlatformTime::Seconds();

	WindDirection = FRotator(0, WindHeading, 0).RotateVector(FVector::ForwardVector);
	BuildSky();
	Water->SetMaterial(0, FFuyuMaterials::Get().Water);

	// scanned grass from Fab, if it has been imported: straw-coloured dry grass, then green wild grass
	for (const TCHAR* Letter : { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D"), TEXT("E") })
	{
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Fab/Megascans/Plants/Dry_Grass_tbbqejqr/High/tbbqejqr_tier_1/StaticMeshes/SM_tbbqejqr_Var%s"), Letter), nullptr, LOAD_NoWarn))
		{
			ScannedGrass.Add(Mesh);
		}
	}
	NumDryGrass = ScannedGrass.Num();
	for (const TCHAR* Letter : { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D"), TEXT("E"), TEXT("F"), TEXT("G"), TEXT("H") })
	{
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/High/vlkhcbxia_tier_1/StaticMeshes/SM_vlkhcbxia_Var%s"), Letter), nullptr, LOAD_NoWarn))
		{
			ScannedGrass.Add(Mesh);
		}
	}

	SharedParameters = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Fuyukaze/Materials/MPC_Fuyu.MPC_Fuyu"), nullptr, LOAD_NoWarn);

	BuildTerrain();
	BuildScatter();
	BuildSites();
	BuildScenery();
	BuildHorses();

	// drifting leaves and the guiding-wind ribbons are two small pools of instances moved by hand each frame
	Leaves = NewObject<UInstancedStaticMeshComponent>(this);
	Leaves->SetStaticMesh(FuyuProps::Get(EProp::Leaf));
	Leaves->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Leaves->SetCastShadow(false);
	Leaves->SetNumCustomDataFloats(3);
	Leaves->SetMobility(EComponentMobility::Movable);
	Leaves->SetupAttachment(Root);
	Leaves->RegisterComponent();

	Streaks = NewObject<UInstancedStaticMeshComponent>(this);
	Streaks->SetStaticMesh(FuyuProps::Get(EProp::WindStreak));
	Streaks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Streaks->SetCastShadow(false);
	Streaks->SetMobility(EComponentMobility::Movable);
	Streaks->SetupAttachment(Root);
	Streaks->RegisterComponent();

	FRandomStream Rng(4242);
	for (int32 i = 0; i < StreakCount; ++i)
	{
		FStreak S;
		S.Pos = FVector::ZeroVector;
		S.Age = 1.0f;
		S.Life = 0.0f;
		S.Phase = Rng.FRandRange(0.0f, 2.0f * PI);
		S.Length = Rng.FRandRange(350.0f, 800.0f);
		StreakState.Add(S);
		Streaks->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000), FVector(0.001f)), true);
	}

	// combat effects: two rings of hand-animated instances, and a few crescents for sword cuts
	UInstancedStaticMeshComponent* NewSparks = nullptr;
	UInstancedStaticMeshComponent* NewBurst = nullptr;
	InitPool(Sparks, NewSparks, EProp::Spark, 260);
	InitPool(BurstLeaves, NewBurst, EProp::Leaf, 180);
	SparkMesh = NewSparks;
	BurstMesh = NewBurst;

	for (int32 i = 0; i < 6; ++i)
	{
		UStaticMeshComponent* Arc = NewObject<UStaticMeshComponent>(this);
		Arc->SetStaticMesh(FuyuProps::Get(EProp::SlashArc));
		Arc->SetMobility(EComponentMobility::Movable);
		Arc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Arc->SetCastShadow(false);
		Arc->SetVisibility(false);
		Arc->SetupAttachment(Root);
		Arc->RegisterComponent();
		SlashMeshes.Add(Arc);
		SlashMaterials.Add(Arc->CreateDynamicMaterialInstance(0));
		SlashState.AddDefaulted();
	}

	for (int32 i = 0; i < 12; ++i)
	{
		UStaticMeshComponent* Line = NewObject<UStaticMeshComponent>(this);
		Line->SetStaticMesh(FuyuProps::Get(EProp::TrailLine));
		Line->SetMobility(EComponentMobility::Movable);
		Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Line->SetCastShadow(false);
		Line->SetVisibility(false);
		Line->SetupAttachment(Root);
		Line->RegisterComponent();
		TrailMeshes.Add(Line);
		TrailMaterials.Add(Line->CreateDynamicMaterialInstance(0));
		TrailState.AddDefaulted();
	}

	ApplyGrade();

	const FFuyuSite* Start = FuyuTerrain::FindSite(TEXT("Start"));
	UpdateGrass(Start ? FuyuTerrain::Ground(Start->Pos) : FVector::ZeroVector, true);

	UE_LOG(LogTemp, Log, TEXT("Fuyukaze: world built in %.2f s"), FPlatformTime::Seconds() - StartTime);
}

void AFuyuWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	GrassCells.Empty();
	GrassPool.Empty();
	FuyuProps::Reset();
}

void AFuyuWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	FVector ViewLocation = FVector::ZeroVector;
	if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		ViewLocation = Camera->GetCameraLocation();
	}

	UpdateSky(DeltaSeconds, ViewLocation);
	UpdateGrass(ViewLocation, false);
	UpdateLeaves(DeltaSeconds, ViewLocation);
	UpdateStreaks(DeltaSeconds, ViewLocation);
	UpdateEffects(DeltaSeconds);
	UpdateBirds(DeltaSeconds);
	UpdateHorses();

	// tell the grass and flower materials where the samurai is standing, so blades lean away from her legs.
	// XYZ is the point between her feet and W the radius of the effect; with no pawn it is parked far away
	if (SharedParameters)
	{
		FLinearColor Feet(0.0f, 0.0f, -1000000.0f, GrassPartRadius);
		if (const ACharacter* Player = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
		{
			const FVector P = Player->GetActorLocation() - FVector(0, 0, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			Feet = FLinearColor(P.X, P.Y, P.Z, GrassPartRadius);
		}
		UKismetMaterialLibrary::SetVectorParameterValue(this, SharedParameters, TEXT("PlayerFeet"), Feet);
		UKismetMaterialLibrary::SetScalarParameterValue(this, SharedParameters, TEXT("GrassPartRadius"), GrassPartRadius);
	}
}

void AFuyuWorld::InitPool(FParticlePool& Pool, UInstancedStaticMeshComponent*& Component, EProp Prop, int32 Count)
{
	Component = NewObject<UInstancedStaticMeshComponent>(this);
	Component->SetStaticMesh(FuyuProps::Get(Prop));
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);
	Component->SetNumCustomDataFloats(3);
	Component->SetupAttachment(Root);
	Component->RegisterComponent();

	Pool.Particles.SetNum(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		Component->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000), FVector(0.001f)), true);
	}
}

int32 AFuyuWorld::Emit(FParticlePool& Pool, UInstancedStaticMeshComponent* Component, const FParticle& Particle, const FLinearColor& Color)
{
	const int32 Index = Pool.Next;
	Pool.Next = (Pool.Next + 1) % Pool.Particles.Num();
	Pool.Particles[Index] = Particle;
	Pool.bDirty = true;

	const float Data[3] = { Color.R, Color.G, Color.B };
	Component->SetCustomData(Index, MakeArrayView(Data, 3), false);
	return Index;
}

void AFuyuWorld::SpawnSparks(const FVector& Location, const FVector& Direction, const FLinearColor& Color, int32 Count, float Speed)
{
	if (!SparkMesh)
	{
		return;
	}

	for (int32 i = 0; i < Count; ++i)
	{
		FParticle P;
		P.Pos = Location + FMath::VRand() * 8.0f;

		// most fly on with the blow, a few scatter wide, and all are thrown slightly upward
		const FVector Spread = FMath::VRandCone(Direction, FMath::DegreesToRadians(i % 4 == 0 ? 85.0f : 38.0f));
		P.Vel = Spread * Speed * FMath::FRandRange(0.35f, 1.0f) + FVector(0, 0, Speed * 0.22f);
		P.Age = 0.0f;
		P.Life = FMath::FRandRange(0.16f, 0.42f);
		P.Size = FMath::FRandRange(0.6f, 1.5f);
		Emit(Sparks, SparkMesh, P, Color);
	}
}

void AFuyuWorld::SpawnLeafBurst(const FVector& Location, int32 Count)
{
	if (!BurstMesh)
	{
		return;
	}

	for (int32 i = 0; i < Count; ++i)
	{
		FParticle P;
		P.Pos = Location + FMath::VRand() * FMath::FRandRange(10.0f, 45.0f);

		// leaves erupt outward and up, then the wind takes them
		FVector Out = FMath::VRand();
		Out.Z = FMath::Abs(Out.Z) * 0.9f + 0.25f;
		P.Vel = Out.GetSafeNormal() * FMath::FRandRange(250.0f, 720.0f) + WindDirection * 120.0f;
		P.Axis = FMath::VRand();
		P.Age = 0.0f;
		P.Life = FMath::FRandRange(0.9f, 1.9f);
		P.Size = FMath::FRandRange(1.3f, 2.6f);

		const FLinearColor Color = FMath::FRand() < 0.6f ? FLinearColor(1.0f, 0.68f, 0.06f) : FLinearColor(0.85f, 0.1f, 0.03f);
		Emit(BurstLeaves, BurstMesh, P, Color);
	}
}

void AFuyuWorld::SpawnSlash(const FVector& Origin, const FVector& Forward, float RollDegrees, float Size, const FLinearColor& Color)
{
	if (SlashMeshes.Num() == 0)
	{
		return;
	}

	NextSlash = (NextSlash + 1) % SlashMeshes.Num();
	FSlash& Slash = SlashState[NextSlash];
	Slash.Age = 0.0f;
	Slash.Life = 0.2f;
	Slash.Size = Size;

	// the crescent lies in the plane of the cut: flat for a sweep, rolled upright for an overhead blow
	FRotator Rotation = Forward.GetSafeNormal2D().Rotation();
	Rotation.Roll = RollDegrees;

	UStaticMeshComponent* Arc = SlashMeshes[NextSlash];
	Arc->SetWorldLocationAndRotation(Origin, Rotation);
	Arc->SetWorldScale3D(FVector(Size * 0.7f));
	Arc->SetVisibility(true);

	if (UMaterialInstanceDynamic* Mat = SlashMaterials[NextSlash])
	{
		Mat->SetVectorParameterValue(TEXT("Tint"), Color);
		Mat->SetScalarParameterValue(TEXT("Fade"), 1.0f);
	}
}

void AFuyuWorld::SpawnTrail(const FVector& A, const FVector& B, const FLinearColor& Color, float Life)
{
	if (TrailMeshes.Num() == 0 || FVector::DistSquared(A, B) < 1.0f)
	{
		return;
	}

	NextTrail = (NextTrail + 1) % TrailMeshes.Num();
	FSlash& Trail = TrailState[NextTrail];
	Trail.Age = 0.0f;
	Trail.Life = Life;
	Trail.Size = FVector::Dist(A, B) / 100.0f;

	// the mesh is a metre long along X, so stretch it to span the two points and thicken it a little
	UStaticMeshComponent* Line = TrailMeshes[NextTrail];
	Line->SetWorldLocationAndRotation(A, (B - A).Rotation());
	Line->SetWorldScale3D(FVector(Trail.Size, 14.0f, 14.0f));
	Line->SetVisibility(true);

	if (UMaterialInstanceDynamic* Mat = TrailMaterials[NextTrail])
	{
		Mat->SetVectorParameterValue(TEXT("Tint"), Color);
		Mat->SetScalarParameterValue(TEXT("Fade"), 1.0f);
	}
}

void AFuyuWorld::PunchScreen(float Strength)
{
	ScreenPunch = FMath::Max(ScreenPunch, FMath::Clamp(Strength, 0.0f, 1.0f));
}

void AFuyuWorld::UpdateEffects(float DeltaSeconds)
{
	// sparks: fast, heavy and short-lived, stretched along their path
	if (Sparks.bDirty && SparkMesh)
	{
		TArray<FTransform> Transforms;
		Transforms.Reserve(Sparks.Particles.Num());
		bool bAnyAlive = false;

		for (FParticle& P : Sparks.Particles)
		{
			P.Age += DeltaSeconds;
			if (P.Age >= P.Life)
			{
				Transforms.Add(FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000), FVector(0.001f)));
				continue;
			}

			bAnyAlive = true;
			P.Vel.Z -= 1500.0f * DeltaSeconds;
			P.Vel *= FMath::Exp(-2.2f * DeltaSeconds);
			P.Pos += P.Vel * DeltaSeconds;

			const float T = P.Age / P.Life;
			const float Stretch = FMath::Clamp(P.Vel.Size() / 260.0f, 0.6f, 5.0f);
			Transforms.Add(FTransform(P.Vel.Rotation(), P.Pos, FVector(Stretch, 1.0f, 1.0f) * P.Size * (1.0f - T * T)));
		}

		SparkMesh->BatchUpdateInstancesTransforms(0, Transforms, true, true, true);
		Sparks.bDirty = bAnyAlive;
	}

	// burst leaves: light and slow, tumbling as they settle
	if (BurstLeaves.bDirty && BurstMesh)
	{
		TArray<FTransform> Transforms;
		Transforms.Reserve(BurstLeaves.Particles.Num());
		bool bAnyAlive = false;

		for (FParticle& P : BurstLeaves.Particles)
		{
			P.Age += DeltaSeconds;
			if (P.Age >= P.Life)
			{
				Transforms.Add(FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000), FVector(0.001f)));
				continue;
			}

			bAnyAlive = true;
			P.Vel.Z -= 380.0f * DeltaSeconds;
			P.Vel *= FMath::Exp(-1.6f * DeltaSeconds);
			P.Pos += (P.Vel + WindDirection * 160.0f) * DeltaSeconds;

			const float T = P.Age / P.Life;
			const float Fade = FMath::Min(1.0f, (1.0f - T) * 4.0f);
			Transforms.Add(FTransform(FQuat(P.Axis, P.Age * 9.0f), P.Pos, FVector(P.Size * Fade)));
		}

		BurstMesh->BatchUpdateInstancesTransforms(0, Transforms, true, true, true);
		BurstLeaves.bDirty = bAnyAlive;
	}

	// crescents flare, widen and are gone in a fifth of a second
	for (int32 i = 0; i < SlashState.Num(); ++i)
	{
		FSlash& Slash = SlashState[i];
		if (Slash.Age >= Slash.Life)
		{
			continue;
		}

		Slash.Age += DeltaSeconds;
		const float T = FMath::Clamp(Slash.Age / Slash.Life, 0.0f, 1.0f);
		SlashMeshes[i]->SetWorldScale3D(FVector(Slash.Size * (0.7f + 0.45f * FMath::Sqrt(T))));

		if (UMaterialInstanceDynamic* Mat = SlashMaterials[i])
		{
			Mat->SetScalarParameterValue(TEXT("Fade"), FMath::Square(1.0f - T));
		}

		if (T >= 1.0f)
		{
			SlashMeshes[i]->SetVisibility(false);
		}
	}

	// trails burn bright, then thin and fade
	for (int32 i = 0; i < TrailState.Num(); ++i)
	{
		FSlash& Trail = TrailState[i];
		if (Trail.Age >= Trail.Life)
		{
			continue;
		}

		Trail.Age += DeltaSeconds;
		const float T = FMath::Clamp(Trail.Age / Trail.Life, 0.0f, 1.0f);
		const float Width = FMath::Lerp(22.0f, 3.0f, FMath::Sqrt(T));
		TrailMeshes[i]->SetWorldScale3D(FVector(Trail.Size, Width, Width));

		if (UMaterialInstanceDynamic* Mat = TrailMaterials[i])
		{
			Mat->SetScalarParameterValue(TEXT("Fade"), FMath::Square(1.0f - T) * 1.6f);
		}

		if (T >= 1.0f)
		{
			TrailMeshes[i]->SetVisibility(false);
		}
	}

	// the ultimate's focus drains the colour out of the world and closes the frame in around the samurai
	if (!FMath::IsNearlyEqual(FocusGrade, AppliedFocus, 0.002f) || FocusGrade > 0.0f)
	{
		AppliedFocus = FocusGrade;
		ApplyGrade();

		FPostProcessSettings& S = PostProcess->Settings;
		const FVector4 Cold(0.1, 0.13, 0.22, 1.0);
		S.ColorSaturation = FMath::Lerp(S.ColorSaturation, Cold, (double)FocusGrade);
		S.ColorContrast = S.ColorContrast * (1.0 + 0.22 * FocusGrade);
		S.VignetteIntensity += 0.2f * FocusGrade;
		S.FilmGrainIntensity += 0.25f * FocusGrade;

		// a cold cast over what colour is left, and no motion blur, so the fast camera cuts stay crisp
		S.ColorGain = FMath::Lerp(S.ColorGain, FVector4(0.9, 1.0, 1.22, 1.0), (double)FocusGrade);
		S.MotionBlurAmount = 0.22f * (1.0f - FocusGrade);
	}

	// the screen punch rides on top of the colour grade and decays in about a quarter of a second
	if (ScreenPunch > 0.0f || PostProcess->Settings.SceneFringeIntensity > 0.0f)
	{
		ScreenPunch = FMath::Max(ScreenPunch - DeltaSeconds * 5.5f, 0.0f);

		FPostProcessSettings& S = PostProcess->Settings;
		S.bOverride_SceneFringeIntensity = true;
		S.SceneFringeIntensity = 2.2f * ScreenPunch;
		S.BloomIntensity = 0.9f + 0.9f * ScreenPunch;
		S.VignetteIntensity = (bCinemaMode ? 0.75f : 0.42f) + 0.35f * ScreenPunch + 0.2f * FocusGrade;
	}
}

void AFuyuWorld::BuildTerrain()
{
	using namespace FuyuTerrain;

	const int32 N = Cells + 1;

	// make sure the site table exists before worker threads start asking for heights
	GetSites();

	// sample the whole island once, chunks then share these arrays so their seams match exactly
	TArray<float> Heights;
	TArray<FVector> Normals;
	TArray<FLinearColor> VertexColors;
	Heights.SetNumUninitialized(N * N);
	Normals.SetNumUninitialized(N * N);
	VertexColors.SetNumUninitialized(N * N);

	ParallelFor(N, [&](int32 j)
	{
		for (int32 i = 0; i < N; ++i)
		{
			Heights[j * N + i] = Height(-HalfSize + i * Step, -HalfSize + j * Step);
		}
	});

	ParallelFor(N, [&](int32 j)
	{
		for (int32 i = 0; i < N; ++i)
		{
			const int32 I0 = FMath::Max(i - 1, 0), I1 = FMath::Min(i + 1, N - 1);
			const int32 J0 = FMath::Max(j - 1, 0), J1 = FMath::Min(j + 1, N - 1);
			const float DX = (Heights[j * N + I1] - Heights[j * N + I0]) / ((I1 - I0) * Step);
			const float DY = (Heights[J1 * N + i] - Heights[J0 * N + i]) / ((J1 - J0) * Step);
			const FVector Nrm = FVector(-DX, -DY, 1.0f).GetSafeNormal();
			Normals[j * N + i] = Nrm;

			const float X = -HalfSize + i * Step, Y = -HalfSize + j * Step;
			VertexColors[j * N + i] = GroundColor(X, Y, Heights[j * N + i], Nrm.Z);
		}
	});

	const int32 ChunkCells = Cells / TerrainChunks1D;
	const int32 CN = ChunkCells + 1;

	for (int32 CY = 0; CY < TerrainChunks1D; ++CY)
	{
		for (int32 CX = 0; CX < TerrainChunks1D; ++CX)
		{
			TArray<FVector> Verts;
			TArray<FVector> Nrms;
			TArray<FVector2D> UV;
			TArray<FLinearColor> Cols;
			TArray<FProcMeshTangent> Tangents;
			TArray<int32> Tris;
			Verts.Reserve(CN * CN);
			Tris.Reserve(ChunkCells * ChunkCells * 6);

			for (int32 j = 0; j < CN; ++j)
			{
				for (int32 i = 0; i < CN; ++i)
				{
					const int32 GI = CX * ChunkCells + i, GJ = CY * ChunkCells + j;
					const float X = -HalfSize + GI * Step, Y = -HalfSize + GJ * Step;
					Verts.Add(FVector(X, Y, Heights[GJ * N + GI]));
					Nrms.Add(Normals[GJ * N + GI]);
					UV.Add(FVector2D(X, Y) / 1000.0);
					Cols.Add(VertexColors[GJ * N + GI]);
					Tangents.Add(FProcMeshTangent(1, 0, 0));
				}
			}

			for (int32 j = 0; j < ChunkCells; ++j)
			{
				for (int32 i = 0; i < ChunkCells; ++i)
				{
					const int32 V00 = j * CN + i, V10 = V00 + 1, V01 = V00 + CN, V11 = V01 + 1;
					Tris.Append({ V00, V01, V10, V10, V01, V11 });
				}
			}

			UProceduralMeshComponent* Chunk = NewObject<UProceduralMeshComponent>(this);
			Chunk->bUseAsyncCooking = false;
			Chunk->SetupAttachment(Root);
			Chunk->RegisterComponent();
			Chunk->CreateMeshSection_LinearColor(0, Verts, Tris, Nrms, UV, Cols, Tangents, true, true);
			Chunk->SetMaterial(0, FFuyuMaterials::Get().Terrain);
			Chunk->SetCollisionProfileName(TEXT("BlockAll"));
			TerrainChunks.Add(Chunk);
		}
	}

	// the far side of the mountain lies outside the playable square. It is only ever seen from a distance,
	// so it gets one coarse mesh with no collision, tucked slightly under the main terrain to hide the seam
	{
		const float CoarseStep = Step * 4.0f;
		const float X0 = HalfSize - CoarseStep;
		const int32 NX = FMath::CeilToInt32((BackdropDepth + CoarseStep) / CoarseStep) + 1;
		const int32 NY = FMath::RoundToInt32((HalfSize * 2.0f) / CoarseStep) + 1;

		TArray<FVector> Verts;
		TArray<FVector> Nrms;
		TArray<FVector2D> UV;
		TArray<FLinearColor> Cols;
		TArray<FProcMeshTangent> Tangents;
		TArray<int32> Tris;
		Verts.SetNumUninitialized(NX * NY);
		Nrms.SetNumUninitialized(NX * NY);
		UV.SetNumUninitialized(NX * NY);
		Cols.SetNumUninitialized(NX * NY);
		Tangents.Init(FProcMeshTangent(1, 0, 0), NX * NY);

		ParallelFor(NY, [&](int32 j)
		{
			for (int32 i = 0; i < NX; ++i)
			{
				const float X = X0 + i * CoarseStep, Y = -HalfSize + j * CoarseStep;
				const float H = Height(X, Y);
				const FVector Nrm = Normal(X, Y);
				Verts[j * NX + i] = FVector(X, Y, H - 260.0f);
				Nrms[j * NX + i] = Nrm;
				UV[j * NX + i] = FVector2D(X, Y) / 1000.0;
				Cols[j * NX + i] = GroundColor(X, Y, H, Nrm.Z);
			}
		});

		for (int32 j = 0; j < NY - 1; ++j)
		{
			for (int32 i = 0; i < NX - 1; ++i)
			{
				const int32 V00 = j * NX + i, V10 = V00 + 1, V01 = V00 + NX, V11 = V01 + 1;
				Tris.Append({ V00, V01, V10, V10, V01, V11 });
			}
		}

		UProceduralMeshComponent* Backdrop = NewObject<UProceduralMeshComponent>(this);
		Backdrop->SetupAttachment(Root);
		Backdrop->RegisterComponent();
		Backdrop->CreateMeshSection_LinearColor(0, Verts, Tris, Nrms, UV, Cols, Tangents, false, true);
		Backdrop->SetMaterial(0, FFuyuMaterials::Get().Terrain);
		Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TerrainChunks.Add(Backdrop);
	}
}

UHierarchicalInstancedStaticMeshComponent* AFuyuWorld::GetScatter(EProp Prop)
{
	if (TObjectPtr<UHierarchicalInstancedStaticMeshComponent>* Found = Scatter.Find((uint8)Prop))
	{
		return *Found;
	}

	const bool bTree = Prop == EProp::Pine || Prop == EProp::Ginkgo || Prop == EProp::Maple;

	UHierarchicalInstancedStaticMeshComponent* Comp = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	Comp->SetStaticMesh(FuyuProps::Get(Prop));
	Comp->SetMobility(EComponentMobility::Static);
	Comp->SetCollisionProfileName(TEXT("BlockAll"));
	Comp->SetCanEverAffectNavigation(false);

	// small rocks stop drawing early, trees stay visible to the horizon because they define the skyline
	if (Prop == EProp::Rock)
	{
		Comp->SetCullDistances(0, 14000);
		Comp->SetCastShadow(false);
	}
	else if (bTree)
	{
		Comp->SetCullDistances(0, 150000);
	}

	Comp->SetupAttachment(Root);
	Comp->RegisterComponent();
	Scatter.Add((uint8)Prop, Comp);
	return Comp;
}

void AFuyuWorld::Place(EProp Prop, const FVector& Location, float Yaw, float Scale, bool bSnapToGround)
{
	FVector P = Location;
	if (bSnapToGround)
	{
		P.Z = FuyuTerrain::Height(P.X, P.Y);
	}
	GetScatter(Prop)->AddInstance(FTransform(FRotator(0, Yaw, 0), P, FVector(Scale)), true);

	if (UInstancedSkinnedMeshComponent** Scanned = ScannedBySpecies.Find((uint8)Prop))
	{
		(*Scanned)->AddInstance(FTransform(FRotator(0, Yaw, 0), P, FVector(Scale * ScannedTreeScale)), 0, true);
	}
}

UStaticMeshComponent* AFuyuWorld::PlaceUnique(EProp Prop, const FVector& Location, float Yaw, bool bCollide)
{
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetStaticMesh(FuyuProps::Get(Prop));
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetCollisionProfileName(bCollide ? TEXT("BlockAll") : TEXT("NoCollision"));
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetupAttachment(Root);
	Comp->RegisterComponent();
	Comp->SetWorldLocationAndRotation(FVector(Location.X, Location.Y, FuyuTerrain::Height(Location.X, Location.Y)), FRotator(0, Yaw, 0));
	return Comp;
}

void AFuyuWorld::BuildScatter()
{
	using namespace FuyuTerrain;

	FRandomStream Rng(20261011);
	TMap<uint8, TArray<FTransform>> Batches;

	// trees: walk a jittered grid and keep a candidate where the forest mask says so
	const float TreeSpacing = 1250.0f;
	const int32 TreeN = FMath::FloorToInt32((HalfSize * 2.0f) / TreeSpacing);
	for (int32 j = 0; j < TreeN; ++j)
	{
		for (int32 i = 0; i < TreeN; ++i)
		{
			const float X = -HalfSize + (i + Rng.FRand()) * TreeSpacing;
			const float Y = -HalfSize + (j + Rng.FRand()) * TreeSpacing;
			const float Roll = Rng.FRand();
			const float Kind = Rng.FRand();
			const float Yaw = Rng.FRandRange(0.0f, 360.0f);
			const float Scale = Rng.FRandRange(0.85f, 1.45f);

			const float Forest = ForestMask(X, Y);
			if (Roll > FMath::Pow(Forest, 1.2f) * 0.9f + 0.014f)
			{
				continue;
			}

			if (!IsOpenGround(X, Y, 31.0f, 500.0f) || PathDistance(X, Y) < 520.0f)
			{
				continue;
			}

			const float H = Height(X, Y);
			if (H > RockLine + 2500.0f)
			{
				continue;
			}

			// species cluster into groves: gold ginkgo and red maple on the lowlands, pine taking over uphill
			const float Grove = FMath::PerlinNoise2D(FVector2D(X, Y) / 11000.0f + FVector2D(55.5f, 12.25f));
			const float PineChance = FMath::Clamp(0.14f + (H - 3500.0f) / 9000.0f, 0.12f, 0.97f);
			EProp Species = EProp::Pine;
			if (Kind > PineChance)
			{
				Species = Grove > 0.02f ? EProp::Ginkgo : EProp::Maple;
			}

			Batches.FindOrAdd((uint8)Species).Add(FTransform(FRotator(0, Yaw, 0), FVector(X, Y, H - 20.0f), FVector(Scale)));
		}
	}

	// rocks gather on slopes and high ground, with the odd stray on the plains
	const float RockSpacing = 2100.0f;
	const int32 RockN = FMath::FloorToInt32((HalfSize * 2.0f) / RockSpacing);
	for (int32 j = 0; j < RockN; ++j)
	{
		for (int32 i = 0; i < RockN; ++i)
		{
			const float X = -HalfSize + (i + Rng.FRand()) * RockSpacing;
			const float Y = -HalfSize + (j + Rng.FRand()) * RockSpacing;
			const float Roll = Rng.FRand();
			const float Yaw = Rng.FRandRange(0.0f, 360.0f);
			const float Scale = Rng.FRandRange(0.6f, 1.9f);
			const bool bBig = Rng.FRand() < 0.2f;

			const float H = Height(X, Y);
			if (H < 120.0f || !IsOpenGround(X, Y, 50.0f, 300.0f) || PathDistance(X, Y) < 380.0f)
			{
				continue;
			}

			const float Slope = 1.0f - Normal(X, Y).Z;
			const float Chance = 0.07f + Slope * 3.2f + FMath::SmoothStep(RockLine - 4000.0f, RockLine + 2000.0f, H) * 0.5f;
			if (Roll > Chance)
			{
				continue;
			}

			Batches.FindOrAdd((uint8)(bBig ? EProp::Boulder : EProp::Rock)).Add(FTransform(FRotator(Rng.FRandRange(-12.0f, 12.0f), Yaw, 0), FVector(X, Y, H - 8.0f), FVector(Scale)));
		}
	}

	int32 Total = 0;
	for (TPair<uint8, TArray<FTransform>>& Batch : Batches)
	{
		UHierarchicalInstancedStaticMeshComponent* Stylised = GetScatter((EProp)Batch.Key);
		Stylised->AddInstances(Batch.Value, false, true);
		Total += Batch.Value.Num();

		// where scanned trees are available they are drawn instead; the stylised instances stay on, unseen,
		// because they carry the trunk collision
		if (PlaceScannedTrees((EProp)Batch.Key, Batch.Value))
		{
			Stylised->SetVisibility(false);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Fuyukaze: scattered %d trees and rocks"), Total);
}

UInstancedSkinnedMeshComponent* AFuyuWorld::PlaceMegaplant(const TCHAR* Folder, const TArray<FTransform>& Transforms, const FLinearColor* LeafTint)
{
	// each Megaplant ships several grown variants, lettered from A
	TArray<USkinnedAsset*> Variants;
	for (const TCHAR* Letter : { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D") })
	{
		const FString Path = FString::Printf(TEXT("/Game/Megaplant_Library/%s_%s"), Folder, Letter);
		if (USkinnedAsset* Asset = LoadObject<USkinnedAsset>(nullptr, *Path, nullptr, LOAD_NoWarn))
		{
			Variants.Add(Asset);
		}
	}

	if (Variants.Num() == 0)
	{
		return nullptr;
	}

	// the wind provider that ships with the vegetation plugin drives the trees' bone-based sway
	UTransformProviderData* Wind = LoadObject<UTransformProviderData>(nullptr, TEXT("/ProceduralVegetationEditor/SampleAssets/Materials/GlobalFoliageActor/Wind_TransformProvider.Wind_TransformProvider"), nullptr, LOAD_NoWarn);

	TArray<TArray<FTransform>> PerVariant;
	PerVariant.SetNum(Variants.Num());
	for (int32 i = 0; i < Transforms.Num(); ++i)
	{
		FTransform T = Transforms[i];
		T.SetScale3D(T.GetScale3D() * ScannedTreeScale);
		PerVariant[i % Variants.Num()].Add(T);
	}

	UInstancedSkinnedMeshComponent* First = nullptr;
	for (int32 v = 0; v < Variants.Num(); ++v)
	{
		UInstancedSkinnedMeshComponent* Comp = NewObject<UInstancedSkinnedMeshComponent>(this);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetupAttachment(Root);
		Comp->SetSkinnedAssetAndUpdate(Variants[v]);
		if (Wind)
		{
			Comp->SetTransformProvider(Wind);
		}
		Comp->RegisterComponent();
		Comp->SetCullDistances(FMath::RoundToInt32(ScannedTreeDrawDistance * 0.85f), FMath::RoundToInt32(ScannedTreeDrawDistance));

		// the scans are of trees in summer leaf; a tint shifts them to autumn colour or to blossom
		if (LeafTint)
		{
			for (int32 m = 0; m < Comp->GetNumMaterials(); ++m)
			{
				UMaterialInterface* Mat = Comp->GetMaterial(m);
				if (Mat && Mat->GetName().Contains(TEXT("Foliage")))
				{
					UMaterialInstanceDynamic* Dyn = Comp->CreateDynamicMaterialInstance(m, Mat);
					Dyn->SetVectorParameterValue(TEXT("BaseColor Tint Leaves"), *LeafTint);
					Dyn->SetVectorParameterValue(TEXT("Translucency Tint Leaves"), *LeafTint);
				}
			}
		}

		TArray<int32> AnimationIndices;
		AnimationIndices.Init(0, PerVariant[v].Num());
		Comp->AddInstances(PerVariant[v], AnimationIndices, false, true);
		ScannedTrees.Add(Comp);
		First = First ? First : Comp;
	}

	UE_LOG(LogTemp, Log, TEXT("Fuyukaze: %d scanned plants placed from %s (%d variants)"), Transforms.Num(), Folder, Variants.Num());
	return First;
}

bool AFuyuWorld::PlaceScannedTrees(EProp Species, const TArray<FTransform>& Transforms)
{
	// gold for ginkgo and red for maple; the spruce keeps its own green
	const FLinearColor Gold(3.2f, 2.7f, 0.1f), Red(5.5f, 0.5f, 0.2f);

	UInstancedSkinnedMeshComponent* Comp = nullptr;
	switch (Species)
	{
	case EProp::Ginkgo: Comp = PlaceMegaplant(TEXT("Tree_Ginkgo/Tree_Ginkgo_01/Tree_Ginkgo_01"), Transforms, &Gold); break;
	case EProp::Maple:  Comp = PlaceMegaplant(TEXT("Tree_Norway_Maple/Tree_Norway_Maple_Forest_01/Tree_Norway_Maple_Forest_01"), Transforms, &Red); break;
	case EProp::Pine:   Comp = PlaceMegaplant(TEXT("Tree_Norway_Spruce/Tree_Norway_Spruce_01/Tree_Norway_Spruce_01"), Transforms, nullptr); break;
	default: break;
	}

	if (Comp)
	{
		ScannedBySpecies.Add((uint8)Species, Comp);
	}
	return Comp != nullptr;
}

void AFuyuWorld::PlaceGrove(const TCHAR* Folder, const FVector2D& Center, float Radius, int32 Count, float MinScale, float MaxScale, const FLinearColor* LeafTint, int32 Seed)
{
	FRandomStream Rng(Seed);
	TArray<FTransform> Transforms;

	for (int32 i = 0; i < Count; ++i)
	{
		// denser towards the middle, and kept off the trails
		const float Angle = Rng.FRandRange(0.0f, 2.0f * PI);
		const float Dist = Radius * FMath::Sqrt(Rng.FRand()) * Rng.FRandRange(0.5f, 1.0f);
		const float X = Center.X + FMath::Cos(Angle) * Dist, Y = Center.Y + FMath::Sin(Angle) * Dist;
		const float Scale = Rng.FRandRange(MinScale, MaxScale);
		const float Yaw = Rng.FRandRange(0.0f, 360.0f);

		if (FuyuTerrain::PathDistance(X, Y) < 320.0f || FuyuTerrain::Height(X, Y) < 200.0f)
		{
			continue;
		}

		Transforms.Add(FTransform(FRotator(0, Yaw, 0), FVector(X, Y, FuyuTerrain::Height(X, Y) - 15.0f), FVector(Scale)));
	}

	PlaceMegaplant(Folder, Transforms, LeafTint);
}

void AFuyuWorld::AddMist(const FVector& Location, float Size, float Yaw)
{
	UStaticMeshComponent* Sheet = NewObject<UStaticMeshComponent>(this);
	Sheet->SetStaticMesh(Water->GetStaticMesh());
	Sheet->SetMaterial(0, FFuyuMaterials::Get().Mist);
	Sheet->SetMobility(EComponentMobility::Movable);
	Sheet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sheet->SetCastShadow(false);
	Sheet->SetupAttachment(Root);
	Sheet->RegisterComponent();
	Sheet->SetWorldLocationAndRotation(Location, FRotator(0, Yaw, 0));
	Sheet->SetWorldScale3D(FVector(Size / 100.0f, Size / 100.0f, 1.0f));
	MistSheets.Add(Sheet);
}

void AFuyuWorld::AddFlock(const FVector& Center, float Radius, float HeightMin, float HeightMax, int32 Count, float Speed, float Scale, const FLinearColor& Color)
{
	if (!Birds)
	{
		Birds = NewObject<UInstancedStaticMeshComponent>(this);
		Birds->SetStaticMesh(FuyuProps::Get(EProp::Bird));
		Birds->SetMobility(EComponentMobility::Movable);
		Birds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Birds->SetCastShadow(false);
		Birds->SetNumCustomDataFloats(3);
		Birds->SetupAttachment(Root);
		Birds->RegisterComponent();
	}

	FFlock Flock;
	Flock.Center = Center;
	Flock.Radius = Radius;
	Flock.HeightMin = HeightMin;
	Flock.HeightMax = HeightMax;
	Flock.First = Birds->GetInstanceCount();
	Flock.Count = Count;
	Flock.Speed = Speed;
	Flock.Scale = Scale;
	Flocks.Add(Flock);

	const float Data[3] = { Color.R, Color.G, Color.B };
	for (int32 i = 0; i < Count; ++i)
	{
		const int32 Index = Birds->AddInstance(FTransform(Center), true);
		Birds->SetCustomData(Index, MakeArrayView(Data, 3), false);
	}
}

void AFuyuWorld::UpdateBirds(float DeltaSeconds)
{
	if (!Birds || Flocks.Num() == 0)
	{
		return;
	}

	const float Time = GetWorld()->GetTimeSeconds();
	TArray<FTransform> Transforms;
	Transforms.Reserve(Birds->GetInstanceCount());

	for (const FFlock& Flock : Flocks)
	{
		for (int32 i = 0; i < Flock.Count; ++i)
		{
			// every bird wheels round the flock's centre on its own ring, at its own height and pace.
			// These are fixed per bird, so no state needs storing: position is a pure function of time
			FRandomStream Rng(Flock.First * 131 + i * 7 + 1);
			const float Ring = Flock.Radius * FMath::Lerp(0.25f, 1.0f, FMath::Sqrt(Rng.FRand()));
			const float Height = FMath::Lerp(Flock.HeightMin, Flock.HeightMax, Rng.FRand());
			const float Phase = Rng.FRandRange(0.0f, 2.0f * PI);
			const float Pace = Rng.FRandRange(0.75f, 1.25f);
			const float Turn = Rng.FRand() < 0.85f ? 1.0f : -1.0f;

			const float Angle = Phase + Turn * Time * Flock.Speed * Pace / Ring;
			const float Wander = FMath::Sin(Time * 0.31f * Pace + Phase * 3.0f);
			const FVector Offset(FMath::Cos(Angle) * Ring * (1.0f + 0.12f * Wander), FMath::Sin(Angle) * Ring * (1.0f - 0.12f * Wander), Height + 0.08f * Flock.Radius * FMath::Sin(Time * 0.5f * Pace + Phase));

			// face along the direction of travel, banked into the turn
			const FRotator Rotation(0.0f, FMath::RadiansToDegrees(Angle) + 90.0f * Turn, -22.0f * Turn);
			Transforms.Add(FTransform(Rotation, Flock.Center + Offset, FVector(Flock.Scale * Rng.FRandRange(0.8f, 1.2f))));
		}
	}

	Birds->BatchUpdateInstancesTransforms(0, Transforms, true, true, true);
}

void AFuyuWorld::BuildScenery()
{
	using namespace FuyuTerrain;

	// --- mist ----------------------------------------------------------------------------------
	// a sea of cloud lies against the foot of the mountain, so that it seems to float above the island
	FRandomStream Rng(8080);
	for (int32 i = 0; i < 11; ++i)
	{
		const float Angle = FMath::DegreesToRadians(132.0f + i * 9.6f);
		const float Dist = 104000.0f + Rng.FRandRange(-7000.0f, 9000.0f);
		const float X = MountainCenter.X + FMath::Cos(Angle) * Dist, Y = MountainCenter.Y + FMath::Sin(Angle) * Dist;
		const float Ground = FMath::Max(Height(X, Y), 0.0f);
		AddMist(FVector(X, Y, Ground + 2100.0f), 46000.0f, Rng.FRandRange(0.0f, 360.0f));
		AddMist(FVector(X + 5000.0f, Y - 3000.0f, Ground + 3900.0f), 34000.0f, Rng.FRandRange(0.0f, 360.0f));
	}

	// low mist drifts over the lake
	AddMist(FVector(LakeCenter.X, LakeCenter.Y, 260.0f), 26000.0f, 20.0f);
	AddMist(FVector(LakeCenter.X - 4000.0f, LakeCenter.Y + 5000.0f, 520.0f), 20000.0f, 110.0f);

	// --- birds ---------------------------------------------------------------------------------
	// white cranes wheel in front of the mountain's dark forest
	const FLinearColor Crane(0.95f, 0.95f, 0.93f), Crow(0.018f, 0.018f, 0.022f);
	const FVector2D CraneSpots[] = { FVector2D(42000, -17000), FVector2D(34000, 27000), FVector2D(72000, 2000) };
	for (int32 i = 0; i < 3; ++i)
	{
		const FVector Center = Ground(CraneSpots[i]);
		AddFlock(Center, 9000.0f + i * 2500.0f, 4200.0f + i * 2200.0f, 8200.0f + i * 3200.0f, 48, 1500.0f, 3.2f + i * 0.7f, Crane);
	}

	// --- groves --------------------------------------------------------------------------------
	// bamboo stands beside the violet slope, blossom rings the pagoda and the shrine, and cypress guards the memorial
	const FLinearColor Blossom(7.5f, 1.7f, 8.5f);
	PlaceGrove(TEXT("Tree_Giant_Bamboo/Tree_Giant_Bamboo_01/Tree_Giant_Bamboo_01"), FVector2D(-61500, 11500), 2400.0f, 260, 0.6f, 0.95f, nullptr, 31);
	PlaceGrove(TEXT("Tree_Giant_Bamboo/Tree_Giant_Bamboo_01/Tree_Giant_Bamboo_01"), FVector2D(-24500, 14500), 1900.0f, 170, 0.6f, 0.95f, nullptr, 32);

	if (const FFuyuSite* Lookout = FindSite(TEXT("Lookout")))
	{
		PlaceGrove(TEXT("Tree_Yoshino_Cherry/Tree_Yoshino_Cherry_01/Tree_Yoshino_Cherry_01"), Lookout->Pos + FVector2D(-1500, -2600), 2100.0f, 12, 0.6f, 0.85f, &Blossom, 33);
	}
	if (const FFuyuSite* Shrine = FindSite(TEXT("Shrine")))
	{
		PlaceGrove(TEXT("Tree_Yoshino_Cherry/Tree_Yoshino_Cherry_01/Tree_Yoshino_Cherry_01"), Shrine->Pos + FVector2D(1500, 0), 3000.0f, 9, 0.6f, 0.85f, &Blossom, 34);
	}
	if (const FFuyuSite* Memorial = FindSite(TEXT("Memorial")))
	{
		PlaceGrove(TEXT("Tree_Japanese_Cypress/Tree_Japanese_Cypress_01/Tree_Japanese_Cypress_01"), Memorial->Pos + FVector2D(0, 5200), 2600.0f, 22, 0.6f, 0.9f, nullptr, 35);
	}
}

void AFuyuWorld::BuildMemorial(const FFuyuSite& Site)
{
	FRandomStream Rng(GetTypeHash(Site.Id));

	// a way of flat stones follows the trail through the field
	for (float Along = -5200.0f; Along <= 5200.0f; Along += 92.0f)
	{
		// the trail wanders, so look sideways for where it actually runs at this point
		float BestRight = 0.0f, BestDistance = MAX_flt;
		for (float Right = -900.0f; Right <= 900.0f; Right += 60.0f)
		{
			const FVector P = SitePoint(Site, Along, Right);
			const float D = FuyuTerrain::PathDistance(P.X, P.Y);
			if (D < BestDistance)
			{
				BestDistance = D;
				BestRight = Right;
			}
		}

		for (float Side : { -72.0f, 0.0f, 72.0f })
		{
			if (Rng.FRand() < 0.88f)
			{
				Place(EProp::PathSlab, SitePoint(Site, Along + Rng.FRandRange(-22.0f, 22.0f), BestRight + Side + Rng.FRandRange(-14.0f, 14.0f)), Rng.FRandRange(0.0f, 360.0f), Rng.FRandRange(0.85f, 1.2f));
			}
		}
	}

	// markers stand among the flowers in loose ranks, a few leaning
	for (int32 i = 0; i < 34; ++i)
	{
		const float Along = Rng.FRandRange(-4300.0f, 4300.0f);
		const float Right = (Rng.FRand() < 0.5f ? -1.0f : 1.0f) * Rng.FRandRange(520.0f, 3600.0f);
		const FVector P = SitePoint(Site, Along, Right);
		if (FuyuTerrain::PathDistance(P.X, P.Y) > 420.0f)
		{
			GetScatter(EProp::GravePillar)->AddInstance(FTransform(FRotator(Rng.FRandRange(-5.0f, 5.0f), Rng.FRandRange(0.0f, 360.0f), Rng.FRandRange(-5.0f, 5.0f)), P - FVector(0, 0, 10.0f), FVector(Rng.FRandRange(0.8f, 1.35f))), true);
		}
	}

	// a small wayside shrine watches over them, with a pair of lanterns
	// the hall stands about three times the samurai's height
	Place(EProp::ShrineHall, SitePoint(Site, -600.0f, 1800.0f), Site.Yaw + 90.0f, 0.76f);
	Place(EProp::StoneLantern, SitePoint(Site, -120.0f, 1280.0f), Site.Yaw);
	Place(EProp::StoneLantern, SitePoint(Site, -1080.0f, 1280.0f), Site.Yaw);

	// ground mist pools here, and crows turn overhead
	const FVector Center = SitePoint(Site, 0.0f, 0.0f);
	AddMist(Center + FVector(0, 0, 380.0f), 15000.0f, 0.0f);
	AddMist(Center + FVector(2600.0f, -1800.0f, 760.0f), 12000.0f, 70.0f);
	AddMist(Center + FVector(-3000.0f, 2200.0f, 1200.0f), 14000.0f, 140.0f);
	AddFlock(Center, 2600.0f, 320.0f, 1900.0f, 130, 950.0f, 1.0f, FLinearColor(0.018f, 0.018f, 0.022f));
}

void AFuyuWorld::BuildSites()
{
	for (const FFuyuSite& Site : FuyuTerrain::GetSites())
	{
		switch (Site.Type)
		{
		case EFuyuSiteType::Camp:
			BuildCamp(Site);
			AddFlock(SitePoint(Site, 0.0f, 0.0f), 1700.0f, 900.0f, 1600.0f, 12, 800.0f, 1.0f, FLinearColor(0.018f, 0.018f, 0.022f));
			break;

		case EFuyuSiteType::Shrine:
			BuildShrine(Site);
			break;

		case EFuyuSiteType::Memorial:
			BuildMemorial(Site);
			break;

		case EFuyuSiteType::Start:
			// a weathered gate frames the first view of the mountain
			Place(EProp::Torii, SitePoint(Site, 900.0f, 0.0f), Site.Yaw);
			Place(EProp::StoneLantern, SitePoint(Site, 700.0f, 420.0f), Site.Yaw);
			Place(EProp::StoneLantern, SitePoint(Site, 700.0f, -420.0f), Site.Yaw);
			Place(EProp::Ginkgo, SitePoint(Site, -900.0f, 700.0f), 40.0f, 1.9f);
			break;

		case EFuyuSiteType::Lookout:
		{
			// a pagoda crowns the hill above the violet slope, with a gate on the side the trail climbs from
			Place(EProp::Pagoda, SitePoint(Site, 0.0f, 0.0f), 20.0f);

			const FFuyuSite* Start = FuyuTerrain::FindSite(TEXT("Start"));
			const FVector2D ToStart = Start ? (Start->Pos - Site.Pos).GetSafeNormal() : FVector2D(-1, 0);
			const FVector Gate = FuyuTerrain::Ground(Site.Pos + ToStart * 1900.0f);
			Place(EProp::Torii, Gate, FMath::RadiansToDegrees(FMath::Atan2(ToStart.Y, ToStart.X)));
			Place(EProp::StoneLantern, FuyuTerrain::Ground(Site.Pos + ToStart * 1300.0f + FVector2D(-ToStart.Y, ToStart.X) * 420.0f), 0.0f);
			Place(EProp::StoneLantern, FuyuTerrain::Ground(Site.Pos + ToStart * 1300.0f - FVector2D(-ToStart.Y, ToStart.X) * 420.0f), 0.0f);
			break;
		}
		}
	}
}

void AFuyuWorld::BuildCamp(const FFuyuSite& Site)
{
	FRandomStream Rng(GetTypeHash(Site.Id));
	TArray<TWeakObjectPtr<USceneComponent>>& Dressing = SiteDressing.FindOrAdd(Site.Id);

	// a log palisade rings the camp, open towards the approach
	const float WallRadius = Site.Radius * 0.74f;
	const int32 Segments = FMath::FloorToInt32(2.0f * PI * WallRadius / 352.0f);
	for (int32 i = 0; i < Segments; ++i)
	{
		const float Angle = 360.0f * i / Segments;
		const float FromGate = FMath::Abs(FMath::FindDeltaAngleDegrees(Angle, 0.0f));
		const float FromBack = FMath::Abs(FMath::FindDeltaAngleDegrees(Angle, 150.0f));
		if (FromGate < 17.0f || FromBack < 9.0f)
		{
			continue;
		}

		const float Rad = FMath::DegreesToRadians(Angle);
		const FVector P = SitePoint(Site, FMath::Cos(Rad) * WallRadius, FMath::Sin(Rad) * WallRadius);
		Place(EProp::Palisade, P, Site.Yaw + Angle);
	}

	// tents face the fire in the middle
	const int32 Tents = 4;
	for (int32 i = 0; i < Tents; ++i)
	{
		const float Angle = 70.0f + 220.0f * i / (Tents - 1);
		const float Rad = FMath::DegreesToRadians(Angle);
		const float Dist = Site.Radius * 0.47f;
		Place(EProp::Tent, SitePoint(Site, FMath::Cos(Rad) * Dist, FMath::Sin(Rad) * Dist), Site.Yaw + Angle + 90.0f);
	}

	const FVector FirePos = SitePoint(Site, 0.0f, 0.0f);
	UStaticMeshComponent* Fire = PlaceUnique(EProp::Campfire, FirePos, Rng.FRandRange(0.0f, 360.0f), false);

	UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(140.0f);
	Light->SetLightColor(FLinearColor(1.0f, 0.5f, 0.18f));
	Light->SetAttenuationRadius(1500.0f);
	Light->SetCastShadows(false);
	Light->SetupAttachment(Fire);
	Light->SetRelativeLocation(FVector(0, 0, 90));
	Light->RegisterComponent();

	// the occupiers' banners mark the gate and the ring; they come down when the camp is freed
	const float BannerAngles[] = { 20.0f, -20.0f, 95.0f, -95.0f, 180.0f };
	for (float Angle : BannerAngles)
	{
		const float Rad = FMath::DegreesToRadians(Angle);
		const float Dist = WallRadius + 90.0f;
		Dressing.Add(PlaceUnique(EProp::Banner, SitePoint(Site, FMath::Cos(Rad) * Dist, FMath::Sin(Rad) * Dist), Site.Yaw + Angle, false));
	}

	// supplies stacked about the yard
	for (int32 i = 0; i < 5; ++i)
	{
		const float Rad = Rng.FRandRange(0.0f, 2.0f * PI);
		const float Dist = Rng.FRandRange(Site.Radius * 0.2f, Site.Radius * 0.62f);
		Place(EProp::Rock, SitePoint(Site, FMath::Cos(Rad) * Dist, FMath::Sin(Rad) * Dist), Rng.FRandRange(0.0f, 360.0f), Rng.FRandRange(0.5f, 0.9f));
	}
}

void AFuyuWorld::BuildShrine(const FFuyuSite& Site)
{
	// the hall sits at the back of the precinct with its open front towards the approach
	Place(EProp::ShrineHall, SitePoint(Site, -1500.0f, 0.0f), Site.Yaw + 180.0f);

	// a tunnel of gates climbs to it, flanked by lanterns
	for (int32 i = 0; i < 7; ++i)
	{
		const float Dist = 1200.0f + i * 820.0f;
		Place(EProp::Torii, SitePoint(Site, Dist, 0.0f), Site.Yaw, i == 0 ? 1.25f : 1.0f);
	}

	for (int32 i = 0; i < 5; ++i)
	{
		const float Dist = -300.0f + i * 700.0f;
		Place(EProp::StoneLantern, SitePoint(Site, Dist, 470.0f), Site.Yaw);
		Place(EProp::StoneLantern, SitePoint(Site, Dist, -470.0f), Site.Yaw);
	}

	// two ancient ginkgo stand guard over the duelling ground
	Place(EProp::Ginkgo, SitePoint(Site, -1100.0f, 1900.0f), 15.0f, 2.6f);
	Place(EProp::Ginkgo, SitePoint(Site, -1100.0f, -1900.0f), 200.0f, 2.4f);
	Place(EProp::Boulder, SitePoint(Site, 600.0f, 2300.0f), 30.0f, 1.3f);
	Place(EProp::Boulder, SitePoint(Site, 300.0f, -2400.0f), 110.0f, 1.1f);
}

void AFuyuWorld::SetSiteLiberated(FName SiteId)
{
	if (TArray<TWeakObjectPtr<USceneComponent>>* Dressing = SiteDressing.Find(SiteId))
	{
		for (TWeakObjectPtr<USceneComponent>& Comp : *Dressing)
		{
			if (Comp.IsValid())
			{
				Comp->SetVisibility(false, true);
			}
		}
	}
}

UInstancedStaticMeshComponent* AFuyuWorld::AcquireGrassComponent(uint8 Key)
{
	// keys from 100 up are scanned grass meshes, lower keys are code-built props
	TArray<UInstancedStaticMeshComponent*>& Pool = GrassPool.FindOrAdd(Key);
	if (Pool.Num() > 0)
	{
		return Pool.Pop(EAllowShrinking::No);
	}

	UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(this);
	Comp->SetStaticMesh(Key >= 100 ? ScannedGrass[Key - 100].Get() : FuyuProps::Get((EProp)Key));
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetCastShadow(false);

	// the code-built clumps read a ground tint from per-instance data. The Megascans foliage material uses that
	// same channel for its own purposes and renders black if handed a tint, so scanned grass gets none
	Comp->SetNumCustomDataFloats(Key >= 100 ? 0 : 3);
	Comp->SetupAttachment(Root);
	Comp->RegisterComponent();
	GrassComponents.Add(Comp);
	return Comp;
}

void AFuyuWorld::ReleaseGrassCell(FFuyuGrassCell& Cell)
{
	for (TPair<uint8, UInstancedStaticMeshComponent*>& Pair : Cell.Components)
	{
		Pair.Value->ClearInstances();
		GrassPool.FindOrAdd(Pair.Key).Add(Pair.Value);
	}
	Cell.Components.Reset();
	Cell.bBuilt = false;
}

void AFuyuWorld::BuildGrassCell(FFuyuGrassCell& Cell, bool bNear)
{
	using namespace FuyuTerrain;

	ReleaseGrassCell(Cell);
	Cell.bNear = bNear;
	Cell.bBuilt = true;

	const FFuyuSite* Shrine = FindSite(TEXT("Shrine"));

	// distant cells draw fewer, larger clumps: the eye reads the same coverage for a quarter of the instances
	const int32 N = bNear ? 30 : 15;
	const float Spacing = GrassCellSize / N;
	const float SizeBoost = bNear ? 1.0f : 1.75f;

	FRandomStream Rng(HashCell(Cell.Coord.X, Cell.Coord.Y, 99));

	TMap<uint8, TArray<FTransform>> Transforms;
	TMap<uint8, TArray<float>> Tints;

	const float BaseX = Cell.Coord.X * GrassCellSize;
	const float BaseY = Cell.Coord.Y * GrassCellSize;

	// slope and biome change slowly, so they are sampled once per cell corner rather than per clump
	const float CenterX = BaseX + GrassCellSize * 0.5f, CenterY = BaseY + GrassCellSize * 0.5f;
	const float CellNormalZ = Normal(CenterX, CenterY).Z;
	const float CellFlower = FlowerMask(CenterX, CenterY);
	const float CellForest = ForestMask(CenterX, CenterY);
	const float CellReed = FMath::PerlinNoise2D(FVector2D(CenterX, CenterY) / 7000.0f + FVector2D(8.5f, 3.25f));
	const float ShrineDist = Shrine ? FVector2D::Distance(FVector2D(CenterX, CenterY), Shrine->Pos) : 1e9f;

	for (int32 j = 0; j < N; ++j)
	{
		for (int32 i = 0; i < N; ++i)
		{
			const float X = BaseX + (i + Rng.FRand()) * Spacing;
			const float Y = BaseY + (j + Rng.FRand()) * Spacing;
			const float Roll = Rng.FRand();
			const float Kind = Rng.FRand();
			const float Yaw = Rng.FRandRange(0.0f, 360.0f);
			const float Scale = Rng.FRandRange(0.8f, 1.35f);

			const float H = Height(X, Y);
			const float Density = GrassDensity(X, Y, H, CellNormalZ);
			if (Roll > Density)
			{
				continue;
			}

			FLinearColor Bloom;
			const float Blooming = Flowers(X, Y, Bloom);

			EProp Prop = EProp::GrassClump;
			FLinearColor Tint = GroundColor(X, Y, H, CellNormalZ) * 1.2f;
			float PropScale = Scale * SizeBoost;

			if (ShrineDist < 7500.0f && Kind < 0.2f)
			{
				// spider lilies carpet the shrine precinct
				Prop = EProp::LilyClump;
				Tint = FLinearColor::White;
				PropScale = Scale;
			}
			else if (Blooming > 0.0f && Kind < Blooming * 0.92f)
			{
				// flower fields: nearly every clump is a patch of blooms, each a shade off its neighbours
				Prop = EProp::FlowerCarpet;
				Tint = Bloom * FMath::Lerp(0.78f, 1.12f, Kind / FMath::Max(Blooming, 0.01f));
				PropScale = Scale * (bNear ? 1.0f : 1.7f);
			}
			else if (CellReed > 0.12f && Kind > 0.945f)
			{
				Prop = EProp::Pampas;
				Tint = FLinearColor::White;
				PropScale = Scale * (bNear ? 1.0f : 1.3f);
			}

			// plain grass uses a scanned clump where available: dry straw on the open plains, green under trees and in meadows
			uint8 Key = (uint8)Prop;
			if (Prop == EProp::GrassClump && ScannedGrass.Num() > 0)
			{
				const bool bGreen = (CellForest > 0.35f || CellFlower > 0.3f || Blooming > 0.1f) && ScannedGrass.Num() > NumDryGrass;
				const int32 First = bGreen ? NumDryGrass : 0;
				const int32 Count = bGreen ? ScannedGrass.Num() - NumDryGrass : FMath::Max(NumDryGrass, 1);
				Key = (uint8)(100 + First + (int32)(Kind * 997.0f) % Count);
				PropScale = Scale * (bNear ? 1.5f : 2.3f);
			}

			Transforms.FindOrAdd(Key).Add(FTransform(FRotator(0, Yaw, 0), FVector(X, Y, H), FVector(PropScale, PropScale, PropScale * Rng.FRandRange(0.85f, 1.25f))));
			Tints.FindOrAdd(Key).Append({ Tint.R, Tint.G, Tint.B });
		}
	}

	for (TPair<uint8, TArray<FTransform>>& Pair : Transforms)
	{
		UInstancedStaticMeshComponent* Comp = AcquireGrassComponent(Pair.Key);
		Comp->AddInstances(Pair.Value, false, true);

		const TArray<float>& TintData = Tints[Pair.Key];
		for (int32 i = 0; i < Pair.Value.Num() && Pair.Key < 100; ++i)
		{
			Comp->SetCustomData(i, MakeArrayView(&TintData[i * 3], 3), false);
		}

		Comp->MarkRenderStateDirty();
		Cell.Components.Add(Pair.Key, Comp);
	}
}

void AFuyuWorld::UpdateGrass(const FVector& ViewLocation, bool bBuildEverything)
{
	const FIntPoint Center(FMath::FloorToInt32(ViewLocation.X / GrassCellSize), FMath::FloorToInt32(ViewLocation.Y / GrassCellSize));

	if (Center != LastGrassCell)
	{
		LastGrassCell = Center;

		// drop cells that have fallen out of range
		for (auto It = GrassCells.CreateIterator(); It; ++It)
		{
			const FIntPoint D = It.Key() - Center;
			if (FMath::Max(FMath::Abs(D.X), FMath::Abs(D.Y)) > GrassRadiusCells)
			{
				ReleaseGrassCell(*It.Value());
				It.RemoveCurrent();
			}
		}

		for (int32 Y = -GrassRadiusCells; Y <= GrassRadiusCells; ++Y)
		{
			for (int32 X = -GrassRadiusCells; X <= GrassRadiusCells; ++X)
			{
				const FIntPoint Coord = Center + FIntPoint(X, Y);
				if (!GrassCells.Contains(Coord))
				{
					TSharedPtr<FFuyuGrassCell> Cell = MakeShared<FFuyuGrassCell>();
					Cell->Coord = Coord;
					GrassCells.Add(Coord, Cell);
				}
			}
		}
	}

	// build or re-detail a few cells per frame, nearest first, so crossing a cell boundary never hitches
	int32 Budget = bBuildEverything ? MAX_int32 : 2;
	for (int32 Ring = 0; Ring <= GrassRadiusCells && Budget > 0; ++Ring)
	{
		for (TPair<FIntPoint, TSharedPtr<FFuyuGrassCell>>& Pair : GrassCells)
		{
			const FIntPoint D = Pair.Key - Center;
			if (FMath::Max(FMath::Abs(D.X), FMath::Abs(D.Y)) != Ring)
			{
				continue;
			}

			const bool bNear = Ring <= 1;
			if (!Pair.Value->bBuilt || Pair.Value->bNear != bNear)
			{
				BuildGrassCell(*Pair.Value, bNear);
				if (--Budget <= 0)
				{
					break;
				}
			}
		}
	}
}

void AFuyuWorld::UpdateLeaves(float DeltaSeconds, const FVector& ViewLocation)
{
	if (!Leaves || LeafCount <= 0)
	{
		return;
	}

	const float Range = 2600.0f;
	const FVector Side = FVector::CrossProduct(FVector::UpVector, WindDirection);

	if (LeafState.Num() != LeafCount)
	{
		FRandomStream Rng(1717);
		LeafState.SetNum(LeafCount);
		Leaves->ClearInstances();

		for (FLeaf& L : LeafState)
		{
			L.Pos = ViewLocation + FVector(Rng.FRandRange(-Range, Range), Rng.FRandRange(-Range, Range), Rng.FRandRange(-200.0f, 700.0f));
			L.Axis = Rng.VRand();
			L.Phase = Rng.FRandRange(0.0f, 2.0f * PI);
			L.Speed = Rng.FRandRange(260.0f, 520.0f);

			const int32 Index = Leaves->AddInstance(FTransform(L.Pos), true);

			// mostly ginkgo gold, with some maple red
			const FLinearColor Tint = Rng.FRand() < 0.7f ? FLinearColor(0.95f, 0.66f, 0.07f) : FLinearColor(0.75f, 0.1f, 0.03f);
			const float Data[3] = { Tint.R, Tint.G, Tint.B };
			Leaves->SetCustomData(Index, MakeArrayView(Data, 3), false);
		}
	}

	const float Time = GetWorld()->GetTimeSeconds();
	TArray<FTransform> Transforms;
	Transforms.Reserve(LeafCount);

	for (int32 i = 0; i < LeafCount; ++i)
	{
		FLeaf& L = LeafState[i];

		// leaves ride the wind, bobbing and sliding sideways as they tumble
		const float Flutter = FMath::Sin(Time * 1.7f + L.Phase);
		L.Pos += (WindDirection * L.Speed + Side * (Flutter * 90.0f) + FVector::UpVector * (FMath::Cos(Time * 1.1f + L.Phase * 2.0f) * 45.0f - 28.0f)) * DeltaSeconds;

		const FVector Offset = L.Pos - ViewLocation;
		const float Ground = FuyuTerrain::Height(L.Pos.X, L.Pos.Y);
		if (FMath::Abs(Offset.X) > Range || FMath::Abs(Offset.Y) > Range || L.Pos.Z < Ground + 4.0f)
		{
			// re-enter from the upwind side of the viewer
			const float Across = FMath::FRandRange(-Range, Range);
			const float Back = FMath::FRandRange(Range * 0.3f, Range * 0.95f);
			L.Pos = ViewLocation - WindDirection * Back + Side * Across;
			L.Pos.Z = FuyuTerrain::Height(L.Pos.X, L.Pos.Y) + FMath::FRandRange(80.0f, 800.0f);
		}

		const FQuat Spin(L.Axis, Time * 2.4f + L.Phase);
		Transforms.Add(FTransform(Spin, L.Pos, FVector(1.6f)));
	}

	Leaves->BatchUpdateInstancesTransforms(0, Transforms, true, true, true);
}

void AFuyuWorld::BlowGuidingWind(const FVector& Target)
{
	GuideTarget = Target;
	GuideTimeLeft = 4.5f;
}

void AFuyuWorld::UpdateStreaks(float DeltaSeconds, const FVector& ViewLocation)
{
	if (!Streaks)
	{
		return;
	}

	const bool bGusting = GuideTimeLeft > 0.0f;
	GuideTimeLeft = FMath::Max(GuideTimeLeft - DeltaSeconds, 0.0f);

	FVector Origin = ViewLocation;
	if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Origin = Pawn->GetActorLocation();
	}

	FVector Dir = (GuideTarget - Origin).GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		Dir = WindDirection;
	}
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Dir);
	const float Time = GetWorld()->GetTimeSeconds();

	TArray<FTransform> Transforms;
	Transforms.Reserve(StreakState.Num());
	bool bAnyAlive = false;

	for (FStreak& S : StreakState)
	{
		S.Age += DeltaSeconds;

		if (S.Age >= S.Life && bGusting)
		{
			// start a new ribbon behind and beside the player so the gust sweeps past them
			S.Age = -FMath::FRandRange(0.0f, 0.5f);
			S.Life = FMath::FRandRange(0.9f, 1.5f);
			S.Pos = Origin - Dir * FMath::FRandRange(200.0f, 1500.0f) + Side * FMath::FRandRange(-900.0f, 900.0f);
			S.Pos.Z = FMath::FRandRange(50.0f, 300.0f);
		}

		if (S.Age < 0.0f || S.Age >= S.Life)
		{
			Transforms.Add(FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000), FVector(0.001f)));
			continue;
		}

		bAnyAlive = true;

		// ribbons race towards the objective, weaving a little and hugging the terrain
		const float Weave = FMath::Sin(Time * 5.0f + S.Phase);
		const FVector Step = (Dir * 2700.0f + Side * (Weave * 240.0f)) * DeltaSeconds;
		S.Pos.X += Step.X;
		S.Pos.Y += Step.Y;

		const FVector World(S.Pos.X, S.Pos.Y, FuyuTerrain::Height(S.Pos.X, S.Pos.Y) + S.Pos.Z + Weave * 25.0f);
		const float Fade = FMath::Sin(PI * S.Age / S.Life);
		const FRotator Rot = (Dir * 2700.0f + Side * (Weave * 240.0f)).Rotation();
		Transforms.Add(FTransform(Rot, World, FVector(S.Length / 100.0f * (0.4f + 0.6f * Fade), 9.0f * Fade, 9.0f * Fade)));
	}

	if (bAnyAlive || bGusting)
	{
		Streaks->BatchUpdateInstancesTransforms(0, Transforms, true, true, true);
	}
}

void AFuyuWorld::SetCinemaMode(bool bEnabled)
{
	bCinemaMode = bEnabled;
	ApplyGrade();
}

void AFuyuWorld::ApplyGrade()
{
	FPostProcessSettings& S = PostProcess->Settings;

	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.9f;

	// a little blur keeps sword swings fluid, but the default amount smears the whole screen on a fast camera
	S.bOverride_MotionBlurAmount = true;
	S.MotionBlurAmount = 0.22f;

	S.bOverride_ColorGain = true;
	S.ColorGain = FVector4(1.0, 1.0, 1.0, 1.0);

	S.bOverride_VignetteIntensity = true;
	S.bOverride_FilmGrainIntensity = true;
	S.bOverride_ColorSaturation = true;
	S.bOverride_ColorContrast = true;
	S.bOverride_ColorGamma = true;
	S.bOverride_WhiteTemp = true;
	S.bOverride_AutoExposureBias = true;

	S.bOverride_FilmSlope = true;
	S.bOverride_FilmToe = true;


	if (bCinemaMode)
	{
		S.ColorSaturation = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
		S.ColorContrast = FVector4(1.28f, 1.28f, 1.28f, 1.0f);
		S.ColorGamma = FVector4(0.94f, 0.94f, 0.94f, 1.0f);
		S.FilmGrainIntensity = 0.75f;
		S.VignetteIntensity = 0.75f;
		S.WhiteTemp = 6500.0f;
		S.AutoExposureBias = 0.1f;
		S.FilmSlope = 0.95f;
		S.FilmToe = 0.62f;
	}
	else
	{
		S.ColorSaturation = FVector4(1.02f, 1.02f, 1.02f, 1.0f);
		S.ColorContrast = FVector4(1.06f, 1.06f, 1.06f, 1.0f);
		S.ColorGamma = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		S.FilmGrainIntensity = 0.12f;
		S.VignetteIntensity = 0.42f;
		S.WhiteTemp = 6900.0f;
		S.AutoExposureBias = 0.15f;
		S.FilmSlope = 0.88f;
		S.FilmToe = 0.47f;
	}

	// night is exposed darker and graded cold, or the eye's adaptation would turn moonlight into a blue noon
	const float Dark = GetNightAmount();
	S.AutoExposureBias += NightExposure * Dark;
	S.ColorGain = FMath::Lerp(FVector4(1.0, 1.0, 1.0, 1.0), FVector4(0.84, 0.93, 1.16, 1.0), (double)Dark);
	S.ColorSaturation = S.ColorSaturation * FMath::Lerp(1.0, 0.86, (double)Dark);
	S.ColorSaturation.W = 1.0;
}
