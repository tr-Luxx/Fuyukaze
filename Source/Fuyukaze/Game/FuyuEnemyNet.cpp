// Multiplayer for enemies. The state machine runs on the host; joined players are sent each enemy's
// state and a handful of cues, and play the matching animation and effects themselves.

#include "FuyuEnemy.h"
#include "FuyuAudio.h"
#include "FuyuSamurai.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "CombatLifeBar.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

void AFuyuEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFuyuEnemy, Type);
	DOREPLIFETIME(AFuyuEnemy, State);
	DOREPLIFETIME(AFuyuEnemy, Posture);
	DOREPLIFETIME(AFuyuEnemy, bUnblockableAttack);
	DOREPLIFETIME(AFuyuEnemy, RepHP);
}

AFuyuSamurai* AFuyuEnemy::FindPrey() const
{
	AFuyuSamurai* Nearest = nullptr;
	float NearestDistance = MAX_flt;

	for (TActorIterator<AFuyuSamurai> It(GetWorld()); It; ++It)
	{
		const float Distance = FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation());
		if (!It->IsDead() && Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = *It;
		}
	}

	return Nearest;
}

void AFuyuEnemy::ClientTick(float DeltaSeconds)
{
	UpdateGlint(DeltaSeconds);

	// health arrives from the host; refresh the floating bar when it changes
	CurrentHP = RepHP;
	if (LifeBarWidget && !FMath::IsNearlyEqual(ShownHP, CurrentHP))
	{
		ShownHP = CurrentHP;
		LifeBarWidget->SetLifePercentage(MaxHP > 0.0f ? CurrentHP / MaxHP : 0.0f);
	}

	if (State == EFuyuEnemyState::Dead)
	{
		return;
	}

	// the bar shows for enemies that are in the fight and near this machine's own player
	const APawn* Me = UGameplayStatics::GetPlayerPawn(this, 0);
	const bool bShowBar = IsAggressive() && Me && FVector::Dist2D(Me->GetActorLocation(), GetActorLocation()) < 3000.0f;
	if (LifeBar->bHiddenInGame == bShowBar)
	{
		LifeBar->SetHiddenInGame(!bShowBar);
	}
}

void AFuyuEnemy::Cue(uint8 Kind, const FVector& Location)
{
	if (HasAuthority() && GetNetMode() != NM_Standalone)
	{
		MulticastCue(Kind, Location);
	}
}

void AFuyuEnemy::MulticastCue_Implementation(uint8 Kind, FVector Location)
{
	// the host has already done all of this for itself
	if (HasAuthority())
	{
		return;
	}

	switch (Kind)
	{
	case 0:
	case 1:
		ShowGlint(Kind == 1);
		UFuyuAudio::Play(this, EFuyuSound::Glint, GetActorLocation(), Kind == 1 ? 1.0f : 0.6f);
		break;

	case 2:
	case 3:
		// a flinch always cuts short whatever swing was in progress
		if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
		{
			Anim->Montage_Stop(0.1f, ComboAttackMontage);
			Anim->Montage_Stop(0.1f, ChargedAttackMontage);
		}
		PlayReaction(Kind == 3);
		break;

	case 4:
	case 5:
		FlashTimeLeft = 0.14f;
		break;

	case 6:
		UFuyuAudio::Play(this, EFuyuSound::Guard, Location);
		if (ImpactFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ImpactFX, Location);
		}
		break;

	default:
		break;
	}
}

void AFuyuEnemy::MulticastAttack_Implementation(bool bHeavy, uint8 Count, float Rate)
{
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (HasAuthority() || !Anim)
	{
		return;
	}

	// the host has chosen how long the string or the charge will be; follow it so both machines show the same attack
	UAnimMontage* Montage = bHeavy ? ChargedAttackMontage : ComboAttackMontage;
	if (bHeavy)
	{
		TargetChargeLoops = Count;
		CurrentChargeLoop = 0;
	}
	else
	{
		TargetComboCount = Count;
		CurrentComboAttack = 0;
	}

	if (Montage)
	{
		Anim->Montage_Play(Montage, Rate);
	}
	UFuyuAudio::Play(this, bHeavy ? EFuyuSound::HeavySwing : EFuyuSound::Swing, GetActorLocation(), 0.6f);
}

void AFuyuEnemy::OnRep_Type()
{
	// dress this enemy for its kind: weapon, hat, size and colour
	Configure(Type, SiteId, Home);
}

void AFuyuEnemy::OnRep_State()
{
	if (State == EFuyuEnemyState::Dead && !bClientDead)
	{
		bClientDead = true;
		Glint->SetVisibility(false);
		GlintLight->SetIntensity(0.0f);

		if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
		{
			Anim->Montage_Stop(0.05f);
		}

		// fall as a ragdoll, as on the host
		ACombatEnemy::HandleDeath();
	}
}
