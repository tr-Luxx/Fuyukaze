#include "FuyuGameState.h"
#include "FuyuWorld.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

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
	DOREPLIFETIME(AFuyuGameState, ClockOffset);
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

void AFuyuGameState::BeginPlay()
{
	Super::BeginPlay();

	// -FuyuTime=22.5 opens the game at that hour, and -FuyuTimeStop holds it there
	float Hour = 0.0f;
	if (HasAuthority() && FParse::Value(FCommandLine::Get(), TEXT("FuyuTime="), Hour))
	{
		ClockOffset = HourToCycle(Hour);
	}
	bClockStopped = FParse::Param(FCommandLine::Get(), TEXT("FuyuTimeStop"));

	// -FuyuPassAfter=20 passes the time once, that many seconds in, so a headless run can show the sky sweeping round
	float PassAfter = 0.0f;
	if (HasAuthority() && FParse::Value(FCommandLine::Get(), TEXT("FuyuPassAfter="), PassAfter) && PassAfter > 0.0f)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, this, &AFuyuGameState::PassTime, PassAfter, false);
	}}

float AFuyuGameState::GetCycleSeconds() const
{
	const float Elapsed = bClockStopped ? 0.0f : (float)FMath::Fmod(GetServerWorldTimeSeconds(), (double)CycleSeconds);
	return FMath::Fmod(Elapsed + ClockOffset, CycleSeconds);
}

float AFuyuGameState::CycleToHour(float Seconds)
{
	// sunrise is at six and sunset at eighteen, with the day stretched and the night squeezed to fit their lengths
	const float Hour = Seconds < DaySeconds ? 6.0f + 12.0f * Seconds / DaySeconds : 18.0f + 12.0f * (Seconds - DaySeconds) / NightSeconds;
	return FMath::Fmod(Hour, 24.0f);
}

float AFuyuGameState::HourToCycle(float Hour)
{
	Hour = FMath::Fmod(Hour + 24.0f, 24.0f);
	if (Hour >= 6.0f && Hour < 18.0f)
	{
		return (Hour - 6.0f) / 12.0f * DaySeconds;
	}
	return DaySeconds + ((Hour < 6.0f ? Hour + 24.0f : Hour) - 18.0f) / 12.0f * NightSeconds;
}

void AFuyuGameState::PassTime()
{
	// by day, wait for full dark; by night, wait for morning
	const float Hour = GetTimeOfDay();
	const float Target = HourToCycle(Hour >= 6.5f && Hour < 20.0f ? 22.0f : 8.0f);
	ClockOffset = FMath::Fmod(ClockOffset + FMath::Fmod(Target - GetCycleSeconds() + CycleSeconds, CycleSeconds), CycleSeconds);
}