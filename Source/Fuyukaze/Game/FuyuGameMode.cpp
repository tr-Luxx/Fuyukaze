#include "FuyuGameMode.h"
#include "FuyuAudio.h"
#include "FuyuEnemy.h"
#include "FuyuGameState.h"
#include "FuyuHUD.h"
#include "FuyuPlayerController.h"
#include "FuyuSamurai.h"
#include "FuyuTerrain.h"
#include "FuyuWorld.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

#define LOCTEXT_NAMESPACE "Fuyukaze"

DEFINE_LOG_CATEGORY_STATIC(LogFuyuGame, Log, All);

namespace
{
	struct FTourShot
	{
		const TCHAR* Name;

		/** Site the shot is framed around, or null to use absolute coordinates */
		const TCHAR* Site;

		/** Camera position: site-local (forward, right) or world XY, plus height above the ground there */
		FVector Camera;

		/** Look-at point in the same space, plus height above the ground there */
		FVector Target;

		float FOV;

		/** 0 = plain, 1 = relative to the player, 2 = cinema mode, 3 = relative to the player with the guiding wind blowing,
		    4 = plain, but the samurai is first stood at the look-at point (to photograph her among grass or flowers) */
		int32 Mode;
	};

	const FTourShot GTourShots[] = {
		{ TEXT("01_start_vista"),   TEXT("Start"),    FVector(-520, 190, 250),      FVector(9000, 300, 3600),      72.0f, 0 },
		{ TEXT("02_player_front"),  nullptr,          FVector(260, 70, 40),         FVector(0, 0, 20),             55.0f, 1 },
		{ TEXT("03_player_back"),   nullptr,          FVector(-330, 60, 70),        FVector(300, 0, 40),           78.0f, 1 },
		{ TEXT("04_meadow"),        nullptr,          FVector(-61000, -2000, 130),  FVector(-50000, 3000, 900),    70.0f, 0 },
		{ TEXT("05_camp_gate"),     TEXT("CampFarm"), FVector(4300, 700, 330),      FVector(0, 0, 160),            70.0f, 0 },
		{ TEXT("06_camp_yard"),     TEXT("CampFarm"), FVector(1500, 950, 190),      FVector(-400, -200, 120),      75.0f, 0 },
		{ TEXT("07_shrine_gates"),  TEXT("Shrine"),   FVector(7900, 60, 210),       FVector(0, 0, 900),            58.0f, 0 },
		{ TEXT("08_shrine_hall"),   TEXT("Shrine"),   FVector(500, 650, 210),       FVector(-1500, 0, 380),        72.0f, 0 },
		{ TEXT("09_lake"),          nullptr,          FVector(-31000, 47000, 420),  FVector(-42000, 60000, 0),     72.0f, 0 },
		{ TEXT("10_forest"),        nullptr,          FVector(34000, -30000, 190),  FVector(60000, -10000, 9000),  75.0f, 0 },
		{ TEXT("11_island"),        nullptr,          FVector(-140000, -80000, 52000), FVector(30000, 5000, 9000), 62.0f, 0 },
		{ TEXT("12_cinema_mode"),   TEXT("Start"),    FVector(-520, 190, 250),      FVector(9000, 300, 2300),      72.0f, 2 },
		{ TEXT("15_white_field"),   nullptr,          FVector(-74800, -15800, 75),   FVector(30000, 2000, 16000),   74.0f, 0 },
		{ TEXT("16_memorial"),      TEXT("Memorial"), FVector(3300, 260, 165),       FVector(-600, 300, 190),       70.0f, 0 },
		{ TEXT("17_memorial_low"),  TEXT("Memorial"), FVector(1500, -700, 60),       FVector(-600, 1500, 260),      66.0f, 0 },
		{ TEXT("18_violet_slope"),  nullptr,          FVector(-60500, 1500, 55),     FVector(-52000, 12000, 2200),  64.0f, 0 },
		{ TEXT("19_pagoda"),        TEXT("Lookout"),  FVector(-3600, -1500, 260),    FVector(0, 0, 1300),           70.0f, 0 },
		{ TEXT("20_mountain_mist"), nullptr,          FVector(-30000, -8000, 2600),  FVector(80000, 4000, 30000),   62.0f, 0 },
		{ TEXT("13_guiding_wind"),  nullptr,          FVector(-560, 150, 210),      FVector(600, 0, 40),           84.0f, 3 },
		{ TEXT("14_player_side"),   nullptr,          FVector(60, 420, 60),         FVector(60, 0, 10),            70.0f, 1 },
		{ TEXT("21_in_the_flowers"), nullptr,         FVector(-74000, -14700, 190), FVector(-74200, -15000, 40),   60.0f, 4 },
		{ TEXT("22_in_the_grass"),  nullptr,          FVector(-70800, -21700, 180), FVector(-71000, -22000, 40),   60.0f, 4 },
	};

