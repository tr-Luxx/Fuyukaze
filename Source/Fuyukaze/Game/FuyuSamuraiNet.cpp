// Multiplayer and duelling for the samurai. The host decides everything that matters (health, guards,
// hits, duels); each player's own machine animates her straight away and reports what she did.

#include "FuyuSamurai.h"
#include "FuyuAudio.h"
#include "FuyuEnemy.h"
#include "FuyuGameMode.h"
#include "FuyuGameState.h"
#include "FuyuHUD.h"
#include "FuyuTerrain.h"
#include "FuyuWorld.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogFuyuNet, Log, All);

void AFuyuSamurai::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFuyuSamurai, RepHP);
	DOREPLIFETIME(AFuyuSamurai, Resolve);
	DOREPLIFETIME(AFuyuSamurai, Stance);
	DOREPLIFETIME(AFuyuSamurai, DuelOpponent);
	DOREPLIFETIME(AFuyuSamurai, ChallengedBy);
	DOREPLIFETIME(AFuyuSamurai, HorseState);
	DOREPLIFETIME(AFuyuSamurai, HorseSpot);
	DOREPLIFETIME(AFuyuSamurai, HorseYaw);
	DOREPLIFETIME(AFuyuSamurai, TakenHorse);

	// the owner already knows whether she is guarding; this is for everyone watching her
	DOREPLIFETIME_CONDITION(AFuyuSamurai, bBlocking, COND_SkipOwner);
}

FString AFuyuSamurai::GetPlayerLabel() const
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const APlayerState* State = GetPlayerState();
	if (GameState && State)
	{
		const int32 Index = GameState->PlayerArray.IndexOfByKey(State);
		if (Index != INDEX_NONE)
		{
			return FString::Printf(TEXT("Player %d"), Index + 1);
		}
	}
	return TEXT("A wanderer");
}

float AFuyuSamurai::GetNoticeAge() const
{
	return GetWorld() ? GetWorld()->GetRealTimeSeconds() - NoticeTime : 100.0f;
}

bool AFuyuSamurai::IsStunned() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < StunnedUntil;
}

void AFuyuSamurai::NetTick(float DeltaSeconds)
{
	// health lives on the host and is mirrored everywhere else
	if (HasAuthority())
	{
		RepHP = CurrentHP;

		// an unanswered challenge lapses
		if (ChallengedBy && GetWorld()->GetTimeSeconds() - ChallengeTime > 15.0f)
		{
			ChallengedBy = nullptr;
		}

		// a duel ends if either fighter leaves or dies to something else
		if (DuelOpponent && (!IsValid(DuelOpponent) || DuelOpponent->IsDead() || IsDead()))
		{
			AFuyuSamurai* Other = DuelOpponent;
			DuelOpponent = nullptr;
			if (IsValid(Other))
			{
				Other->DuelOpponent = nullptr;
			}
		}
	}
	else
	{
		CurrentHP = RepHP;
	}

	// later players wear different colours so they can be told apart
	if (!bTinted && GetPlayerState() && GetWorld()->GetGameState())
	{
		const int32 Index = GetWorld()->GetGameState()->PlayerArray.IndexOfByKey(GetPlayerState());
		if (Index != INDEX_NONE)
		{
			bTinted = true;
			static const FLinearColor Colours[] = { FLinearColor(0.45f, 0.06f, 0.05f), FLinearColor(0.04f, 0.14f, 0.5f), FLinearColor(0.06f, 0.3f, 0.1f) };
			if (Index > 0)
			{
				for (int32 i = 0; i < GetMesh()->GetNumMaterials(); ++i)
				{
					if (UMaterialInstanceDynamic* Dyn = GetMesh()->CreateDynamicMaterialInstance(i))
					{
						Dyn->SetVectorParameterValue(TEXT("Paint Tint"), Colours[(Index - 1) % UE_ARRAY_COUNT(Colours)]);
					}
				}
			}
		}
	}

	ClientBotTick(DeltaSeconds);
}

// --- guard ------------------------------------------------------------------------------------

void AFuyuSamurai::StartBlockPose()
{
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (Anim && GuardAnim && ComboAttackMontage && ComboAttackMontage->SlotAnimTracks.Num() > 0)
	{
		Anim->Montage_Stop(0.08f, ComboAttackMontage);
		Anim->Montage_Stop(0.08f, ChargedAttackMontage);
		GuardMontage = Anim->PlaySlotAnimationAsDynamicMontage(GuardAnim, ComboAttackMontage->SlotAnimTracks[0].SlotName, 0.1f, 0.15f, 1.0f, 9999);
	}
}

void AFuyuSamurai::OnRep_Blocking()
{
	if (bBlocking)
	{
		StartBlockPose();
	}
	else
	{
		StopBlockPose();
	}
}

void AFuyuSamurai::ServerSetBlocking_Implementation(bool bNewBlocking)
{
	bBlocking = bNewBlocking && !IsDead();
	if (bBlocking)
	{
		BlockStartTime = GetWorld()->GetTimeSeconds();
	}
	OnRep_Blocking();
}

