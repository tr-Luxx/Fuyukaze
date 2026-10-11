#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FuyuGameState.generated.h"

/**
 *  The part of the game's progress that every player needs to see. The game mode only exists on the
 *  host, so it writes objectives, title cards and freed camps here and they replicate to everyone.
 */
UCLASS()
class AFuyuGameState : public AGameStateBase
{
	GENERATED_BODY()

public:

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Raises a centre-screen title card for all players */
	void ShowBanner(const FText& Title, const FText& Subtitle);

	/** Marks a camp as freed, which takes its banners down on every machine */
	void AddLiberatedSite(FName SiteId);

	const FText& GetBanner() const { return Banner; }
	const FText& GetBannerSubtitle() const { return BannerSubtitle; }
	float GetBannerAge() const;
	const FText& GetObjectiveText() const { return ObjectiveText; }
	FVector GetObjectiveLocation() const { return ObjectiveLocation; }

	UPROPERTY(Replicated)
	FText ObjectiveText;

	UPROPERTY(Replicated)
	FVector ObjectiveLocation = FVector::ZeroVector;

	UPROPERTY(Replicated)
	bool bVictory = false;

protected:

	UFUNCTION()
	void OnRep_LiberatedSites();

	UPROPERTY(Replicated)
	FText Banner;

	UPROPERTY(Replicated)
	FText BannerSubtitle;

	/** Server time at which the banner was raised, so every client fades it in step */
	UPROPERTY(Replicated)
	float BannerServerTime = -100.0f;

	UPROPERTY(ReplicatedUsing = OnRep_LiberatedSites)
	TArray<FName> LiberatedSites;
};