	FVector GroundPoint(const FTourShot& Shot, const FVector& Local)
	{
		if (Shot.Site)
		{
			if (const FFuyuSite* Site = FuyuTerrain::FindSite(Shot.Site))
			{
				return AFuyuWorld::SitePoint(*Site, Local.X, Local.Y) + FVector(0, 0, Local.Z);
			}
		}
		return FVector(Local.X, Local.Y, FuyuTerrain::Height(Local.X, Local.Y) + Local.Z);
	}
}

AFuyuGameMode::AFuyuGameMode()
{
	PrimaryActorTick.bCanEverTick = true;

	PlayerControllerClass = AFuyuPlayerController::StaticClass();
	HUDClass = AFuyuHUD::StaticClass();
	GameStateClass = AFuyuGameState::StaticClass();
}

void AFuyuGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	// blueprint classes are resolved here rather than in the constructor so the module loads cleanly before they exist
	if (UClass* SamuraiClass = LoadClass<APawn>(nullptr, TEXT("/Game/Fuyukaze/Blueprints/BP_FuyuSamurai.BP_FuyuSamurai_C")))
	{
		DefaultPawnClass = SamuraiClass;
	}
	else
	{
		UE_LOG(LogFuyuGame, Error, TEXT("BP_FuyuSamurai is missing. Run Tools/build_content.py to generate the game's assets."));
	}

	EnemyClass = LoadClass<AFuyuEnemy>(nullptr, TEXT("/Game/Fuyukaze/Blueprints/BP_FuyuEnemy.BP_FuyuEnemy_C"));
	if (!EnemyClass)
	{
		UE_LOG(LogFuyuGame, Error, TEXT("BP_FuyuEnemy is missing. Run Tools/build_content.py to generate the game's assets."));
	}

	Super::InitGame(MapName, Options, ErrorMessage);

	if (!AFuyuWorld::Get(this))
	{
		GetWorld()->SpawnActor<AFuyuWorld>(AFuyuWorld::StaticClass(), FTransform::Identity);
	}

	bTour = FParse::Value(FCommandLine::Get(), TEXT("FuyuTour="), TourFolder);
	bBot = FParse::Param(FCommandLine::Get(), TEXT("FuyuBot"));
	FParse::Value(FCommandLine::Get(), TEXT("FuyuBotTime="), BotExtraTime);
}

void AFuyuGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer || NewPlayer->IsPendingKillPending())
	{
		return;
	}

	const FFuyuSite* Start = FuyuTerrain::FindSite(TEXT("Start"));
	if (!Start)
	{
		Super::RestartPlayer(NewPlayer);
		return;
	}

	// later players arrive beside the first, not inside her
	const int32 Slot = FMath::Max(GetNumPlayers() - 1, 0);
	const FVector Location = AFuyuWorld::SitePoint(*Start, -180.0f * ((Slot + 1) / 2), (Slot % 2 == 0 ? 1.0f : -1.0f) * 200.0f * ((Slot + 1) / 2)) + FVector(0, 0, 110.0f);
	RestartPlayerAtTransform(NewPlayer, FTransform(FRotator(0, Start->Yaw, 0), Location));

	// start looking at the mountain rather than at the back of the character's head
	NewPlayer->SetControlRotation(FRotator(-4.0f, Start->Yaw, 0.0f));
}