// --- movement ---------------------------------------------------------------------------------

void AFuyuSamurai::ServerDodge_Implementation(FVector Direction, bool bFaceDirection)
{
	if (IsDead() || GetCharacterMovement()->IsFalling())
	{
		return;
	}

	// the host runs the same dodge so that it agrees with the player about where she ends up,
	// and so that its own check for dodged blows uses the right moment
	bBlocking = false;
	StopBlockPose();
	DodgeDirection = Direction.GetSafeNormal2D();
	DodgeTimeLeft = DodgeDuration;
	FacingTimeLeft = 0.0f;
	LungeTimeLeft = 0.0f;
	if (bFaceDirection)
	{
		SetActorRotation(DodgeDirection.Rotation());
	}
}

void AFuyuSamurai::ServerSetSprinting_Implementation(bool bNewSprinting)
{
	bSprinting = bNewSprinting;
}

void AFuyuSamurai::ServerSetStance_Implementation(EFuyuStance NewStance)
{
	Stance = NewStance;
}

void AFuyuSamurai::ServerHeal_Implementation()
{
	if (!IsDead() && Resolve >= 1.0f && CurrentHP < MaxHP)
	{
		Resolve -= 1.0f;
		CurrentHP = FMath::Min(CurrentHP + MaxHP * HealFraction, MaxHP);
	}
}

void AFuyuSamurai::ServerPassTime_Implementation()
{
	if (AFuyuGameState* State = GetWorld()->GetGameState<AFuyuGameState>())
	{
		State->PassTime();
	}
}

void AFuyuSamurai::ServerChainReward_Implementation()
{
	Resolve = FMath::Min(Resolve + 0.25f, (float)MaxResolve);
}

void AFuyuSamurai::SnapTo(AActor* Target)
{
	if (!Target)
	{
		return;
	}

	// turn over a few frames instead of snapping, which reads as a pivot rather than a teleport
	const FVector To = Target->GetActorLocation() - GetActorLocation();
	FacingGoal = To.GetSafeNormal2D().Rotation();
	FacingTimeLeft = 0.14f;

	// glide in to striking distance over the wind-up of the cut, easing out so the stop is soft
	const float Gap = To.Size2D() - 135.0f;
	if (Gap > 15.0f && GetCharacterMovement()->IsMovingOnGround())
	{
		LungeTarget = Target;
		LungeTimeLeft = 0.2f;
		LungeSpeed = FMath::Min(Gap / 0.13f, 1500.0f);
	}
}

void AFuyuSamurai::SnapToTarget()
{
	// in a duel the opponent is the target if they are within reach of a lunge; otherwise pick an enemy
	AActor* Target = nullptr;
	if (DuelOpponent && !DuelOpponent->IsDead() && FVector::Dist2D(DuelOpponent->GetActorLocation(), GetActorLocation()) < 560.0f)
	{
		Target = DuelOpponent;
	}
	else
	{
		Target = FindAttackTarget(520.0f);
	}

	if (!Target)
	{
		return;
	}

	SnapTo(Target);
	if (!HasAuthority())
	{
		ServerSnapTo(Target);
	}
}

void AFuyuSamurai::ServerSnapTo_Implementation(AActor* Target)
{
	if (Target && !IsDead() && FVector::Dist2D(Target->GetActorLocation(), GetActorLocation()) < 900.0f)
	{
		SnapTo(Target);
	}
}

// --- attack animation -------------------------------------------------------------------------

void AFuyuSamurai::SendAttackAnim(uint8 Kind, uint8 Section)
{
	if (GetNetMode() == NM_Standalone)
	{
		return;
	}

	if (HasAuthority())
	{
		MulticastAttackAnim(Kind, Section);
	}
	else
	{
		ServerAttackAnim(Kind, Section);
	}
}

void AFuyuSamurai::ServerAttackAnim_Implementation(uint8 Kind, uint8 Section)
{
	MulticastAttackAnim(Kind, Section);
}

void AFuyuSamurai::MulticastAttackAnim_Implementation(uint8 Kind, uint8 Section)
{
	// the player who made the move has already played it
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (IsLocallyControlled() || !Anim)
	{
		return;
	}

	switch (Kind)
	{
	case 0:
		if (Section == 0 || !Anim->Montage_IsPlaying(ComboAttackMontage))
		{
			Anim->Montage_Play(ComboAttackMontage, ComboPlayRate);
		}
		if (Section > 0 && ComboSectionNames.IsValidIndex(Section))
		{
			Anim->Montage_JumpToSection(ComboSectionNames[Section], ComboAttackMontage);
		}
		UFuyuAudio::Play(this, EFuyuSound::Swing, GetActorLocation(), 0.7f);
		break;

	case 1:
		// hold the charge until the release cue arrives
		bIsChargingAttack = true;
		bHeavySwingSounded = false;
		Anim->Montage_Play(ChargedAttackMontage);
		break;

	case 2:
		bIsChargingAttack = false;
		if (Anim->Montage_IsPlaying(ChargedAttackMontage))
		{
			Anim->Montage_JumpToSection(ChargeAttackSection, ChargedAttackMontage);
		}
		break;

	case 3:
		if (DodgeAnim && ComboAttackMontage && ComboAttackMontage->SlotAnimTracks.Num() > 0)
		{
			Anim->Montage_Stop(0.05f, ComboAttackMontage);
			Anim->Montage_Stop(0.05f, ChargedAttackMontage);
			const float Rate = FMath::Max(DodgeAnim->GetPlayLength() / (DodgeDuration + 0.1f), 0.5f);
			Anim->PlaySlotAnimationAsDynamicMontage(DodgeAnim, ComboAttackMontage->SlotAnimTracks[0].SlotName, 0.06f, 0.14f, Rate);
		}
		break;

	default:
		break;
	}
}

