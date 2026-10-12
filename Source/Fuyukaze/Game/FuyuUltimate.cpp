// Winter Wind, the samurai's ultimate.
//
// There is no bespoke animation for it. The sequence is staged from what the game already has: a charge
// pose, the dash animation, a guard pose and the heavy cut, tied together with blink-dashes, trails of light,
// frozen enemies, camera moves, a drained colour grade and letterbox bars.
//
//   gather   she draws breath; the frame closes in and the colour leaves the world
//   dash     she blinks through each enemy in turn, leaving a line of light; each one freezes where it stands
//   hold     she stops beyond the last of them, blade raised, and nothing moves
//   release  one cut, and every enemy she passed is struck at once

#include "FuyuSamurai.h"
#include "FuyuAudio.h"
#include "FuyuEnemy.h"
#include "FuyuGameMode.h"
#include "FuyuHUD.h"
#include "FuyuTerrain.h"
#include "FuyuWorld.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr float GatherTime = 0.75f;
	constexpr float StepTime = 0.12f;
	constexpr float HoldTime = 0.6f;

	/** From the start of the release animation to the moment the cuts land */
	constexpr float StrikeDelay = 0.2f;

	/** From the start of the release to handing control back */
	constexpr float RecoverTime = 0.85f;

	const FLinearColor Frost(0.55f, 0.85f, 1.0f);
}

void AFuyuSamurai::UltimatePressed()
{
	if (IsUltimateReady() && !IsStunned() && !IsMounted())
	{
		ServerUltimate();
	}
}

void AFuyuSamurai::ServerUltimate_Implementation()
{
	if (IsUltimateReady())
	{
		ResolveUltimate();
	}
}

void AFuyuSamurai::ResolveUltimate()
{
	Resolve = 0.0f;
	bBlocking = false;

	// everything hostile within range: enemies, and the other player if a duel is on
	TArray<AActor*> Pool;
	for (TActorIterator<AFuyuEnemy> It(GetWorld()); It; ++It)
	{
		if (!It->IsDead() && FVector::Dist(It->GetActorLocation(), GetActorLocation()) < UltimateRange)
		{
			Pool.Add(*It);
		}
	}
	if (DuelOpponent && !DuelOpponent->IsDead() && FVector::Dist(DuelOpponent->GetActorLocation(), GetActorLocation()) < UltimateRange)
	{
		Pool.Add(DuelOpponent.Get());
	}

	// thread them nearest-first from wherever she last stood, so the path zigzags through the group
	TArray<AActor*> Targets;
	TArray<FVector> Waypoints;
	FVector From = GetActorLocation();
	Waypoints.Add(From);

	while (Pool.Num() > 0 && Targets.Num() < UltimateMaxTargets)
	{
		int32 Best = 0;
		for (int32 i = 1; i < Pool.Num(); ++i)
		{
			if (FVector::DistSquared(Pool[i]->GetActorLocation(), From) < FVector::DistSquared(Pool[Best]->GetActorLocation(), From))
			{
				Best = i;
			}
		}

		// each blink carries her through the target and out the far side
		AActor* Target = Pool[Best];
		Pool.RemoveAtSwap(Best);

		FVector Through = (Target->GetActorLocation() - From).GetSafeNormal2D();
		if (Through.IsNearlyZero())
		{
			Through = GetActorForwardVector();
		}

		FVector Beyond = Target->GetActorLocation() + Through * 190.0f;
		Beyond.Z = FuyuTerrain::Height(Beyond.X, Beyond.Y) + GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.0f;

		Targets.Add(Target);
		Waypoints.Add(Beyond);
		From = Beyond;
	}

	// with nobody to cut she still performs it: one long blink straight ahead
	if (Targets.Num() == 0)
	{
		FVector Ahead = GetActorLocation() + GetActorForwardVector().GetSafeNormal2D() * 900.0f;
		Ahead.Z = FuyuTerrain::Height(Ahead.X, Ahead.Y) + GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.0f;
		Waypoints.Add(Ahead);
	}

	MulticastUltimate(Targets, Waypoints);
}