void AFuyuGameMode::StartPlay()
{
	Super::StartPlay();

	if (!EnemyClass)
	{
		return;
	}

	using T = EFuyuEnemyType;

	for (const FFuyuSite& Site : FuyuTerrain::GetSites())
	{
		Progress.Add(Site.Id);

		if (Site.Id == TEXT("CampFarm"))
		{
			SpawnCampGarrison(Site, { T::Ronin, T::Ronin, T::Spearman, T::Ronin });
		}
		else if (Site.Id == TEXT("CampLake"))
		{
			SpawnCampGarrison(Site, { T::Ronin, T::Spearman, T::Brute, T::Spearman, T::Ronin });
		}
		else if (Site.Id == TEXT("CampRidge"))
		{
			SpawnCampGarrison(Site, { T::Ronin, T::Brute, T::Spearman, T::Ronin, T::Brute, T::Ronin });
		}
	}

	// a pair of ronin on the road below the start hill give the player a first fight before any camp
	if (const FFuyuSite* Start = FuyuTerrain::FindSite(TEXT("Start")))
	{
		FFuyuSiteProgress& Patrol = Progress.Add(TEXT("Patrol"));
		for (int32 i = 0; i < 2; ++i)
		{
			const FVector P = AFuyuWorld::SitePoint(*Start, 5200.0f + i * 260.0f, -700.0f + i * 420.0f);
			if (SpawnEnemy(T::Ronin, TEXT("Patrol"), P, Start->Yaw + 180.0f))
			{
				++Patrol.Total;
				++Patrol.Alive;
			}
		}
	}

	ShowBanner(LOCTEXT("Intro", "FUYUKAZE"), LOCTEXT("IntroSub", "Three camps hold the island. Free them, then answer the challenge at the shrine."));
}

AFuyuEnemy* AFuyuGameMode::SpawnEnemy(EFuyuEnemyType Type, FName SiteId, const FVector& Location, float Yaw)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FVector SpawnLocation(Location.X, Location.Y, FuyuTerrain::Height(Location.X, Location.Y) + 115.0f);
	AFuyuEnemy* Enemy = GetWorld()->SpawnActor<AFuyuEnemy>(EnemyClass, SpawnLocation, FRotator(0, Yaw, 0), Params);
	if (Enemy)
	{
		Enemy->Configure(Type, SiteId, SpawnLocation);
	}
	return Enemy;
}

void AFuyuGameMode::SpawnCampGarrison(const FFuyuSite& Site, const TArray<EFuyuEnemyType>& Roster)
{
	FFuyuSiteProgress& State = Progress.FindOrAdd(Site.Id);

	for (int32 i = 0; i < Roster.Num(); ++i)
	{
		// the first two stand watch at the gate, the rest are spread round the fire
		FVector P;
		float Yaw;
		if (i < 2)
		{
			P = AFuyuWorld::SitePoint(Site, Site.Radius * 0.8f, i == 0 ? -330.0f : 330.0f);
			Yaw = Site.Yaw;
		}
		else
		{
			const float Angle = 60.0f + 240.0f * (i - 2) / FMath::Max(Roster.Num() - 3, 1);
			const float Rad = FMath::DegreesToRadians(Angle);
			P = AFuyuWorld::SitePoint(Site, FMath::Cos(Rad) * Site.Radius * 0.24f, FMath::Sin(Rad) * Site.Radius * 0.24f);
			Yaw = Site.Yaw + Angle + 180.0f;
		}

		if (SpawnEnemy(Roster[i], Site.Id, P, Yaw))
		{
			++State.Total;
			++State.Alive;
		}
	}
}

void AFuyuGameMode::SpawnDuelist()
{
	const FFuyuSite* Shrine = FuyuTerrain::FindSite(TEXT("Shrine"));
	if (!Shrine || bDuelistSpawned || !EnemyClass)
	{
		return;
	}

	bDuelistSpawned = true;
	FFuyuSiteProgress& State = Progress.FindOrAdd(Shrine->Id);
	if (SpawnEnemy(EFuyuEnemyType::Duelist, Shrine->Id, AFuyuWorld::SitePoint(*Shrine, -600.0f, 0.0f), Shrine->Yaw))
	{
		State.Total = 1;
		State.Alive = 1;
	}
}

void AFuyuGameMode::ShowBanner(const FText& Title, const FText& Subtitle)
{
	Banner = Title;
	BannerSubtitle = Subtitle;
	BannerTime = GetWorld()->GetTimeSeconds();

	// the game state carries it to every player
	if (AFuyuGameState* Shared = GetGameState<AFuyuGameState>())
	{
		Shared->ShowBanner(Title, Subtitle);
	}
}

float AFuyuGameMode::GetBannerAge() const
{
	return GetWorld()->GetTimeSeconds() - BannerTime;
}