void AFuyuSamurai::DoChargedAttackEnd()
{
	Super::DoChargedAttackEnd();
	SendAttackAnim(2, 0);
}

// --- striking ---------------------------------------------------------------------------------

void AFuyuSamurai::DoAttackTrace(FName DamageSourceBone)
{
	// the ultimate borrows the heavy cut's animation but deals its own damage
	if (IsUltimateActive())
	{
		return;
	}

	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	const bool bHeavy = Anim && Anim->Montage_IsPlaying(ChargedAttackMontage);
	const bool bMine = IsLocallyControlled();

	// the charged montage fires several trace notifies for one swing, the combo fires one per cut
	if (bMine && bHeavy && HitThisSwing.Num() == 0)
	{
		SnapToTarget();
	}

	// every swing leaves a crescent in the air, whether or not it connects. Successive cuts in a string
	// sweep along different lines, and a heavy blow comes down almost vertically
	if (!bHeavy || !bHeavySwingSounded)
	{
		static const float Rolls[] = { -28.0f, 34.0f, -8.0f, 62.0f };
		const float Roll = bHeavy ? 78.0f : Rolls[SwingCount++ % UE_ARRAY_COUNT(Rolls)];
		if (AFuyuWorld* World = AFuyuWorld::Get(this))
		{
			World->SpawnSlash(GetActorLocation() + FVector(0, 0, 25.0f), GetActorForwardVector(), Roll, bHeavy ? 1.75f : 1.25f, FuyuCombat::StanceColor(Stance));
		}
	}

	if (bHeavy && !bHeavySwingSounded)
	{
		bHeavySwingSounded = true;
		UFuyuAudio::Play(this, EFuyuSound::HeavySwing, GetActorLocation(), 0.9f);
	}

	// only the machine that controls this samurai decides who her sword reached
	if (!bMine)
	{
		return;
	}

	const FVector Origin = GetActorLocation();
	const FVector Forward = GetActorForwardVector();
	const float ArcCos = FMath::Cos(FMath::DegreesToRadians(SwordArc));

	// candidates: every living enemy, and the other player if a duel is on
	TArray<AActor*> Candidates;
	for (TActorIterator<AFuyuEnemy> It(GetWorld()); It; ++It)
	{
		if (!It->IsDead())
		{
			Candidates.Add(*It);
		}
	}
	if (DuelOpponent && !DuelOpponent->IsDead())
	{
		Candidates.Add(DuelOpponent);
	}

	for (AActor* Target : Candidates)
	{
		if (HitThisSwing.Contains(Target))
		{
			continue;
		}

		const FVector To = Target->GetActorLocation() - Origin;
		const float Distance = To.Size2D();
		const ACharacter* Body = Cast<ACharacter>(Target);
		const float Reach = SwordReach + (Body ? Body->GetCapsuleComponent()->GetScaledCapsuleRadius() : 35.0f);
		if (Distance > Reach || FMath::Abs(To.Z) > 220.0f)
		{
			continue;
		}

		if (Distance > 110.0f && FVector::DotProduct(To.GetSafeNormal2D(), Forward) < ArcCos)
		{
			continue;
		}

		HitThisSwing.Add(Target);

		if (HasAuthority())
		{
			ResolveStrike(Target, bHeavy);
		}
		else
		{
			ServerStrike(Target, bHeavy);
		}
	}
}

void AFuyuSamurai::ServerStrike_Implementation(AActor* Target, bool bHeavy)
{
	ResolveStrike(Target, bHeavy);
}

