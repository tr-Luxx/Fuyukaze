#pragma once

#include "CoreMinimal.h"
#include "CombatCharacter.h"
#include "FuyuTypes.h"
#include "FuyuSamurai.generated.h"

class UInputMappingContext;
class UStaticMeshComponent;
class UAnimSequenceBase;
class UNiagaraSystem;
class AFuyuEnemy;

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
	void PlayStrikeFeedback(AFuyuEnemy* Enemy, const FFuyuStrike& Strike, EFuyuStrikeResult Result);

	/** Shakes the camera and punches the field of view. Strength is roughly 0 to 1 */
	void KickCamera(float Strength);

	/** Drops the whole world into slow motion for a moment, timed in real seconds */
	void SlowMotion(float Dilation, float RealSeconds);
	void StopBlockPose();

	UInputAction* MakeAction(FName Name);

protected:

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Katana;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Hat;

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

	EFuyuStance Stance = EFuyuStance::Stone;
	float Resolve = 1.0f;

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
	TWeakObjectPtr<AFuyuEnemy> LungeTarget;
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