const FFuyuSite* AFuyuGameMode::FindNextCamp() const
{
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector2D From = Player ? FVector2D(Player->GetActorLocation()) : FVector2D::ZeroVector;

	const FFuyuSite* Best = nullptr;
	float BestDistance = MAX_flt;

	for (const FFuyuSite& Site : FuyuTerrain::GetSites())
	{
		const FFuyuSiteProgress* State = Progress.Find(Site.Id);
		if (Site.Type != EFuyuSiteType::Camp || !State || State->bLiberated)
		{
			continue;
		}

		const float Distance = FVector2D::Distance(From, Site.Pos);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = &Site;
		}
	}

	return Best;
}

FVector AFuyuGameMode::GetObjectiveLocation() const
{
	if (const FFuyuSite* Camp = FindNextCamp())
	{
		return FuyuTerrain::Ground(Camp->Pos);
	}

	const FFuyuSite* Site = FuyuTerrain::FindSite(bVictory ? TEXT("Lookout") : TEXT("Shrine"));
	return Site ? FuyuTerrain::Ground(Site->Pos) : FVector::ZeroVector;
}

FText AFuyuGameMode::GetObjectiveText() const
{
	if (bVictory)
	{
		return LOCTEXT("ObjFree", "The island is free. Wander where the wind takes you.");
	}

	int32 Camps = 0, Freed = 0;
	for (const FFuyuSite& Site : FuyuTerrain::GetSites())
	{
		if (Site.Type == EFuyuSiteType::Camp)
		{
			++Camps;
			const FFuyuSiteProgress* State = Progress.Find(Site.Id);
			Freed += (State && State->bLiberated) ? 1 : 0;
		}
	}

	if (const FFuyuSite* Camp = FindNextCamp())
	{
		return FText::Format(LOCTEXT("ObjCamp", "Free {0}   ({1} of {2} camps freed)"), Camp->DisplayName, FText::AsNumber(Freed), FText::AsNumber(Camps));
	}

	return LOCTEXT("ObjDuel", "Face Raiko at the Shrine of the First Snow");
}

void AFuyuGameMode::NotifyEnemyKilled(AFuyuEnemy* Enemy)
{
	++BotKills;

	FFuyuSiteProgress* State = Progress.Find(Enemy->GetSiteId());
	if (!State)
	{
		return;
	}

	State->Alive = FMath::Max(State->Alive - 1, 0);
	UE_LOG(LogFuyuGame, Log, TEXT("%s fell at %s, %d of %d remain"), *Enemy->GetDisplayName().ToString(), *Enemy->GetSiteId().ToString(), State->Alive, State->Total);

	if (State->Alive > 0 || State->bLiberated)
	{
		return;
	}

	State->bLiberated = true;

	const FFuyuSite* Site = FuyuTerrain::FindSite(Enemy->GetSiteId());
	AFuyuSamurai* Player = Cast<AFuyuSamurai>(UGameplayStatics::GetPlayerPawn(this, 0));

	if (Site && (Site->Type == EFuyuSiteType::Camp || Site->Type == EFuyuSiteType::Shrine))
	{
		UFuyuAudio::Play(this, EFuyuSound::Drum, Player ? Player->GetActorLocation() : Enemy->GetActorLocation());
	}

	if (Enemy->GetEnemyType() == EFuyuEnemyType::Duelist)
	{
		bVictory = true;
		if (AFuyuGameState* Shared = GetGameState<AFuyuGameState>())
		{
			Shared->bVictory = true;
		}
		ShowBanner(LOCTEXT("Victory", "THE FIRST SNOW FALLS"), LOCTEXT("VictorySub", "Raiko is defeated. The island breathes again."));
		return;
	}

	if (!Site || Site->Type != EFuyuSiteType::Camp)
	{
		return;
	}

	if (AFuyuWorld* World = AFuyuWorld::Get(this))
	{
		World->SetSiteLiberated(Site->Id);
	}

	// the banners come down on every player's machine
	if (AFuyuGameState* Shared = GetGameState<AFuyuGameState>())
	{
		Shared->AddLiberatedSite(Site->Id);
	}

	// every player shares in the reward, and a freed camp becomes the place they return to if they fall
	int32 Slot = 0;
	for (TActorIterator<AFuyuPlayerController> It(GetWorld()); It; ++It, ++Slot)
	{
		if (AFuyuSamurai* Samurai = Cast<AFuyuSamurai>(It->GetPawn()))
		{
			Samurai->Restore(0.5f, 1.0f);
		}
		It->SetRespawnTransform(FTransform(FRotator(0, Site->Yaw + 180.0f, 0), AFuyuWorld::SitePoint(*Site, 400.0f, (Slot - 0.5f) * 220.0f) + FVector(0, 0, 110.0f)));
	}

	if (FindNextCamp())
	{
		ShowBanner(FText::Format(LOCTEXT("Liberated", "{0} LIBERATED"), Site->DisplayName.ToUpper()), LOCTEXT("LiberatedSub", "Health and resolve restored."));
	}
	else
	{
		SpawnDuelist();
		ShowBanner(FText::Format(LOCTEXT("Liberated", "{0} LIBERATED"), Site->DisplayName.ToUpper()), LOCTEXT("DuelSub", "A challenger waits at the Shrine of the First Snow."));
	}
}

void AFuyuGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// announce each site the first time the player walks into it
	if (const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		const FVector2D Where(Player->GetActorLocation());
		for (const FFuyuSite& Site : FuyuTerrain::GetSites())
		{
			FFuyuSiteProgress* State = Progress.Find(Site.Id);
			if (State && !State->bDiscovered && Site.Type != EFuyuSiteType::Start && FVector2D::Distance(Where, Site.Pos) < Site.Radius * 1.6f + 600.0f)
			{
				State->bDiscovered = true;
				if (GetBannerAge() > 5.0f)
				{
					ShowBanner(Site.DisplayName.ToUpper(), State->bLiberated || Site.Type != EFuyuSiteType::Camp ? FText::GetEmpty() : LOCTEXT("Occupied", "Occupied"));
				}
			}
		}
	}

	// keep the shared objective current
	if (AFuyuGameState* Shared = GetGameState<AFuyuGameState>())
	{
		Shared->ObjectiveText = GetObjectiveText();
		Shared->ObjectiveLocation = GetObjectiveLocation();
	}

	if (bBot)
	{
		TickBot(DeltaSeconds);
	}

	if (bTour)
	{
		TickTour(DeltaSeconds);
	}
}

void AFuyuGameMode::TickTour(float DeltaSeconds)
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		return;
	}

	TourTimer += DeltaSeconds;

	if (TourShot < 0)
	{
		// hold until shaders have finished compiling, otherwise the first shots show placeholder materials
		const bool bCompiling = GShaderCompilingManager && GShaderCompilingManager->IsCompiling();
		TourQuietTime = bCompiling ? 0.0f : TourQuietTime + DeltaSeconds;

		if ((TourQuietTime > 5.0f && TourTimer > 10.0f) || TourTimer > 1500.0f)
		{
			UE_LOG(LogFuyuGame, Log, TEXT("Tour starting after %.0f s"), TourTimer);
			TourCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity);
			TourCamera->GetCameraComponent()->bConstrainAspectRatio = false;
			// a long self-play run skips the scenery and stays with the samurai
			TourShot = (bBot && BotExtraTime > 0.0f) ? (int32)UE_ARRAY_COUNT(GTourShots) : 0;
			TourTimer = 0.0f;
			bTourShotTaken = false;
		}
		return;
	}

	if (MomentDelayFrames > 0 && --MomentDelayFrames == 0)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(TourFolder, FString::Printf(TEXT("fx_%02d_%s.png"), MomentShots++, *MomentTag)), true, false);
	}

	if (TourShot >= (int32)UE_ARRAY_COUNT(GTourShots))
	{
		// in a long self-play run, keep following the samurai and photograph the action every so often
		if (bBot && BotExtraTime > 0.0f && !bVictory)
		{
			BotExtraTime -= DeltaSeconds;

			// during the ultimate, show what the player would see: its camera work is part of the effect
			const AFuyuSamurai* Caster = Cast<AFuyuSamurai>(PC->GetPawn());
			AActor* WantedView = (Caster && (Caster->IsUltimateActive() || Caster->GetUltimateFocus() > 0.0f)) ? (AActor*)PC->GetPawn() : (AActor*)TourCamera;
			if (PC->GetViewTarget() != WantedView)
			{
				PC->SetViewTarget(WantedView);
			}

			if (const APawn* Pawn = PC->GetPawn())
			{
				const FTransform Base(FRotator(0, Pawn->GetActorRotation().Yaw, 0), Pawn->GetActorLocation());
				const FVector CameraLocation = Base.TransformPosition(FVector(-520.0f, 260.0f, 170.0f));
				const FVector TargetLocation = Base.TransformPosition(FVector(200.0f, 0.0f, 20.0f));
				TourCamera->SetActorLocationAndRotation(FMath::VInterpTo(TourCamera->GetActorLocation(), CameraLocation, DeltaSeconds, 3.0f), (TargetLocation - TourCamera->GetActorLocation()).Rotation());
				TourCamera->GetCameraComponent()->SetFieldOfView(80.0f);
			}

			if (TourTimer > 12.0f)
			{
				TourTimer = 0.0f;
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(TourFolder, FString::Printf(TEXT("bot_%02d.png"), BotShot++)), true, false);
			}
			return;
		}

		if (TourTimer > 1.5f)
		{
			UE_LOG(LogFuyuGame, Log, TEXT("Tour complete"));
			UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false);
			bTour = false;
		}
		return;
	}

	const FTourShot& Shot = GTourShots[TourShot];

	// mode 3 is a player-relative shot taken with the guiding wind blowing
	if (Shot.Mode == 3 && TourTimer < 3.0f)
	{
		if (AFuyuWorld* World = AFuyuWorld::Get(this))
		{
			World->BlowGuidingWind(GetObjectiveLocation());
		}
	}

	if (Shot.Mode == 4 && TourTimer < 0.5f)
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->TeleportTo(FVector(Shot.Target.X, Shot.Target.Y, FuyuTerrain::Height(Shot.Target.X, Shot.Target.Y) + 100.0f), Pawn->GetActorRotation(), false, true);
		}
	}

	FVector CameraLocation, TargetLocation;
	if (Shot.Mode == 1 || Shot.Mode == 3)
	{
		const APawn* Pawn = PC->GetPawn();
		const FTransform Base = Pawn ? Pawn->GetActorTransform() : FTransform::Identity;
		CameraLocation = Base.TransformPosition(Shot.Camera);
		TargetLocation = Base.TransformPosition(Shot.Target);
	}
	else
	{
		CameraLocation = GroundPoint(Shot, Shot.Camera);
		TargetLocation = GroundPoint(Shot, Shot.Target);
	}

	TourCamera->SetActorLocationAndRotation(CameraLocation, (TargetLocation - CameraLocation).Rotation());
	TourCamera->GetCameraComponent()->SetFieldOfView(Shot.FOV);

	if (PC->GetViewTarget() != TourCamera)
	{
		PC->SetViewTarget(TourCamera);
	}

	if (AFuyuWorld* World = AFuyuWorld::Get(this))
	{
		if (World->IsCinemaMode() != (Shot.Mode == 2))
		{
			World->SetCinemaMode(Shot.Mode == 2);
		}
	}

	// give streaming grass, shadows and temporal anti-aliasing a few seconds to settle before the shutter
	if (!bTourShotTaken && TourTimer > 4.0f)
	{
		extern ENGINE_API float GAverageFPS;
		const FString File = FPaths::Combine(TourFolder, FString(Shot.Name) + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(File, true, false);
		UE_LOG(LogFuyuGame, Log, TEXT("Tour shot %s at %.1f fps"), Shot.Name, GAverageFPS);
		bTourShotTaken = true;
	}

	if (bTourShotTaken && TourTimer > 4.8f)
	{
		++TourShot;
		TourTimer = 0.0f;
		bTourShotTaken = false;
	}
}