void AFuyuSamurai::MulticastUltimate_Implementation(const TArray<AActor*>& Targets, const TArray<FVector>& Waypoints)
{
	UltTargets.Reset();
	for (AActor* Target : Targets)
	{
		UltTargets.Add(Target);
	}
	UltWaypoints = Waypoints;

	UltStage = EUltStage::Gather;
	UltTime = 0.0f;
	UltStepsDone = 0;
	bUltStruck = false;

	// whatever she was doing stops
	Buffered = EBuffered::None;
	DodgeTimeLeft = 0.0f;
	FacingTimeLeft = 0.0f;
	LungeTimeLeft = 0.0f;
	bBlocking = false;
	StopBlockPose();
	GetCharacterMovement()->StopMovementImmediately();

	// she turns to face the first of them and draws the blade back, as for a heavy cut
	if (UltWaypoints.Num() > 1)
	{
		SetActorRotation((UltWaypoints[1] - UltWaypoints[0]).GetSafeNormal2D().Rotation());
	}

	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		Anim->Montage_Stop(0.05f, ComboAttackMontage);
		bIsChargingAttack = true;
		Anim->Montage_Play(ChargedAttackMontage, 0.8f);
	}

	UFuyuAudio::Play(this, EFuyuSound::Charge, GetActorLocation());

	if (AFuyuWorld* World = AFuyuWorld::Get(this))
	{
		// the air around her stirs
		World->SpawnLeafBurst(GetActorLocation() - FVector(0, 0, 60.0f), 28);
		World->SpawnSparks(GetActorLocation(), FVector::UpVector, Frost, 26, 520.0f);
	}

	// alone, time itself slows while she gathers. In a shared game the clock belongs to everyone
	if (GetNetMode() == NM_Standalone)
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 0.45f);
	}

	if (AFuyuGameMode* GameMode = GetWorld()->GetAuthGameMode<AFuyuGameMode>())
	{
		GameMode->RequestMomentShot(TEXT("ult_1_gather"));
	}
}

void AFuyuSamurai::TickUltimate(float RealDelta)
{
	// ease the screen focus back out once the sequence is over
	if (UltStage == EUltStage::None)
	{
		if (UltFocus > 0.0f || UltTitle > 0.0f)
		{
			UltFocus = FMath::Max(UltFocus - RealDelta * 2.2f, 0.0f);
			UltTitle = FMath::Max(UltTitle - RealDelta * 1.6f, 0.0f);
			if (IsLocallyControlled())
			{
				if (AFuyuWorld* World = AFuyuWorld::Get(this))
				{
					World->SetFocusGrade(UltFocus);
				}
			}
		}
		return;
	}

	UltTime += RealDelta;

	const int32 Steps = FMath::Max(UltWaypoints.Num() - 1, 1);
	const float DashEnd = GatherTime + Steps * StepTime;
	const float HoldEnd = DashEnd + HoldTime;

	// she cannot be moved or steered while it plays out
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->Velocity = FVector(0.0f, 0.0f, Move->Velocity.Z);

	// the frame closes in over the gather and stays closed until the cut
	UltFocus = FMath::Min(UltFocus + RealDelta / (GatherTime * 0.8f), 1.0f);
	if (IsLocallyControlled())
	{
		if (AFuyuWorld* World = AFuyuWorld::Get(this))
		{
			World->SetFocusGrade(UltFocus);
		}
	}

	// the camera is directed too: in front of her as she gathers, high and side-on for the dash so the whole
	// zigzag of light is in frame, then in front again looking back past her at the enemies she has left frozen
	if (IsLocallyControlled() && GetController() && UltWaypoints.Num() > 1)
	{
		const float Facing = GetActorRotation().Yaw;
		const float PathYaw = (UltWaypoints.Last() - UltWaypoints[0]).GetSafeNormal2D().Rotation().Yaw;

		FRotator Want(-6.0f, Facing + 155.0f, 0.0f);
		float Speed = 6.0f;
		if (UltStage == EUltStage::Dash)
		{
			Want = FRotator(-44.0f, PathYaw + 90.0f, 0.0f);
			Speed = 13.0f;
		}
		else if (UltStage == EUltStage::Hold || UltStage == EUltStage::Release)
		{
			Want = FRotator(-11.0f, Facing + 150.0f, 0.0f);
			Speed = 8.0f;
		}

		GetController()->SetControlRotation(FMath::RInterpTo(GetController()->GetControlRotation(), Want, RealDelta, Speed));
	}

	// --- dash: one blink per step --------------------------------------------------------------
	while (UltStepsDone < Steps && UltTime >= GatherTime + UltStepsDone * StepTime)
	{
		if (UltStepsDone == 0)
		{
			UltStage = EUltStage::Dash;
			if (GetNetMode() == NM_Standalone)
			{
				UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
			}
			if (AFuyuGameMode* GameMode = GetWorld()->GetAuthGameMode<AFuyuGameMode>())
			{
				GameMode->RequestMomentShot(TEXT("ult_2_dash"));
			}
		}

		UltimateDashStep(UltStepsDone);
		++UltStepsDone;
	}

	// --- hold: blade raised, the world still ---------------------------------------------------
	if (UltStage == EUltStage::Dash && UltTime >= DashEnd)
	{
		UltStage = EUltStage::Hold;

		if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
		{
			if (GuardAnim && ComboAttackMontage && ComboAttackMontage->SlotAnimTracks.Num() > 0)
			{
				Anim->PlaySlotAnimationAsDynamicMontage(GuardAnim, ComboAttackMontage->SlotAnimTracks[0].SlotName, 0.12f, 0.1f, 0.6f, 4);
			}
		}

		UFuyuAudio::Play(this, EFuyuSound::Glint, GetActorLocation(), 1.0f, 0.7f);

		if (GetNetMode() == NM_Standalone)
		{
			UGameplayStatics::SetGlobalTimeDilation(this, 0.3f);
		}
		if (AFuyuGameMode* GameMode = GetWorld()->GetAuthGameMode<AFuyuGameMode>())
		{
			GameMode->RequestMomentShot(TEXT("ult_3_hold"));
		}
	}

	if (UltStage == EUltStage::Hold)
	{
		UltTitle = FMath::Min(UltTitle + RealDelta * 3.5f, 1.0f);
	}

	// --- release: the cut ------------------------------------------------------------------------
	if (UltStage == EUltStage::Hold && UltTime >= HoldEnd)
	{
		UltStage = EUltStage::Release;

		if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
		{
			bIsChargingAttack = false;
			Anim->Montage_Play(ChargedAttackMontage, 1.5f);
			Anim->Montage_JumpToSection(ChargeAttackSection, ChargedAttackMontage);
		}

		UFuyuAudio::Play(this, EFuyuSound::HeavySwing, GetActorLocation(), 1.0f, 0.8f);
	}

	if (UltStage == EUltStage::Release && !bUltStruck && UltTime >= HoldEnd + StrikeDelay)
	{
		bUltStruck = true;
		UltimateStrike();
	}

	if (UltStage == EUltStage::Release && UltTime >= HoldEnd + RecoverTime)
	{
		EndUltimate();
	}
}

