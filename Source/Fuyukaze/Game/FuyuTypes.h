#pragma once

#include "CoreMinimal.h"
#include "FuyuTypes.generated.h"

/** Sword stances. Each one breaks the guard of a particular kind of opponent */
UENUM(BlueprintType)
enum class EFuyuStance : uint8
{
	/** Tight, direct cuts. Strong against swordsmen */
	Stone,

	/** Sweeping, circling cuts. Strong against spearmen */
	Wind,

	/** Heavy, committed blows. Strong against brutes */
	Moon
};

UENUM(BlueprintType)
enum class EFuyuEnemyType : uint8
{
	Ronin,
	Spearman,
	Brute,
	Duelist
};

/** Everything a defender needs to know about a blow that has just landed */
struct FFuyuStrike
{
	AActor* Attacker = nullptr;
	FVector Location = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	float Damage = 0.0f;

	/** Added to the defender's posture meter. A full meter staggers them */
	float PostureDamage = 0.0f;

	bool bHeavy = false;
	EFuyuStance Stance = EFuyuStance::Stone;
};

/** What became of a blow, so the attacker can answer with the right feedback */
enum class EFuyuStrikeResult : uint8
{
	Missed,
	Guarded,
	Hit,
	/** The blow broke the defender's posture */
	Broken,
	Killed
};

namespace FuyuCombat
{
	/** Each stance cuts in its own colour */
	inline FLinearColor StanceColor(EFuyuStance Stance)
	{
		switch (Stance)
		{
		case EFuyuStance::Wind: return FLinearColor(0.45f, 0.9f, 1.0f);
		case EFuyuStance::Moon: return FLinearColor(0.75f, 0.55f, 1.0f);
		default:                return FLinearColor(1.0f, 0.8f, 0.45f);
		}
	}

	inline EFuyuStance CounterStance(EFuyuEnemyType Type)
	{
		switch (Type)
		{
		case EFuyuEnemyType::Spearman: return EFuyuStance::Wind;
		case EFuyuEnemyType::Brute:    return EFuyuStance::Moon;
		default:                       return EFuyuStance::Stone;
		}
	}

	inline const TCHAR* StanceName(EFuyuStance Stance)
	{
		switch (Stance)
		{
		case EFuyuStance::Wind: return TEXT("WIND");
		case EFuyuStance::Moon: return TEXT("MOON");
		default:                return TEXT("STONE");
		}
	}
}