void AFuyuSamurai::ResolveStrike(AActor* Target, bool bHeavy)
{
	if (!Target || IsDead())
	{
		return;
	}

	// a generous range check: the player's machine has already tested the arc, this only rejects the absurd
	const FVector To = Target->GetActorLocation() - GetActorLocation();
	if (To.Size2D() > (SwordReach + 120.0f) * 1.7f)
	{
		return;
	}

	FFuyuStrike Strike;
	Strike.Attacker = this;
	Strike.Direction = To.GetSafeNormal2D();
	Strike.Location = Target->GetActorLocation() - Strike.Direction * 30.0f + FVector(0, 0, 40.0f);
	Strike.bHeavy = bHeavy;
	Strike.Stance = Stance;
	Strike.Damage = bHeavy ? HeavyDamage : LightDamage;
	Strike.PostureDamage = bHeavy ? HeavyPostureDamage : LightPostureDamage;

	EFuyuStrikeResult Result = EFuyuStrikeResult::Missed;
	float Dealt = 0.0f;
	bool bStrong = false;

	if (AFuyuEnemy* Enemy = Cast<AFuyuEnemy>(Target))
	{
		Result = Enemy->ReceiveStrike(Strike);
		Dealt = Enemy->GetLastDamageTaken();
		bStrong = Strike.Stance == FuyuCombat::CounterStance(Enemy->GetEnemyType()) || Dealt > Strike.Damage * 1.1f;
	}
	else if (AFuyuSamurai* Rival = Cast<AFuyuSamurai>(Target))
	{
		const FVector Impulse = Strike.Direction * (bHeavy ? 520.0f : 260.0f);
		Result = Rival->TakeStrike(Strike.Damage, this, Strike.Location, Impulse, false, bHeavy, Dealt);
		bStrong = bHeavy;
	}

	if (Result == EFuyuStrikeResult::Missed)
	{
		return;
	}

	if (!IsLocallyControlled())
	{
		UE_LOG(LogFuyuNet, Verbose, TEXT("%s struck %s for %.0f (result %d)"), *GetPlayerLabel(), *Target->GetName(), Dealt, (int32)Result);
		static int32 RemoteStrikes = 0;
		if (++RemoteStrikes % 10 == 1)
		{
			UE_LOG(LogFuyuNet, Log, TEXT("Strikes received from joined players so far: %d (latest: %s hit %s for %.0f)"), RemoteStrikes, *GetPlayerLabel(), *Target->GetName(), Dealt);
		}
	}

	if (IsLocallyControlled())
	{
		PlayStrikeFeedback(Target, Strike, Result, Dealt, bStrong);
	}
	else
	{
		ClientStrikeFeedback(Target, Strike.Location, Strike.Direction, bHeavy, (uint8)Result, Dealt, bStrong);
	}
}

void AFuyuSamurai::ClientStrikeFeedback_Implementation(AActor* Target, FVector Location, FVector Direction, bool bHeavy, uint8 Result, float Dealt, bool bStrong)
{
	FFuyuStrike Strike;
	Strike.Attacker = this;
	Strike.Location = Location;
	Strike.Direction = Direction;
	Strike.bHeavy = bHeavy;
	Strike.Stance = Stance;
	Strike.Damage = bHeavy ? HeavyDamage : LightDamage;
	PlayStrikeFeedback(Target, Strike, (EFuyuStrikeResult)Result, Dealt, bStrong);
}

