#pragma once

#include "CoreMinimal.h"
#include "CombatCharacter.h"
#include "FuyuTypes.h"
#include "Engine/NetSerialization.h"
#include "FuyuSamurai.generated.h"

class UInputMappingContext;
class UStaticMeshComponent;
class UAnimSequenceBase;
class UNiagaraSystem;
class AFuyuEnemy;
class UInstancedStaticMeshComponent;
class USkeletalMeshComponent;

/**
 *  The player's wandering swordswoman. Builds on the template combat character with the
 *  things a duel needs: a guard with a tight parry window, a dodge with brief invulnerability,
 *  lock-on, three stances, and resolve earned in combat that can be spent to recover.
 */
UCLASS(abstract)
class AFuyuSamurai : public ACombatCharacter
{
	GENERATED_BODY()

public:

	AFuyuSamurai();

	// ~begin ACombatCharacter
	virtual void DoComboAttackStart() override;
	virtual void DoChargedAttackStart() override;
	virtual void DoChargedAttackEnd() override;
	virtual void DoAttackTrace(FName DamageSourceBone) override;
	virtual void CheckCombo() override;
	virtual void ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse) override;
	virtual void HandleDeath() override;
	virtual void DoMove(float Right, float Forward) override;
	// ~end ACombatCharacter

	float GetHealthFraction() const { return MaxHP > 0.0f ? FMath::Clamp(CurrentHP / MaxHP, 0.0f, 1.0f) : 0.0f; }
	float GetResolve() const { return Resolve; }
	int32 GetMaxResolve() const { return MaxResolve; }
	EFuyuStance GetStance() const { return Stance; }
	AFuyuEnemy* GetLockTarget() const { return LockTarget.Get(); }
	bool IsBlocking() const { return bBlocking; }
	bool IsDodging() const { return DodgeTimeLeft > 0.0f; }
	bool IsDead() const { return CurrentHP <= 0.0f; }

	/** Seconds since the last perfect parry, used by the HUD to flash */
	float GetTimeSinceParry() const;

	/** Unbroken run of landed hits, and how long ago the last one was. Drives the combo counter */
	int32 GetHitChain() const { return HitChain; }
	float GetTimeSinceHit() const;
	float GetTimeSinceKill() const;

	/** How long a run of hits survives without another landing */
	static constexpr float HitChainWindow = 3.0f;

	/** Rewards a kill with resolve */
	void NotifyKill(AFuyuEnemy* Enemy);

	/** Restores health and tops up resolve, used when a camp is freed */
	void Restore(float HealthFraction, float ResolveGain);

	// --- multiplayer -----------------------------------------------------------------------------
	// The host owns the truth: health, blocking, hits and duels are decided there. Each player's own
	// machine plays her animations and effects at once and tells the host what she did.

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The player this samurai is currently duelling, if any. Players can only hurt each other in a duel */
	AFuyuSamurai* GetDuelOpponent() const { return DuelOpponent; }

	/** A player who has challenged this one and is waiting for an answer */
	AFuyuSamurai* GetChallenger() const { return ChallengedBy; }

	/** "Player 2" and so on, by order of joining */
	FString GetPlayerLabel() const;

	/** Challenges the nearest player to a duel, accepts their challenge, or yields a duel in progress */
	void ChallengePressed();

	/**
	 *  Resolves a blow against this samurai on the host: it may be dodged, parried, blocked or land.
	 *  Used for enemy attacks and for another player's cuts during a duel
	 */
	EFuyuStrikeResult TakeStrike(float Damage, AActor* Causer, const FVector& Location, const FVector& Impulse, bool bUnblockable, bool bHeavy, float& OutDealt);

	/** Short private title card on this player's screen, and how long ago it was shown */
	const FString& GetNoticeTitle() const { return NoticeTitle; }
	const FString& GetNoticeSubtitle() const { return NoticeSubtitle; }
	float GetNoticeAge() const;

	void BotChallenge() { ChallengePressed(); }

	// --- ultimate: Winter Wind ---------------------------------------------------------------------
	// Spends a full resolve meter. The samurai gathers herself, blinks through every enemy nearby leaving a
	// trail of light, holds for a breath, and then all of them are cut at once.

	/** Starts the ultimate if resolve is full */
	void UltimatePressed();
	void BotUltimate() { UltimatePressed(); }

	bool IsUltimateActive() const { return UltStage != EUltStage::None; }
	bool IsUltimateReady() const { return !IsDead() && !IsUltimateActive() && Resolve >= (float)MaxResolve; }

	/** 0 to 1: how far the screen has closed in for the ultimate. Drives the letterbox bars and colour drain */
	float GetUltimateFocus() const { return UltFocus; }

	/** 0 to 1: opacity of the ultimate's title card, shown during the held breath and the release */
	float GetUltimateTitle() const { return UltTitle; }

	// --- riding ------------------------------------------------------------------------------------
	// E whistles for a horse, mounts one within reach (her own, or one taken from a herd), and gets down again.

	void HorsePressed();
	bool IsMounted() const { return HorseState == EFuyuHorse::Mounted; }

	/** What E would do right now, for the HUD: 0 whistle, 1 mount the horse within reach, 2 dismount */
	int32 GetHorsePrompt() const;

	/** Which horse of the herds she has taken for her own, so the island stops drawing it there. -1 for none */
	int32 GetTakenHorse() const { return TakenHorse; }

	// hooks for the self-play bot (-FuyuBot), which drives the same code paths as the input bindings
	void BotGuard(bool bGuard);
	void BotDodge() { DodgePressed(); }
	void BotSprint(bool bSprint) { bSprinting = bSprint; }
	void BotHeal() { HealPressed(); }
	void BotLock(bool bLock) { if (bLock != LockTarget.IsValid()) { LockPressed(); } }
	void BotSetStance(EFuyuStance NewStance) { SetStance(NewStance); }

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;

	void BlockPressed();
	void BlockReleased();
	void DodgePressed();
	void SprintPressed();
	void SprintReleased();
	void LockPressed();
	void HealPressed();
	void WindPressed();
	void CinemaPressed();
	void PassTimePressed();
	void StanceStone();
	void StanceWind();
	void StanceMoon();
	void SetStance(EFuyuStance NewStance);

	/** Picks the enemy the player most plausibly means: the lock target, else the nearest one roughly ahead */
	AFuyuEnemy* FindAttackTarget(float MaxDistance) const;

	/** Turns towards the target and closes a little distance so swings connect */
	void SnapToTarget();

	void UpdateLockOn(float DeltaSeconds);
	void UpdateCamera(float DeltaSeconds);
	void UpdateDodge(float DeltaSeconds);

	/** All the feedback for one landed blow: sparks, crescent, shake, freeze, sound, numbers */
	void PlayStrikeFeedback(AActor* Victim, const FFuyuStrike& Strike, EFuyuStrikeResult Result, float Dealt, bool bStrong);

	/** Shakes the camera and punches the field of view. Strength is roughly 0 to 1 */
	void KickCamera(float Strength);

	/** Drops the whole world into slow motion for a moment, timed in real seconds */
	void SlowMotion(float Dilation, float RealSeconds);
	void StopBlockPose();

	UInputAction* MakeAction(FName Name);

	// --- multiplayer internals -------------------------------------------------------------------

	void NetTick(float DeltaSeconds);

	enum class EUltStage : uint8 { None, Gather, Dash, Hold, Release };

	/** Picks targets and the path through them. Host only */
	void ResolveUltimate();
	void TickUltimate(float RealDelta);
	void UltimateDashStep(int32 Step);
	void UltimateStrike();
	void EndUltimate();

	UFUNCTION(Server, Reliable)
	void ServerUltimate();

	/** Starts the sequence on every machine: who is hit and the points the samurai blinks between */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastUltimate(const TArray<AActor*>& Targets, const TArray<FVector>& Waypoints);

	/** How far away an enemy may be and still be caught by the ultimate */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Ultimate", meta = (Units = "cm"))
	float UltimateRange = 1500.0f;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Ultimate", meta = (ClampMin = 1, ClampMax = 10))
	int32 UltimateMaxTargets = 6;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Ultimate")
	float UltimateDamage = 120.0f;

	/** Damage to another player caught by it during a duel */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Ultimate")
	float UltimateDuelDamage = 45.0f;

	EUltStage UltStage = EUltStage::None;
	float UltTime = 0.0f;
	int32 UltStepsDone = 0;
	bool bUltStruck = false;
	float UltFocus = 0.0f;
	float UltTitle = 0.0f;
	TArray<TWeakObjectPtr<AActor>> UltTargets;
	TArray<FVector> UltWaypoints;
	void ClientBotTick(float DeltaSeconds);
	void StartBlockPose();
	void SnapTo(AActor* Target);

	/** Sends an animation cue to the other machines: 0 combo section, 1 charge start, 2 charge release, 3 dodge */
	void SendAttackAnim(uint8 Kind, uint8 Section);

	/** Works out what a cut does to its target. Host only */
	void ResolveStrike(AActor* Target, bool bHeavy);

	void StartDuel(AFuyuSamurai* Other);
	static void EndDuel(AFuyuSamurai* Winner, AFuyuSamurai* Loser, bool bYielded);
	bool IsStunned() const;

	UFUNCTION()
	void OnRep_Blocking();

	UFUNCTION(Server, Reliable)
	void ServerSetBlocking(bool bNewBlocking);

	UFUNCTION(Server, Reliable)
	void ServerDodge(FVector Direction, bool bFaceDirection);

	UFUNCTION(Server, Reliable)
	void ServerSetSprinting(bool bNewSprinting);

	UFUNCTION(Server, Reliable)
	void ServerSetStance(EFuyuStance NewStance);

	UFUNCTION(Server, Reliable)
	void ServerHeal();

	UFUNCTION(Server, Reliable)
	void ServerChainReward();

	UFUNCTION(Server, Reliable)
	void ServerSnapTo(AActor* Target);

	UFUNCTION(Server, Reliable)
	void ServerAttackAnim(uint8 Kind, uint8 Section);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastAttackAnim(uint8 Kind, uint8 Section);

	UFUNCTION(Server, Reliable)
	void ServerStrike(AActor* Target, bool bHeavy);

	UFUNCTION(Client, Reliable)
	void ClientStrikeFeedback(AActor* Target, FVector Location, FVector Direction, bool bHeavy, uint8 Result, float Dealt, bool bStrong);

	/** What happened to this samurai, for every machine to show: 0 hit, 1 blocked, 2 parried, 3 killed, 4 lost a duel, 5 duel begun, 6 grazed in a duel */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastDamaged(uint8 Kind, FVector Location, FVector Direction);

	UFUNCTION(Server, Reliable)
	void ServerChallenge();

	UFUNCTION(Server, Reliable)
	void ServerPassTime();

	UFUNCTION(Server, Reliable)
	void ServerHorse(FVector Behind);

	void SetupRide();
	void TickRide(float DeltaSeconds);
	void MountAt(const FVector& Spot, float Yaw);
	void Dismount(bool bStepAside);

	/** Swaps between her own body and the one in the saddle */
	void ShowRide(bool bRiding);
	FVector GetFeet() const;

	UPROPERTY(Replicated)
	EFuyuHorse HorseState = EFuyuHorse::None;

	/** Where her horse stands, or has got to on its way to her, while she is not on it */
	UPROPERTY(Replicated)
	FVector_NetQuantize10 HorseSpot;

	UPROPERTY(Replicated)
	float HorseYaw = 0.0f;

	UPROPERTY(Replicated)
	int32 TakenHorse = -1;

	float HorseCalledAt = -100.0f;

	// what this machine is currently showing, which trails the replicated state smoothly
	EFuyuHorse ShownHorseState = EFuyuHorse::None;
	FVector ShownHorseSpot = FVector::ZeroVector;
	float ShownHorseYaw = 0.0f;
	float ShownGait = 0.0f;
	float SentGait = 0.0f;
	float HoofTimer = 0.0f;
	bool bRideShown = false;

	UFUNCTION(Client, Reliable)
	void ClientNotice(const FString& Title, const FString& Subtitle);

	UFUNCTION(Client, Reliable)
	void ClientStagger(float Seconds);

	UPROPERTY(Replicated)
	float RepHP = 100.0f;

	UPROPERTY(Replicated)
	TObjectPtr<AFuyuSamurai> DuelOpponent;

	UPROPERTY(Replicated)
	TObjectPtr<AFuyuSamurai> ChallengedBy;

	float ChallengeTime = -100.0f;

	// duel fairness: light cuts do not interrupt the other fighter, and three in a row throw the attacker off
	int32 RivalHits = 0;
	float LastRivalHitTime = -100.0f;
	float GraceUntil = -100.0f;
	float StunnedUntil = -100.0f;
	bool bTinted = false;

	FString NoticeTitle;
	FString NoticeSubtitle;
	float NoticeTime = -100.0f;

	float ClientBotTime = 0.0f;
	float ClientBotAttackTimer = 0.0f;
	bool bClientBotShot = false;