void AFuyuSamurai::UltimateDashStep(int32 Step)
{
	if (!UltWaypoints.IsValidIndex(Step + 1))
	{
		return;
	}

	const FVector From = UltWaypoints[Step];
	const FVector To = UltWaypoints[Step + 1];
	const FVector Direction = (To - From).GetSafeNormal2D();

	// the host and her own machine both move her, so they agree without waiting on each other
	if (HasAuthority() || IsLocallyControlled())
	{
		TeleportTo(To, Direction.Rotation(), false, true);
		GetCharacterMovement()->StopMovementImmediately();
	}

	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		if (DodgeAnim && ComboAttackMontage && ComboAttackMontage->SlotAnimTracks.Num() > 0)
		{
			Anim->PlaySlotAnimationAsDynamicMontage(DodgeAnim, ComboAttackMontage->SlotAnimTracks[0].SlotName, 0.03f, 0.08f, 2.6f);
		}
	}

	AFuyuWorld* World = AFuyuWorld::Get(this);
	if (World)
	{
		// a line of light marks where she passed, at chest height, running a little past each end
		const FVector Lift(0, 0, 10.0f);
		World->SpawnTrail(From + Lift - Direction * 60.0f, To + Lift + Direction * 40.0f, FMath::Lerp(Frost, FuyuCombat::StanceColor(Stance), 0.35f), 2.2f);
		World->SpawnSparks(From, -Direction, Frost, 8, 420.0f);
	}

	// rising pitch with each blink, like a blade being drawn faster and faster
	UFuyuAudio::Play(this, EFuyuSound::Swing, To, 0.9f, 1.15f + 0.09f * Step);

	// the one she passed through freezes where it stands, marked by a small cut that has not yet opened
	if (UltTargets.IsValidIndex(Step))
	{
		if (AActor* Target = UltTargets[Step].Get())
		{
			const int32 StepsLeft = FMath::Max(UltWaypoints.Num() - 2 - Step, 0);
			if (AFuyuEnemy* Enemy = Cast<AFuyuEnemy>(Target))
			{
				Enemy->Freeze(StepsLeft * StepTime + HoldTime + StrikeDelay + 0.1f);
			}
			if (World)
			{
				World->SpawnSlash(Target->GetActorLocation() + FVector(0, 0, 25.0f), Direction, FMath::FRandRange(-70.0f, 70.0f), 0.9f, Frost);
			}
		}
	}

	if (IsLocallyControlled())
	{
		KickCamera(0.35f);
	}
}