void AFuyuSamurai::PlayStrikeFeedback(AActor* Victim, const FFuyuStrike& Strike, EFuyuStrikeResult Result, float Dealt, bool bStrong)
{
	AFuyuWorld* World = AFuyuWorld::Get(this);
	const float Now = GetWorld()->GetRealTimeSeconds();
	const FLinearColor StanceColor = FuyuCombat::StanceColor(Stance);
	const bool bSolo = GetNetMode() == NM_Standalone;
	const FVector VictimLocation = Victim ? Victim->GetActorLocation() : Strike.Location;

	if (Result == EFuyuStrikeResult::Guarded)
	{
		// steel on steel: a tight fan of white sparks and a small jolt, but no reward
		if (World)
		{
			World->SpawnSparks(Strike.Location, -Strike.Direction, FLinearColor(1.0f, 0.95f, 0.8f), 10, 620.0f);
		}
		KickCamera(0.25f);
		return;
	}

	// a landed hit extends the chain, and every fifth one pays out a little resolve
	HitChain = (Now - LastHitTime <= HitChainWindow) ? HitChain + 1 : 1;
	LastHitTime = Now;
	if (HitChain % 5 == 0)
	{
		if (HasAuthority())
		{
			Resolve = FMath::Min(Resolve + 0.25f, (float)MaxResolve);
		}
		else
		{
			ServerChainReward();
		}
	}

	const bool bKill = Result == EFuyuStrikeResult::Killed;
	const bool bBreak = Result == EFuyuStrikeResult::Broken;
	const float Weight = bKill ? 1.0f : (bBreak ? 0.85f : (Strike.bHeavy ? 0.7f : 0.4f));

	if (World)
	{
		// sparks fly on in the direction of the cut, in the colour of the stance
		World->SpawnSparks(Strike.Location, Strike.Direction, FMath::Lerp(FLinearColor::White, StanceColor, 0.6f), bKill ? 34 : (Strike.bHeavy ? 22 : 12), 520.0f + 520.0f * Weight);
		World->PunchScreen(Weight * (bKill ? 0.9f : 0.5f));

		if (bBreak)
		{
			World->SpawnSparks(Strike.Location, FVector::UpVector, FLinearColor(1.0f, 0.75f, 0.2f), 28, 900.0f);
		}

		if (bKill)
		{
			// a second, larger crescent crosses the first, and a fallen enemy scatters into leaves
			World->SpawnSlash(VictimLocation + FVector(0, 0, 25.0f), Strike.Direction, FMath::FRandRange(-60.0f, 60.0f), 2.1f, StanceColor);
			if (Cast<AFuyuEnemy>(Victim))
			{
				World->SpawnLeafBurst(VictimLocation + FVector(0, 0, 30.0f), 46);
			}
		}
	}

	KickCamera(Weight);
	DealtDamage(Strike.Damage, Strike.Location);

	// freeze on the frame of impact. Only when playing alone: slowing one player's clock in a shared game
	// would put her out of step with the host
	if (bSolo)
	{
		CustomTimeDilation = 0.04f;
		HitStopTimeLeft = Strike.bHeavy ? 0.12f : 0.07f;
	}

	if (AFuyuGameMode* GameMode = GetWorld()->GetAuthGameMode<AFuyuGameMode>())
	{
		GameMode->RequestMomentShot(bKill ? TEXT("kill") : (bBreak ? TEXT("break") : (Strike.bHeavy ? TEXT("heavy") : TEXT("hit"))));
	}

	// the hit sound climbs in pitch as the chain grows, which is most of what makes a long string feel good
	const float Pitch = 1.0f + 0.04f * FMath::Min(HitChain - 1, 12);
	UFuyuAudio::Play(this, EFuyuSound::Hit, Strike.Location, 1.0f, Pitch);
	if (bBreak)
	{
		UFuyuAudio::Play(this, EFuyuSound::Break, Strike.Location);
	}

	// numbers pop out of the target: gold when the stance is right or the target is reeling
	if (Dealt > 0.0f)
	{
		AFuyuHUD::Popup(this, Strike.Location + FVector(0, 0, 70.0f), FString::FromInt(FMath::RoundToInt32(Dealt)), bStrong ? FLinearColor(1.0f, 0.78f, 0.2f) : FLinearColor(0.96f, 0.93f, 0.86f), bStrong || Strike.bHeavy ? 30.0f : 22.0f);
	}

	if (bBreak)
	{
		AFuyuHUD::Popup(this, VictimLocation + FVector(0, 0, 150.0f), TEXT("GUARD BROKEN"), FLinearColor(1.0f, 0.78f, 0.2f), 24.0f);
	}

	if (bKill)
	{
		LastKillTime = Now;
		UFuyuAudio::Play(this, EFuyuSound::Kill, Strike.Location);

		// slow motion on a kill, longer for the last foe standing. Solo only, for the same reason as the freeze
		if (bSolo)
		{
			bool bOthersRemain = false;
			for (TActorIterator<AFuyuEnemy> It(GetWorld()); It; ++It)
			{
				if (!It->IsDead() && It->IsAggressive() && FVector::Dist(It->GetActorLocation(), GetActorLocation()) < 2600.0f)
				{
					bOthersRemain = true;
					break;
				}
			}
			SlowMotion(bOthersRemain ? 0.3f : 0.14f, bOthersRemain ? 0.22f : 0.7f);
		}
	}
}

// --- being struck -----------------------------------------------------------------------------

void AFuyuSamurai::ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse)
{
	const AFuyuEnemy* Enemy = Cast<AFuyuEnemy>(DamageCauser);
	float Dealt = 0.0f;
	TakeStrike(Damage, DamageCauser, DamageLocation, DamageImpulse, Enemy && Enemy->IsAttackUnblockable(), false, Dealt);
}