void AFuyuGameMode::RequestMomentShot(const TCHAR* Tag)
{
	// the ultimate is rare, so its shots are taken even when the ordinary quota has been used up
	const bool bUltimate = FCString::Strncmp(Tag, TEXT("ult"), 3) == 0;
	if (bTour && bBot && (MomentShots < 16 || bUltimate) && (MomentDelayFrames == 0 || bUltimate) && TourCamera)
	{
		MomentTag = Tag;
		MomentDelayFrames = 3;
	}
}

void AFuyuGameMode::TickBot(float DeltaSeconds)
{
	AFuyuSamurai* Player = Cast<AFuyuSamurai>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Player || Player->IsDead())
	{
		return;
	}

	// pick the nearest living enemy and fight it with a simple, readable routine
	AFuyuEnemy* Target = nullptr;
	float BestDistance = MAX_flt;
	for (TActorIterator<AFuyuEnemy> It(GetWorld()); It; ++It)
	{
		const float Distance = FVector::Dist(It->GetActorLocation(), Player->GetActorLocation());
		if (!It->IsDead() && Distance < BestDistance)
		{
			BestDistance = Distance;
			Target = *It;
		}
	}

	BotLogTimer += DeltaSeconds;
	if (BotLogTimer > 2.0f)
	{
		BotLogTimer = 0.0f;
		UE_LOG(LogFuyuGame, Log, TEXT("Bot: hp %.0f%% resolve %.1f kills %d target %s at %.0f cm (hp %.0f%% posture %.0f%% state %d)"),
			Player->GetHealthFraction() * 100.0f, Player->GetResolve(), BotKills,
			Target ? *Target->GetDisplayName().ToString() : TEXT("none"), BestDistance,
			Target ? Target->GetHealthFraction() * 100.0f : 0.0f, Target ? Target->GetPostureFraction() * 100.0f : 0.0f, Target ? (int32)Target->GetState() : -1);
	}

	// multiplayer: accept any challenge, and when a duel is on, fight the other player
	if (Player->GetChallenger() && !Player->GetDuelOpponent())
	{
		Player->BotChallenge();
	}

	if (AFuyuSamurai* Rival = Player->GetDuelOpponent())
	{
		const FVector ToRival = Rival->GetActorLocation() - Player->GetActorLocation();
		Player->BotLock(false);
		if (ToRival.Size2D() > 215.0f)
		{
			Player->AddMovementInput(ToRival.GetSafeNormal2D(), 1.0f);
		}
		else
		{
			BotAttackTimer -= DeltaSeconds;
			if (BotAttackTimer <= 0.0f)
			{
				BotAttackTimer = 0.34f;
				Player->DoComboAttackStart();
			}
		}
		return;
	}

	if (!Target)
	{
		return;
	}

	if (Player->GetHealthFraction() < 0.45f && Player->GetResolve() >= 1.0f)
	{
		Player->BotHeal();
	}

	const FVector To = (Target->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D();

	// with a full meter and a crowd to spend it on, use the ultimate. -FuyuBotUlt fills the meter the first
	// time a crowd is in reach, so that a test run is sure to show it
	static const bool bForceUltimate = FParse::Param(FCommandLine::Get(), TEXT("FuyuBotUlt"));
	static bool bForcedOnce = false;

	int32 Near = 0;
	for (TActorIterator<AFuyuEnemy> It(GetWorld()); It; ++It)
	{
		Near += (!It->IsDead() && FVector::Dist(It->GetActorLocation(), Player->GetActorLocation()) < 1300.0f) ? 1 : 0;
	}

	if (bForceUltimate && !bForcedOnce && Near >= 3 && !Player->IsUltimateActive())
	{
		bForcedOnce = true;
		Player->Restore(0.0f, 3.0f);
	}

	if (Player->IsUltimateReady() && Near >= 2)
	{
		Player->BotUltimate();
		return;
	}

	if (Player->IsUltimateActive())
	{
		return;
	}

	Player->BotSprint(BestDistance > 1500.0f);
	Player->BotLock(BestDistance < 900.0f);

	if (BestDistance > 230.0f)
	{
		Player->BotGuard(false);
		Player->AddMovementInput(To, 1.0f);
		return;
	}

	// guard the moment an enemy telegraphs, which lands inside the parry window; dodge what cannot be blocked
	const bool bThreat = Target->GetState() == EFuyuEnemyState::Windup;
	if (bThreat && Target->IsAttackUnblockable())
	{
		Player->BotGuard(false);
		Player->BotDodge();
		return;
	}

	if (bThreat || Target->GetState() == EFuyuEnemyState::Attack)
	{
		Player->BotGuard(true);
		return;
	}

	Player->BotGuard(false);

	BotAttackTimer -= DeltaSeconds;
	if (BotAttackTimer <= 0.0f)
	{
		BotAttackTimer = 0.32f;
		Player->BotSetStance(FuyuCombat::CounterStance(Target->GetEnemyType()));

		// finish a staggered enemy with a heavy blow, otherwise keep the light string going
		if (Target->IsStaggered())
		{
			Player->DoChargedAttackStart();
			Player->DoChargedAttackEnd();
		}
		else
		{
			Player->DoComboAttackStart();
		}
	}
}

#undef LOCTEXT_NAMESPACE
