#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FuyuTypes.h"
#include "FuyuGameMode.generated.h"

class AFuyuEnemy;
class AFuyuSamurai;
class AFuyuWorld;
class ACameraActor;
struct FFuyuSite;

/** Progress at one of the island's sites */
struct FFuyuSiteProgress
{
	int32 Total = 0;
	int32 Alive = 0;
	bool bLiberated = false;
	bool bDiscovered = false;
};

/**
 *  Runs the game's loop: free the three occupied camps, then answer the challenge at the shrine.
 *  Also makes sure a world actor exists, so the game plays in any level it is pointed at.
 */
UCLASS()
class AFuyuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	AFuyuGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Tick(float DeltaSeconds) override;

	void NotifyEnemyKilled(AFuyuEnemy* Enemy);

	/** Where the guiding wind should blow towards */
	FVector GetObjectiveLocation() const;
	FText GetObjectiveText() const;

	/** Large centre-screen message and how long ago it was raised, for the HUD to fade */
	const FText& GetBanner() const { return Banner; }
	const FText& GetBannerSubtitle() const { return BannerSubtitle; }
	float GetBannerAge() const;

	bool IsVictorious() const { return bVictory; }

	/** During a self-play run, photographs a moment of combat (a hit, a kill) a few frames after it happens */
	void RequestMomentShot(const TCHAR* Tag);
	bool IsTourRunning() const { return bTour; }

protected:

	AFuyuEnemy* SpawnEnemy(EFuyuEnemyType Type, FName SiteId, const FVector& Location, float Yaw);
	void SpawnCampGarrison(const FFuyuSite& Site, const TArray<EFuyuEnemyType>& Roster);
	void SpawnDuelist();
	void ShowBanner(const FText& Title, const FText& Subtitle);
	const FFuyuSite* FindNextCamp() const;

	void TickTour(float DeltaSeconds);
	void TickBot(float DeltaSeconds);

protected:

	UPROPERTY(Transient)
	TSubclassOf<AFuyuEnemy> EnemyClass;

	TMap<FName, FFuyuSiteProgress> Progress;

	FText Banner;
	FText BannerSubtitle;
	float BannerTime = -100.0f;

	bool bDuelistSpawned = false;
	bool bVictory = false;

	// automated tour: flies a camera round the island taking screenshots, then quits. Enabled with -FuyuTour=<folder>
	bool bTour = false;
	FString TourFolder;
	int32 TourShot = -1;
	float TourTimer = 0.0f;
	float TourQuietTime = 0.0f;
	bool bTourShotTaken = false;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> TourCamera;

	// self-play: drives the samurai through fights so combat can be checked from the log. Enabled with -FuyuBot
	bool bBot = false;
	float BotAttackTimer = 0.0f;
	float BotLogTimer = 0.0f;
	int32 BotKills = 0;

	/** Extra seconds the bot keeps playing after the tour's fixed shots, set with -FuyuBotTime=<seconds> */
	float BotExtraTime = 0.0f;
	int32 BotShot = 0;
	int32 MomentShots = 0;
	int32 MomentDelayFrames = 0;
	FString MomentTag;
};