EFuyuStrikeResult AFuyuSamurai::TakeStrike(float Damage, AActor* Causer, const FVector& Location, const FVector& Impulse, bool bUnblockable, bool bHeavy, float& OutDealt)
{
	OutDealt = 0.0f;

	// nothing touches her while the ultimate plays out
	if (!HasAuthority() || IsDead() || IsUltimateActive())
	{
		return EFuyuStrikeResult::Missed;
	}

	// a rider who is struck at comes off her horse, and takes the blow on foot
	Dismount(false);

	// blows pass through the opening frames of a dodge
	if (IsDodging() && (DodgeDuration - DodgeTimeLeft) < DodgeInvulnerableTime)
	{
		return EFuyuStrikeResult::Missed;
	}

	// players can only hurt each other while they are duelling one another
	AFuyuSamurai* Rival = Cast<AFuyuSamurai>(Causer);
	if (Rival && (DuelOpponent != Rival || Rival->DuelOpponent != this))
	{
		return EFuyuStrikeResult::Missed;
	}

	// a fighter who has just broken out of a string cannot be touched for a moment
	const float Now = GetWorld()->GetTimeSeconds();
	if (Rival && Now < GraceUntil)
	{
		return EFuyuStrikeResult::Missed;
	}

	AFuyuEnemy* Enemy = Cast<AFuyuEnemy>(Causer);
	const FVector Direction = Impulse.GetSafeNormal2D();

	bool bFromFront = true;
	if (Causer)
	{
		const FVector ToAttacker = (Causer->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		bFromFront = FVector::DotProduct(ToAttacker, GetActorForwardVector()) > -0.1f;

		// with no lock-on the guard still turns to meet the attacker, as long as they are not behind
		if (bBlocking && bFromFront)
		{
			SetActorRotation(ToAttacker.Rotation());
		}
	}

	if (bBlocking && bFromFront && !bUnblockable)
	{
		// a remote player's guard reaches the host a moment late, so she is given a little grace
		const float Window = ParryWindow + (IsLocallyControlled() ? 0.0f : 0.1f);
		const float GuardHeld = GetWorld()->GetTimeSeconds() - BlockStartTime;

		if (GuardHeld <= Window)
		{
			// a perfect parry: the blow is turned aside and the attacker is left open
			Resolve = FMath::Min(Resolve + 0.34f, (float)MaxResolve);
			if (Enemy)
			{
				Enemy->OnParried(this);
			}
			if (Rival)
			{
				Rival->StunnedUntil = GetWorld()->GetTimeSeconds() + 0.9f;
				Rival->ClientStagger(0.9f);
			}
			MulticastDamaged(2, Location, Direction);
			return EFuyuStrikeResult::Guarded;
		}

		if (!(bHeavy && Rival))
		{
			// an ordinary block holds, but shoves the samurai back
			GetCharacterMovement()->AddImpulse(Direction * 260.0f, true);
			MulticastDamaged(1, Location, Direction);
			return EFuyuStrikeResult::Guarded;
		}

		// another player's heavy cut drives through a plain guard, though much weakened
		Damage *= 0.4f;
	}

	// a duel is fought to the yield, not the death: the loser is spared
	if (Rival && CurrentHP - Damage <= 0.0f)
	{
		OutDealt = Damage;
		MulticastDamaged(4, Location, Direction);
		EndDuel(Rival, this, false);
		return EFuyuStrikeResult::Killed;
	}

	bBlocking = false;

	// the template launches a struck character into the air and blends in ragdoll physics until it lands,
	// which takes control away for a long, floaty moment. Here the samurai keeps her feet: she is shoved
	// back along the ground and flinches, and can act again almost at once
	FDamageEvent DamageEvent;
	TakeDamage(Damage, DamageEvent, nullptr, Causer);
	OutDealt = Damage;

	if (IsDead())
	{
		GetMesh()->AddImpulseAtLocation(Impulse * GetMesh()->GetMass(), Location);
		MulticastDamaged(3, Location, Direction);
		return EFuyuStrikeResult::Killed;
	}

	if (Rival)
	{
		// between players a light cut wounds without stopping the other's swing, so fighters trade blows
		// instead of the first hit deciding everything. A heavy cut still staggers
		RivalHits = (Now - LastRivalHitTime > 1.6f) ? 1 : RivalHits + 1;
		LastRivalHitTime = Now;

		if (RivalHits >= 3)
		{
			// three unanswered hits and she breaks free: the attacker is thrown back and briefly cannot strike
			RivalHits = 0;
			GraceUntil = Now + 0.8f;
			Rival->StunnedUntil = Now + 0.6f;
			Rival->ClientStagger(0.6f);
			Rival->GetCharacterMovement()->AddImpulse(-Direction * 520.0f, true);
		}

		GetCharacterMovement()->AddImpulse(Direction * (bHeavy ? 380.0f : 140.0f), true);
		MulticastDamaged(bHeavy ? 0 : 6, Location, Direction);
		return EFuyuStrikeResult::Hit;
	}

	GetCharacterMovement()->AddImpulse(Direction * FMath::Min(Impulse.Size2D(), 420.0f), true);
	MulticastDamaged(0, Location, Direction);
	return EFuyuStrikeResult::Hit;
}

void AFuyuSamurai::MulticastDamaged_Implementation(uint8 Kind, FVector Location, FVector Direction)
{
	AFuyuWorld* World = AFuyuWorld::Get(this);
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	const bool bMine = IsLocallyControlled();

	switch (Kind)
	{
	case 1:
		// blocked
		UFuyuAudio::Play(this, EFuyuSound::Guard, Location);
		if (ImpactFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ImpactFX, Location);
		}
		return;

	case 2:
		// parried: time hangs for a beat when playing alone
		UFuyuAudio::Play(this, EFuyuSound::Clang, Location);
		if (World)
		{
			World->SpawnSparks(Location, FVector::UpVector, FLinearColor(1.0f, 0.95f, 0.75f), 30, 950.0f);
		}
		if (bMine)
		{
			LastParryTime = GetWorld()->GetRealTimeSeconds();
			KickCamera(0.6f);
			if (World)
			{
				World->PunchScreen(0.7f);
			}
			if (GetNetMode() == NM_Standalone)
			{
				SlowMotion(0.25f, 0.28f);
			}
		}
		return;

	case 5:
		// a duel begins
		UFuyuAudio::Play(this, EFuyuSound::Drum, GetActorLocation());
		return;

	case 6:
		// grazed by another player: it hurts, but her own swing carries on
		GetMesh()->SetPhysicsBlendWeight(0.0f);
		UFuyuAudio::Play(this, EFuyuSound::Hit, Location);
		if (World)
		{
			World->SpawnSparks(Location, Direction, FLinearColor(1.0f, 0.2f, 0.1f), 10, 520.0f);
		}
		if (bMine)
		{
			HitChain = 0;
			KickCamera(0.5f);
			if (World)
			{
				World->PunchScreen(0.4f);
			}
		}
		return;

	default:
		break;
	}

	// hit, killed, or beaten in a duel
	bBlocking = false;
	StopBlockPose();
	GetMesh()->SetPhysicsBlendWeight(0.0f);

	if (Anim)
	{
		Anim->Montage_Stop(0.1f, ComboAttackMontage);
		Anim->Montage_Stop(0.1f, ChargedAttackMontage);
	}

	UFuyuAudio::Play(this, EFuyuSound::Hit, Location);
	if (World)
	{
		World->SpawnSparks(Location, Direction, FLinearColor(1.0f, 0.2f, 0.1f), 14, 600.0f);
	}

	if (bMine)
	{
		// being cut ends the chain
		HitChain = 0;
		Buffered = EBuffered::None;
		FacingTimeLeft = 0.0f;
		LungeTimeLeft = 0.0f;
		KickCamera(0.8f);
		if (World)
		{
			World->PunchScreen(0.6f);
		}
	}

	if (Kind == 3)
	{
		// the host has already run the death; everyone else falls here
		if (!HasAuthority())
		{
			CurrentHP = 0.0f;
			HandleDeath();
			GetMesh()->AddImpulseAtLocation(Direction * 500.0f * GetMesh()->GetMass(), Location);
		}
		return;
	}

	if (Anim && HitReactAnims.Num() > 0 && ComboAttackMontage && ComboAttackMontage->SlotAnimTracks.Num() > 0)
	{
		Anim->PlaySlotAnimationAsDynamicMontage(HitReactAnims[FMath::RandHelper(HitReactAnims.Num())], ComboAttackMontage->SlotAnimTracks[0].SlotName, 0.05f, 0.18f, Kind == 4 ? 0.7f : 1.5f);
	}
}