void AFuyuSamurai::UltimateStrike()
{
	AFuyuWorld* World = AFuyuWorld::Get(this);
	const FLinearColor StanceColor = FuyuCombat::StanceColor(Stance);
	int32 Felled = 0;

	for (int32 i = 0; i < UltTargets.Num(); ++i)
	{
		AActor* Target = UltTargets[i].Get();
		if (!Target)
		{
			continue;
		}

		const FVector Direction = UltWaypoints.IsValidIndex(i + 1) ? (UltWaypoints[i + 1] - UltWaypoints[i]).GetSafeNormal2D() : GetActorForwardVector();
		const FVector Location = Target->GetActorLocation() + FVector(0, 0, 40.0f);

		// the cuts that were waiting all open together: two crossed crescents and a spray of frost on each
		if (World)
		{
			World->SpawnSlash(Location, Direction, 55.0f, 2.3f, FLinearColor::White);
			World->SpawnSlash(Location, Direction, -55.0f, 2.3f, StanceColor);
			World->SpawnSparks(Location, Direction, Frost, 36, 1100.0f);
			World->SpawnSparks(Location, FVector::UpVector, FLinearColor::White, 18, 800.0f);
		}

		// the host decides what the cut did
		if (HasAuthority())
		{
			EFuyuStrikeResult Result = EFuyuStrikeResult::Missed;
			float Dealt = 0.0f;

			if (AFuyuEnemy* Enemy = Cast<AFuyuEnemy>(Target))
			{
				FFuyuStrike Strike;
				Strike.Attacker = this;
				Strike.Location = Location;
				Strike.Direction = Direction;
				Strike.bHeavy = true;
				Strike.Stance = FuyuCombat::CounterStance(Enemy->GetEnemyType());
				Strike.Damage = UltimateDamage;
				Strike.PostureDamage = 200.0f;
				Result = Enemy->ReceiveStrike(Strike);
				Dealt = Enemy->GetLastDamageTaken();
			}
			else if (AFuyuSamurai* Rival = Cast<AFuyuSamurai>(Target))
			{
				Result = Rival->TakeStrike(UltimateDuelDamage, this, Location, Direction * 520.0f, true, true, Dealt);
			}

			Felled += Result == EFuyuStrikeResult::Killed ? 1 : 0;
			if (Dealt > 0.0f && IsLocallyControlled())
			{
				AFuyuHUD::Popup(this, Location + FVector(0, 0, 70.0f), FString::FromInt(FMath::RoundToInt32(Dealt)), FLinearColor(0.7f, 0.92f, 1.0f), 34.0f);
			}
		}
	}

	UFuyuAudio::Play(this, EFuyuSound::Kill, GetActorLocation(), 1.0f, 0.85f);
	UFuyuAudio::Play(this, EFuyuSound::Clang, GetActorLocation(), 1.0f, 0.75f);
	if (UltTargets.Num() > 0)
	{
		UFuyuAudio::Play(this, EFuyuSound::Break, GetActorLocation());
	}

	if (IsLocallyControlled())
	{
		LastKillTime = GetWorld()->GetRealTimeSeconds();
		KickCamera(1.4f);
		if (World)
		{
			World->PunchScreen(1.0f);
		}
	}

	// the breath is let out: time runs slow for one more beat, then returns
	if (GetNetMode() == NM_Standalone)
	{
		SlowMotion(0.2f, 0.45f);
	}

	if (HasAuthority())
	{
		UE_LOG(LogTemp, Log, TEXT("Fuyukaze: Winter Wind struck %d and felled %d"), UltTargets.Num(), Felled);
	}

	if (AFuyuGameMode* GameMode = GetWorld()->GetAuthGameMode<AFuyuGameMode>())
	{
		GameMode->RequestMomentShot(TEXT("ult_4_release"));
	}
}

void AFuyuSamurai::EndUltimate()
{
	UltStage = EUltStage::None;
	UltTargets.Reset();
	UltWaypoints.Reset();
	bIsChargingAttack = false;
}
