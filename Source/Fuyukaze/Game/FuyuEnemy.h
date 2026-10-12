#pragma once

#include "CoreMinimal.h"
#include "CombatEnemy.h"
#include "FuyuTypes.h"
#include "FuyuEnemy.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UAnimSequenceBase;
class UNiagaraSystem;
class AFuyuSamurai;

UENUM()
enum class EFuyuEnemyState : uint8
{
	/** Standing guard or pottering about the post */
	Idle,
	/** Closing on the player */
	Approach,
	/** Holding at range, waiting for a turn to attack */
	Circle,
	/** Telegraphing a blow */
	Windup,
	/** Swinging */
	Attack,
	/** Catching breath after a string */
	Recover,
	/** Reeling from a hit */
	HitStun,
	/** Guard broken, wide open */
	Staggered,
	/** Walking back to the post after losing the player */
	Return,
	Dead
};

/**
 *  An opponent driven by a small state machine rather than the template's StateTree, so that
 *  fights have the rhythm of a sword duel: enemies ring the player, take turns to attack,
 *  telegraph each blow with a glint, guard against careless cuts and stagger when their posture breaks.
 */
UCLASS(abstract)
class AFuyuEnemy : public ACombatEnemy
{
	GENERATED_BODY()

public:

	AFuyuEnemy();

	/** Sets stats, weapon and look for this kind of opponent. Call straight after spawning */
	void Configure(EFuyuEnemyType InType, FName InSiteId, const FVector& InHome);

	/** Resolves a blow from the player: may be guarded, may stagger, may kill */
	EFuyuStrikeResult ReceiveStrike(const FFuyuStrike& Strike);

	/** Called when the player turns this enemy's blow aside with a perfect parry */
	void OnParried(AActor* By);

	EFuyuEnemyType GetEnemyType() const { return Type; }
	EFuyuEnemyState GetState() const { return State; }
	FName GetSiteId() const { return SiteId; }
	bool IsDead() const { return State == EFuyuEnemyState::Dead; }
	bool IsStaggered() const { return State == EFuyuEnemyState::Staggered; }
	bool IsAttackUnblockable() const { return bUnblockableAttack; }
	bool IsAggressive() const { return State != EFuyuEnemyState::Idle && State != EFuyuEnemyState::Return && State != EFuyuEnemyState::Dead; }
	float GetHealthFraction() const { return MaxHP > 0.0f ? FMath::Clamp(CurrentHP / MaxHP, 0.0f, 1.0f) : 0.0f; }
	float GetPostureFraction() const { return MaxPosture > 0.0f ? FMath::Clamp(Posture / MaxPosture, 0.0f, 1.0f) : 0.0f; }
	FText GetDisplayName() const;

	/** Stops this enemy dead for a while, as if time had left it behind. Used by the samurai's ultimate */
	void Freeze(float Seconds);

	/** Damage dealt by the most recent blow that got through, for the attacker's feedback */
	float GetLastDamageTaken() const { return LastDamageTaken; }

	// ~begin ACombatEnemy
	virtual void DoAttackTrace(FName DamageSourceBone) override;
	virtual void ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse) override;
	virtual void HandleDeath() override;
	// ~end ACombatEnemy

	/** How many enemies may press an attack on the player at the same moment */
	static constexpr int32 MaxSimultaneousAttackers = 2;

protected:

	virtual void PreInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	void SetState(EFuyuEnemyState NewState);
	void TickIdle(float DeltaSeconds, AFuyuSamurai* Player, float Distance);
	void TickApproach(float DeltaSeconds, AFuyuSamurai* Player, float Distance);
	void TickCircle(float DeltaSeconds, AFuyuSamurai* Player, float Distance);
	void TickWindup(float DeltaSeconds, AFuyuSamurai* Player, float Distance);

	void BeginWindup();
	void BeginAttack();
	void HandleAttackFinished();

	/** Walks towards a point, steering round obstacles and other enemies */
	void MoveTowards(const FVector& Target, float Speed);
	void FaceTowards(const FVector& Target, float DeltaSeconds, float RateDegPerSec = 540.0f);

	bool TryTakeAttackToken();
	void ReleaseAttackToken();

	void PlayReaction(bool bHeavy);
	void ShowGlint(bool bUnblockable);
	void UpdateGlint(float DeltaSeconds);
	void Die(const FVector& Impulse, const FVector& Location);

protected:

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Weapon;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Hat;

	/** Bright star that flares on the weapon to warn of an incoming blow */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Glint;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UPointLightComponent> GlintLight;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequenceBase>> HitReactions;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> HeavyReaction;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> ImpactFX;

	UPROPERTY(ReplicatedUsing = OnRep_Type)
	EFuyuEnemyType Type = EFuyuEnemyType::Ronin;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	EFuyuEnemyState State = EFuyuEnemyState::Idle;
	FName SiteId;
	FVector Home = FVector::ZeroVector;
	FVector WanderTarget = FVector::ZeroVector;

	/** Steering direction, low-pass filtered so avoidance does not make enemies twitch */
	FVector SteerDirection = FVector::ZeroVector;

	float StateTime = 0.0f;
	float MaxPosture = 60.0f;
	UPROPERTY(Replicated)
	float Posture = 0.0f;
	float LastPostureHitTime = -100.0f;

	float AttackDamage = 14.0f;
	float AttackReach = 190.0f;
	float AttackRange = 185.0f;
	float WalkSpeed = 330.0f;
	float CircleDistance = 480.0f;
	float GuardChance = 0.3f;
	float HeavyChance = 0.15f;
	float AttackRate = 1.0f;
	float SightRange = 2400.0f;
	float LeashRange = 7000.0f;
	bool bFlinches = true;

	float AttackCooldown = 0.0f;
	float WindupDuration = 0.5f;
	float CircleSign = 1.0f;
	float NextCircleFlip = 0.0f;
	bool bHasToken = false;
	UPROPERTY(Replicated)
	bool bUnblockableAttack = false;
	bool bHeavyAttack = false;
	bool bHitThisSwing = false;

	float GlintTime = -1.0f;

	/** Materials dyed for this enemy type, kept so a hit can flash them white */
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UMaterialInstanceDynamic>> BodyMaterials;

	FLinearColor BodyColor = FLinearColor::Black;
	float FlashTimeLeft = 0.0f;
	float LastDamageTaken = 0.0f;
	float HitStopTimeLeft = 0.0f;

	/** Freezes this enemy for a few frames and flashes it white */
	void Impact(float FreezeSeconds);

	// --- multiplayer -----------------------------------------------------------------------------
	// Enemies think on the host only. Other machines are sent their state, and cues for what to show.

public:

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	/** The living player nearest this enemy */
	AFuyuSamurai* FindPrey() const;

	/** What a joined player's machine does each frame in place of the state machine */
	void ClientTick(float DeltaSeconds);

	/** Sends a cue to the other machines, if there are any */
	void Cue(uint8 Kind, const FVector& Location = FVector::ZeroVector);

	UFUNCTION()
	void OnRep_Type();

	UFUNCTION()
	void OnRep_State();

	/** 0 white glint, 1 red glint, 2 light flinch, 3 heavy flinch, 4 light impact, 5 heavy impact, 6 guard */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastCue(uint8 Kind, FVector Location);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastAttack(bool bHeavy, uint8 Count, float Rate);

	UPROPERTY(Replicated)
	float RepHP = 60.0f;

	float ShownHP = -1.0f;
	bool bClientDead = false;

	/** Enemies currently holding permission to attack */
	static TArray<TWeakObjectPtr<AFuyuEnemy>> AttackTokens;
};