void AFuyuSamurai::ClientStagger_Implementation(float Seconds)
{
	// her cut was parried: the swing is thrown off and she cannot attack again for a moment
	StunnedUntil = GetWorld()->GetTimeSeconds() + Seconds;
	Buffered = EBuffered::None;

	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		Anim->Montage_Stop(0.08f, ComboAttackMontage);
		Anim->Montage_Stop(0.08f, ChargedAttackMontage);
		if (HitReactAnims.Num() > 0 && ComboAttackMontage && ComboAttackMontage->SlotAnimTracks.Num() > 0)
		{
			Anim->PlaySlotAnimationAsDynamicMontage(HitReactAnims.Last(), ComboAttackMontage->SlotAnimTracks[0].SlotName, 0.05f, 0.2f, 0.9f);
		}
	}
	KickCamera(0.5f);
}

// --- duels ------------------------------------------------------------------------------------

void AFuyuSamurai::ChallengePressed()
{
	if (!IsDead())
	{
		ServerChallenge();
	}
}

void AFuyuSamurai::ClientNotice_Implementation(const FString& Title, const FString& Subtitle)
{
	NoticeTitle = Title;
	NoticeSubtitle = Subtitle;
	NoticeTime = GetWorld()->GetRealTimeSeconds();
}

void AFuyuSamurai::ServerChallenge_Implementation()
{
	if (IsDead())
	{
		return;
	}

	// pressed during a duel, the same key yields it
	if (DuelOpponent)
	{
		EndDuel(DuelOpponent, this, true);
		return;
	}

	AFuyuSamurai* Nearest = nullptr;
	float NearestDistance = 1500.0f;
	for (TActorIterator<AFuyuSamurai> It(GetWorld()); It; ++It)
	{
		const float Distance = FVector::Dist(It->GetActorLocation(), GetActorLocation());
		if (*It != this && !It->IsDead() && Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = *It;
		}
	}

	if (!Nearest)
	{
		ClientNotice(TEXT("NO ONE TO CHALLENGE"), TEXT("Stand near another player and press V."));
		UE_LOG(LogFuyuNet, Log, TEXT("%s looked for someone to challenge but nobody was near"), *GetPlayerLabel());
		return;
	}

	if (Nearest->DuelOpponent)
	{
		ClientNotice(TEXT("ALREADY DUELLING"), Nearest->GetPlayerLabel() + TEXT(" is in a duel."));
		return;
	}

	// answering a challenge that is already on the table accepts it
	if (ChallengedBy == Nearest)
	{
		StartDuel(Nearest);
		return;
	}

	Nearest->ChallengedBy = this;
	Nearest->ChallengeTime = GetWorld()->GetTimeSeconds();
	ClientNotice(TEXT("CHALLENGE SENT"), TEXT("Waiting for ") + Nearest->GetPlayerLabel() + TEXT(" to accept."));
	Nearest->ClientNotice(GetPlayerLabel().ToUpper() + TEXT(" CHALLENGES YOU"), TEXT("Press V to accept the duel."));
	UE_LOG(LogFuyuNet, Log, TEXT("%s challenged %s"), *GetPlayerLabel(), *Nearest->GetPlayerLabel());
}

