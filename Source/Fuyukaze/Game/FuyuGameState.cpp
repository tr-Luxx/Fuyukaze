#include "FuyuGameState.h"
#include "FuyuWorld.h"
#include "Net/UnrealNetwork.h"

void AFuyuGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFuyuGameState, ObjectiveText);
	DOREPLIFETIME(AFuyuGameState, ObjectiveLocation);
	DOREPLIFETIME(AFuyuGameState, bVictory);
	DOREPLIFETIME(AFuyuGameState, Banner);
	DOREPLIFETIME(AFuyuGameState, BannerSubtitle);
	DOREPLIFETIME(AFuyuGameState, BannerServerTime);
	DOREPLIFETIME(AFuyuGameState, LiberatedSites);
}

void AFuyuGameState::ShowBanner(const FText& Title, const FText& Subtitle)
{
	Banner = Title;
	BannerSubtitle = Subtitle;
	BannerServerTime = GetServerWorldTimeSeconds();
}

float AFuyuGameState::GetBannerAge() const
{
	return GetServerWorldTimeSeconds() - BannerServerTime;
}

void AFuyuGameState::AddLiberatedSite(FName SiteId)
{
	LiberatedSites.AddUnique(SiteId);
	OnRep_LiberatedSites();
}

void AFuyuGameState::OnRep_LiberatedSites()
{
	if (AFuyuWorld* World = AFuyuWorld::Get(this))
	{
		for (const FName& SiteId : LiberatedSites)
		{
			World->SetSiteLiberated(SiteId);
		}
	}
}