protected:

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Katana;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Hat;

	/** Her horse. It is placed in the world, not carried about with her, so it can stand where she left it */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UInstancedStaticMeshComponent> Steed;

	/** The body that sits in the saddle while she rides */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USkeletalMeshComponent> Rider;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Riding", meta = (Units = "cm/s"))
	float TrotSpeed = 700.0f;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Riding", meta = (Units = "cm/s"))
	float GallopSpeed = 1700.0f;

	/** How close a horse must be to be mounted */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Riding", meta = (Units = "cm"))
	float MountReach = 480.0f;

	/** How far off a whistled horse starts its run */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Riding", meta = (Units = "cm"))
	float WhistleDistance = 2600.0f;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Riding")
	float SteedScale = 1.1f;

	/** Hit points the samurai starts and respawns with */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Vitals", meta = (ClampMin = 1))
	float SamuraiMaxHP = 100.0f;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Vitals", meta = (ClampMin = 1, ClampMax = 6))
	int32 MaxResolve = 3;

	/** Share of max health restored by spending one resolve */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Vitals", meta = (ClampMin = 0, ClampMax = 1))
	float HealFraction = 0.4f;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Attack")
	float LightDamage = 15.0f;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Attack")
	float HeavyDamage = 34.0f;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Attack")
	float LightPostureDamage = 9.0f;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Attack")
	float HeavyPostureDamage = 30.0f;

	/** Reach of a sword cut from the centre of the body */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Attack", meta = (Units = "cm"))
	float SwordReach = 235.0f;

	/** Half-angle of the arc a cut covers */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Attack", meta = (Units = "deg"))
	float SwordArc = 70.0f;

	/** A guard raised within this long before a blow lands turns it into a parry */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Defence", meta = (Units = "s"))
	float ParryWindow = 0.24f;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Defence", meta = (Units = "s"))
	float DodgeDuration = 0.42f;

	/** Portion of the dodge during which blows pass harmlessly */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Defence", meta = (Units = "s"))
	float DodgeInvulnerableTime = 0.3f;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Defence", meta = (Units = "cm/s"))
	float DodgeSpeed = 1150.0f;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Defence", meta = (Units = "s"))
	float DodgeCooldown = 0.25f;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Movement", meta = (Units = "cm/s"))
	float RunSpeed = 470.0f;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Movement", meta = (Units = "cm/s"))
	float SprintSpeed = 800.0f;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Movement", meta = (Units = "cm/s"))
	float GuardSpeed = 230.0f;

	UPROPERTY(EditAnywhere, Category="Fuyukaze|Lock On", meta = (Units = "cm"))
	float LockRange = 2600.0f;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> FuyuMapping;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> FuyuActions;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> DodgeAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> GuardAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> GuardMontage;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> ImpactFX;

	TWeakObjectPtr<AFuyuEnemy> LockTarget;

	UPROPERTY(Replicated)
	EFuyuStance Stance = EFuyuStance::Stone;

	UPROPERTY(Replicated)
	float Resolve = 1.0f;

	UPROPERTY(ReplicatedUsing = OnRep_Blocking)
	bool bBlocking = false;

	bool bSprinting = false;
	float BlockStartTime = -100.0f;
	float LastParryTime = -100.0f;

	float DodgeTimeLeft = 0.0f;
	float DodgeCooldownLeft = 0.0f;
	FVector DodgeDirection = FVector::ForwardVector;

	/** Latest movement input in world space, so a dodge goes where the stick points */
	FVector LastMoveInput = FVector::ZeroVector;

	/** Last place the samurai stood on dry land, for recovering from a fall into the sea */
	FVector LastDryLocation = FVector::ZeroVector;

	/** Enemies already cut by the swing in progress, so one swing cannot hit the same foe twice */
	TArray<TWeakObjectPtr<AActor>> HitThisSwing;

	float HitStopTimeLeft = 0.0f;

	/** Whether the charged swing in progress has already made its sound */
	bool bHeavySwingSounded = false;

	/** An action pressed a moment too early. It is held briefly and performed as soon as it becomes possible */
	enum class EBuffered : uint8 { None, Light, Dodge };
	EBuffered Buffered = EBuffered::None;
	float BufferedAt = -100.0f;
	void Buffer(EBuffered Action);

	/** How long an early press is remembered */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Attack", meta = (Units = "s"))
	float InputBufferTime = 0.3f;

	/** Combo animations are played a little fast so the string feels crisp */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Attack")
	float ComboPlayRate = 1.18f;

	/** Speed the samurai may drift at mid-swing, so cuts feel planted instead of sliding */
	UPROPERTY(EditAnywhere, Category="Fuyukaze|Movement", meta = (Units = "cm/s"))
	float AttackMoveSpeed = 140.0f;

	// turning to face a target and closing the gap are eased over a few frames rather than snapped
	FRotator FacingGoal = FRotator::ZeroRotator;
	float FacingTimeLeft = 0.0f;
	TWeakObjectPtr<AActor> LungeTarget;
	float LungeTimeLeft = 0.0f;
	float LungeSpeed = 0.0f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequenceBase>> HitReactAnims;

	int32 HitChain = 0;
	float LastHitTime = -100.0f;
	float LastKillTime = -100.0f;

	/** Counts swings so successive cuts sweep along different lines */
	int32 SwingCount = 0;

	float CameraKick = 0.0f;

	/** Field of view before the impact punch is applied */
	float SmoothedFOV = 78.0f;
	FTimerHandle SlowMotionTimer;
};