void AFuyuSamurai::StartDuel(AFuyuSamurai* Other)
{
	AFuyuSamurai* Fighters[2] = { this, Other };
	for (int32 i = 0; i < 2; ++i)
	{
		// both start fresh, so the duel is decided by the fight and not by what came before it
		AFuyuSamurai* Fighter = Fighters[i];
		Fighter->DuelOpponent = Fighters[1 - i];
		Fighter->ChallengedBy = nullptr;
		Fighter->CurrentHP = Fighter->MaxHP;
		Fighter->ClientNotice(TEXT("DUEL"), TEXT("Against ") + Fighters[1 - i]->GetPlayerLabel() + TEXT(". First to fall yields. V to give up."));
	}

	MulticastDamaged(5, GetActorLocation(), FVector::ForwardVector);
	UE_LOG(LogFuyuNet, Log, TEXT("Duel begun between %s and %s"), *GetPlayerLabel(), *Other->GetPlayerLabel());
}

void AFuyuSamurai::EndDuel(AFuyuSamurai* Winner, AFuyuSamurai* Loser, bool bYielded)
{
	if (!Winner || !Loser)
	{
		return;
	}

	Winner->DuelOpponent = nullptr;
	Loser->DuelOpponent = nullptr;

	// the loser is left standing with a third of her health; the winner takes heart from it
	Loser->CurrentHP = Loser->MaxHP * 0.35f;
	Loser->StunnedUntil = Loser->GetWorld()->GetTimeSeconds() + 1.2f;
	Loser->ClientStagger(1.2f);
	Winner->Restore(0.3f, 1.0f);

	Winner->ClientNotice(TEXT("DUEL WON"), Loser->GetPlayerLabel() + (bYielded ? TEXT(" gives up.") : TEXT(" yields.")));
	Loser->ClientNotice(TEXT("DUEL LOST"), Winner->GetPlayerLabel() + TEXT(" has the better of you today."));
	UE_LOG(LogFuyuNet, Log, TEXT("Duel ended: %s beat %s%s"), *Winner->GetPlayerLabel(), *Loser->GetPlayerLabel(), bYielded ? TEXT(" (yielded)") : TEXT(""));
}

// --- self-play for a joining player -----------------------------------------------------------

void AFuyuSamurai::ClientBotTick(float DeltaSeconds)
{
	// -FuyuClientBot drives a joined player through the same input paths a person would use, so that the
	// client side of the networking can be checked without anyone at the keyboard
	static const bool bEnabled = FParse::Param(FCommandLine::Get(), TEXT("FuyuClientBot"));
	if (!bEnabled || HasAuthority() || !IsLocallyControlled() || IsDead())
	{
		return;
	}

	ClientBotTime += DeltaSeconds;
	ClientBotAttackTimer -= DeltaSeconds;

	float Lifetime = 100.0f;
	FParse::Value(FCommandLine::Get(), TEXT("FuyuClientBotTime="), Lifetime);

	if (!bClientBotShot && ClientBotTime > Lifetime * 0.55f)
	{
		bClientBotShot = true;
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tour"), TEXT("client_view.png")), true, false);
	}

	if (ClientBotTime > Lifetime)
	{
		UKismetSystemLibrary::QuitGame(this, Cast<APlayerController>(GetController()), EQuitPreference::Quit, false);
		return;
	}

	// fight the duel opponent if there is one, else the nearest enemy, else stay close to the host
	AActor* Target = DuelOpponent;
	float Best = 6000.0f;
	if (!Target)
	{
		for (TActorIterator<AFuyuEnemy> It(GetWorld()); It; ++It)
		{
			const float Distance = FVector::Dist(It->GetActorLocation(), GetActorLocation());
			if (!It->IsDead() && Distance < Best)
			{
				Best = Distance;
				Target = *It;
			}
		}
	}

	AFuyuSamurai* Host = nullptr;
	for (TActorIterator<AFuyuSamurai> It(GetWorld()); It; ++It)
	{
		if (*It != this)
		{
			Host = *It;
		}
	}

	// partway through, challenge the host to a duel
	if (!DuelOpponent && Host && ClientBotTime > Lifetime * 0.3f && FMath::Fmod(ClientBotTime, 8.0f) < DeltaSeconds)
	{
		ChallengePressed();
	}

	const bool bFight = Target != nullptr;
	if (!Target)
	{
		Target = Host;
	}
	if (!Target)
	{
		return;
	}

	const FVector To = Target->GetActorLocation() - GetActorLocation();
	const float Distance = To.Size2D();
	if (Distance > (bFight ? 210.0f : 420.0f))
	{
		const bool bSprint = Distance > 1500.0f;
		if (bSprint != bSprinting)
		{
			bSprint ? SprintPressed() : SprintReleased();
		}
		AddMovementInput(To.GetSafeNormal2D(), 1.0f);
	}
	else if (bFight && ClientBotAttackTimer <= 0.0f)
	{
		ClientBotAttackTimer = 0.34f;
		DoComboAttackStart();
	}
}
