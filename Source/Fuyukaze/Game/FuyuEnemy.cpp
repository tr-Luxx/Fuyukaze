#include "FuyuEnemy.h"
#include "FuyuAudio.h"
#include "FuyuGameMode.h"
#include "FuyuProps.h"
#include "FuyuSamurai.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "CombatLifeBar.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "Fuyukaze"

TArray<TWeakObjectPtr<AFuyuEnemy>> AFuyuEnemy::AttackTokens;

AFuyuEnemy::AFuyuEnemy()
{
	Weapon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Weapon"));
	Weapon->SetupAttachment(GetMesh(), TEXT("hand_r"));
	Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Weapon->SetRelativeLocationAndRotation(FVector(-8.5f, 3.5f, 1.0f), FRotator(-90.0f, 0.0f, 0.0f));

	Hat = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hat"));
	Hat->SetupAttachment(GetMesh(), TEXT("head"));
	Hat->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Hat->SetRelativeLocationAndRotation(FVector(19.0f, 2.0f, 0.0f), FRotator(-90.0f, 0.0f, 0.0f));
	Hat->SetRelativeScale3D(FVector(0.92f));

	Glint = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Glint"));
	Glint->SetupAttachment(Weapon);
	Glint->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Glint->SetCastShadow(false);
	Glint->SetRelativeLocation(FVector(60.0f, 0.0f, 0.0f));
	Glint->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Glint->SetStaticMesh(SphereMesh.Object);
	}

	GlintLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlintLight"));
	GlintLight->SetupAttachment(Glint);
	GlintLight->SetCastShadows(false);
	GlintLight->SetAttenuationRadius(700.0f);
	GlintLight->SetIntensity(0.0f);

	// rotation is driven by the state machine so enemies can keep their eyes on the player while circling
	GetCharacterMovement()->bUseControllerDesiredRotation = false;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	GetCharacterMovement()->MaxAcceleration = 1400.0f;
	GetCharacterMovement()->SetWalkableFloorAngle(50.0f);
}

void AFuyuEnemy::PreInitializeComponents()
{
	// the template blueprint asks for a StateTree controller; these enemies think for themselves
	AIControllerClass = AAIController::StaticClass();

	Super::PreInitializeComponents();
}

void AFuyuEnemy::BeginPlay()
{
	Super::BeginPlay();

	OnAttackCompleted.BindUObject(this, &AFuyuEnemy::HandleAttackFinished);

	for (const TCHAR* Path : {
		TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Lgt_01.MM_HitReact_Front_Lgt_01"),
		TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Lgt_02.MM_HitReact_Front_Lgt_02"),
		TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Med_01.MM_HitReact_Front_Med_01") })
	{
		if (UAnimSequenceBase* Anim = LoadObject<UAnimSequenceBase>(nullptr, Path))
		{
			HitReactions.Add(Anim);
		}
	}

	HeavyReaction = LoadObject<UAnimSequenceBase>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Hvy_01.MM_HitReact_Front_Hvy_01"));
	ImpactFX = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Variant_Combat/VFX/NS_Damage.NS_Damage"));

	Glint->SetMaterial(0, FFuyuMaterials::Get().Glow);

	Home = GetActorLocation();
	WanderTarget = Home;
	CircleSign = FMath::RandBool() ? 1.0f : -1.0f;
	Configure(Type, SiteId, Home);
}

void AFuyuEnemy::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	ReleaseAttackToken();
	Super::EndPlay(EndPlayReason);
}

FText AFuyuEnemy::GetDisplayName() const
{
	switch (Type)
	{
	case EFuyuEnemyType::Spearman: return LOCTEXT("EnemySpear", "Ashigaru Spearman");
	case EFuyuEnemyType::Brute:    return LOCTEXT("EnemyBrute", "Iron-Club Brute");
	case EFuyuEnemyType::Duelist:  return LOCTEXT("EnemyDuelist", "Raiko, Blade of the First Snow");
	default:                       return LOCTEXT("EnemyRonin", "Ronin");
	}
}

void AFuyuEnemy::Configure(EFuyuEnemyType InType, FName InSiteId, const FVector& InHome)
{
	Type = InType;
	SiteId = InSiteId;
	Home = InHome;
	WanderTarget = InHome;

	FuyuProps::EProp WeaponProp = FuyuProps::EProp::Katana;
	FLinearColor Cloth(0.03f, 0.04f, 0.09f);
	float BodyScale = 1.0f;
	bool bHat = false;

	switch (Type)
	{
	case EFuyuEnemyType::Spearman:
		MaxHP = 50.0f; MaxPosture = 50.0f; AttackDamage = 16.0f;
		AttackReach = 275.0f; AttackRange = 255.0f; WalkSpeed = 320.0f; CircleDistance = 540.0f;
		GuardChance = 0.15f; HeavyChance = 0.3f; AttackRate = 1.0f; bFlinches = true;
		WeaponProp = FuyuProps::EProp::Spear; Cloth = FLinearColor(0.05f, 0.09f, 0.03f); bHat = true;
		break;

	case EFuyuEnemyType::Brute:
		MaxHP = 150.0f; MaxPosture = 120.0f; AttackDamage = 26.0f;
		AttackReach = 225.0f; AttackRange = 205.0f; WalkSpeed = 260.0f; CircleDistance = 430.0f;
		GuardChance = 0.0f; HeavyChance = 0.55f; AttackRate = 0.8f; bFlinches = false;
		WeaponProp = FuyuProps::EProp::Club; Cloth = FLinearColor(0.16f, 0.035f, 0.02f); BodyScale = 1.22f;
		break;

	case EFuyuEnemyType::Duelist:
		MaxHP = 340.0f; MaxPosture = 170.0f; AttackDamage = 20.0f;
		AttackReach = 205.0f; AttackRange = 190.0f; WalkSpeed = 410.0f; CircleDistance = 420.0f;
		GuardChance = 0.6f; HeavyChance = 0.3f; AttackRate = 1.2f; bFlinches = true;
		Cloth = FLinearColor(0.75f, 0.75f, 0.78f); BodyScale = 1.08f; bHat = true;
		SightRange = 3200.0f; LeashRange = 9000.0f;
		break;

	default:
		MaxHP = 60.0f; MaxPosture = 60.0f; AttackDamage = 14.0f;
		AttackReach = 200.0f; AttackRange = 185.0f; WalkSpeed = 340.0f; CircleDistance = 480.0f;
		GuardChance = 0.35f; HeavyChance = 0.12f; AttackRate = 1.0f; bFlinches = true;
		break;
	}

	CurrentHP = MaxHP;
	Posture = 0.0f;
	MinChargeLoops = 1;
	MaxChargeLoops = 2;
	AttackCooldown = FMath::FRandRange(0.4f, 2.0f);

	SetActorScale3D(FVector(BodyScale));
	Weapon->SetStaticMesh(FuyuProps::Get(WeaponProp));
	Hat->SetStaticMesh(bHat ? FuyuProps::Get(FuyuProps::EProp::Kasa) : nullptr);
	Glint->SetRelativeLocation(FVector(WeaponProp == FuyuProps::EProp::Spear ? 140.0f : 70.0f, 0.0f, 0.0f));

	// dye the mannequin so each kind of opponent can be told apart at a glance
	BodyColor = Cloth;
	BodyMaterials.Reset();
	for (int32 i = 0; i < GetMesh()->GetNumMaterials(); ++i)
	{
		if (UMaterialInstanceDynamic* Dyn = GetMesh()->CreateDynamicMaterialInstance(i))
		{
			Dyn->SetVectorParameterValue(TEXT("Paint Tint"), Cloth);
			BodyMaterials.Add(Dyn);
		}
	}

	if (LifeBarWidget)
	{
		LifeBarWidget->SetLifePercentage(1.0f);
		LifeBarWidget->SetBarColor(Type == EFuyuEnemyType::Duelist ? FLinearColor(0.9f, 0.75f, 0.2f) : FLinearColor(0.8f, 0.12f, 0.08f));
	}
}

void AFuyuEnemy::SetState(EFuyuEnemyState NewState)
{
	if (State == EFuyuEnemyState::Dead)
	{
		return;
	}

	State = NewState;
	StateTime = 0.0f;
}

bool AFuyuEnemy::TryTakeAttackToken()
{
	if (bHasToken)
	{
		return true;
	}

	AttackTokens.RemoveAll([](const TWeakObjectPtr<AFuyuEnemy>& E) { return !E.IsValid() || E->IsDead(); });

	if (AttackTokens.Num() >= MaxSimultaneousAttackers)
	{
		return false;
	}

	AttackTokens.Add(this);
	bHasToken = true;
	return true;
}

void AFuyuEnemy::ReleaseAttackToken()
{
	if (bHasToken)
	{
		AttackTokens.Remove(this);
		bHasToken = false;
	}
}

void AFuyuEnemy::FaceTowards(const FVector& Target, float DeltaSeconds, float RateDegPerSec)
{
	const FVector To = (Target - GetActorLocation()).GetSafeNormal2D();
	if (!To.IsNearlyZero())
	{
		SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), To.Rotation(), DeltaSeconds, RateDegPerSec));
	}
}

void AFuyuEnemy::MoveTowards(const FVector& Target, float Speed)
{
	GetCharacterMovement()->MaxWalkSpeed = Speed;

	FVector Dir = (Target - GetActorLocation()).GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		return;
	}

	// keep a shoulder's width from the other fighters so a group spreads out instead of stacking up
	for (TActorIterator<AFuyuEnemy> It(GetWorld()); It; ++It)
	{
		if (*It == this || It->IsDead())
		{
			continue;
		}

		const FVector Away = GetActorLocation() - It->GetActorLocation();
		const float Distance = Away.Size2D();
		if (Distance < 170.0f && Distance > KINDA_SMALL_NUMBER)
		{
			Dir += Away.GetSafeNormal2D() * (1.0f - Distance / 170.0f) * 1.6f;
		}
	}

	// slide round tents, palisades and trunks rather than walking into them
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FuyuEnemyMove), false, this);
	const FVector Start = GetActorLocation();
	if (GetWorld()->SweepSingleByChannel(Hit, Start, Start + Dir.GetSafeNormal2D() * 170.0f, FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(30.0f), Params) && Hit.ImpactNormal.Z < 0.55f)
	{
		const FVector Normal = Hit.ImpactNormal.GetSafeNormal2D();
		FVector Slide = FVector::CrossProduct(Normal, FVector::UpVector);
		if (FVector::DotProduct(Slide, Dir) < 0.0f)
		{
			Slide = -Slide;
		}
		Dir = Slide + Normal * 0.35f;
	}

	SteerDirection = FMath::VInterpTo(SteerDirection, Dir.GetSafeNormal2D(), GetWorld()->GetDeltaSeconds(), 9.0f);
	AddMovementInput(SteerDirection.GetSafeNormal2D(), FMath::Min(SteerDirection.Size2D() + 0.25f, 1.0f));
}

void AFuyuEnemy::Impact(float FreezeSeconds)
{
	CustomTimeDilation = 0.04f;
	HitStopTimeLeft = FreezeSeconds;
	FlashTimeLeft = 0.14f;
}

void AFuyuEnemy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// impact freeze and flash are timed in world seconds, because this actor's own clock is the thing being slowed
	const float WorldDelta = GetWorld()->GetDeltaSeconds();
	if (HitStopTimeLeft > 0.0f)
	{
		HitStopTimeLeft -= WorldDelta;
		if (HitStopTimeLeft <= 0.0f)
		{
			CustomTimeDilation = 1.0f;
		}
	}

	if (FlashTimeLeft > 0.0f)
	{
		FlashTimeLeft = FMath::Max(FlashTimeLeft - WorldDelta, 0.0f);
		const float Flash = FMath::Square(FlashTimeLeft / 0.14f);
		const FLinearColor Color = FMath::Lerp(BodyColor, FLinearColor(6.0f, 5.5f, 4.5f), Flash);
		for (UMaterialInstanceDynamic* Dyn : BodyMaterials)
		{
			if (Dyn)
			{
				Dyn->SetVectorParameterValue(TEXT("Paint Tint"), Color);
			}
		}
	}

	if (State == EFuyuEnemyState::Dead)
	{
		return;
	}

	StateTime += DeltaSeconds;
	AttackCooldown -= DeltaSeconds;
	UpdateGlint(DeltaSeconds);

	// posture recovers once the pressure is off
	if (State != EFuyuEnemyState::Staggered && GetWorld()->GetTimeSeconds() - LastPostureHitTime > 3.0f)
	{
		Posture = FMath::Max(Posture - 16.0f * DeltaSeconds, 0.0f);
	}

	AFuyuSamurai* Player = Cast<AFuyuSamurai>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Player || Player->IsDead())
	{
		if (IsAggressive() && State != EFuyuEnemyState::Attack)
		{
			ReleaseAttackToken();
			SetState(EFuyuEnemyState::Return);
		}
		Player = nullptr;
	}

	const float Distance = Player ? FVector::Dist2D(Player->GetActorLocation(), GetActorLocation()) : MAX_flt;

	// floating health bars only belong on enemies that are in the fight and close enough to matter
	const bool bShowBar = IsAggressive() && Distance < 3000.0f;
	if (LifeBar->bHiddenInGame == bShowBar)
	{
		LifeBar->SetHiddenInGame(!bShowBar);
	}

	switch (State)
	{
	case EFuyuEnemyState::Idle:
		TickIdle(DeltaSeconds, Player, Distance);
		break;

	case EFuyuEnemyState::Approach:
		TickApproach(DeltaSeconds, Player, Distance);
		break;

	case EFuyuEnemyState::Circle:
		TickCircle(DeltaSeconds, Player, Distance);
		break;

	case EFuyuEnemyState::Windup:
		TickWindup(DeltaSeconds, Player, Distance);
		break;

	case EFuyuEnemyState::Attack:
		if (Player)
		{
			// only a slow turn during the swing, so a well-timed sidestep makes it miss
			FaceTowards(Player->GetActorLocation(), DeltaSeconds, 140.0f);
		}
		if (StateTime > 7.0f)
		{
			HandleAttackFinished();
		}
		break;

	case EFuyuEnemyState::Recover:
		if (Player)
		{
			FaceTowards(Player->GetActorLocation(), DeltaSeconds, 300.0f);
		}
		if (StateTime > 0.9f / AttackRate)
		{
			SetState(EFuyuEnemyState::Circle);
		}
		break;

	case EFuyuEnemyState::HitStun:
		if (StateTime > 0.55f)
		{
			SetState(Player ? EFuyuEnemyState::Circle : EFuyuEnemyState::Return);
		}
		break;

	case EFuyuEnemyState::Staggered:
		if (StateTime > 2.6f)
		{
			Posture = 0.0f;
			SetState(Player ? EFuyuEnemyState::Circle : EFuyuEnemyState::Return);
		}
		break;

	case EFuyuEnemyState::Return:
	{
		MoveTowards(Home, WalkSpeed * 0.8f);
		FaceTowards(Home, DeltaSeconds);

		// lick wounds on the way back so a retreat is not a free kill later
		CurrentHP = FMath::Min(CurrentHP + MaxHP * 0.2f * DeltaSeconds, MaxHP);
		if (LifeBarWidget)
		{
			LifeBarWidget->SetLifePercentage(CurrentHP / MaxHP);
		}

		if (Player && Distance < SightRange * 0.5f && FVector::Dist2D(Home, GetActorLocation()) < LeashRange * 0.6f)
		{
			SetState(EFuyuEnemyState::Approach);
		}
		else if (FVector::Dist2D(Home, GetActorLocation()) < 180.0f)
		{
			SetState(EFuyuEnemyState::Idle);
		}
		break;
	}

	default:
		break;
	}
}

void AFuyuEnemy::TickIdle(float DeltaSeconds, AFuyuSamurai* Player, float Distance)
{
	// amble about the post
	if (StateTime > NextCircleFlip)
	{
		NextCircleFlip = StateTime + FMath::FRandRange(4.0f, 10.0f);
		const FVector2D Offset = FMath::RandPointInCircle(520.0f);
		WanderTarget = Home + FVector(Offset.X, Offset.Y, 0.0f);
	}

	if (FVector::Dist2D(WanderTarget, GetActorLocation()) > 90.0f)
	{
		MoveTowards(WanderTarget, 105.0f);
		FaceTowards(WanderTarget, DeltaSeconds, 220.0f);
	}

	if (!Player)
	{
		return;
	}

	// noticed if close, or if further off but in front
	const float Facing = FVector::DotProduct((Player->GetActorLocation() - GetActorLocation()).GetSafeNormal2D(), GetActorForwardVector());
	const bool bSeen = Distance < SightRange * 0.45f || (Distance < SightRange && Facing > 0.25f);
	if (!bSeen)
	{
		return;
	}

	// raise the alarm: everyone from the same camp within earshot joins in
	for (TActorIterator<AFuyuEnemy> It(GetWorld()); It; ++It)
	{
		if (It->State == EFuyuEnemyState::Idle && It->SiteId == SiteId && FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) < 3600.0f)
		{
			It->NextCircleFlip = 0.0f;
			It->SetState(EFuyuEnemyState::Approach);
		}
	}
}

void AFuyuEnemy::TickApproach(float DeltaSeconds, AFuyuSamurai* Player, float Distance)
{
	if (!Player)
	{
		return;
	}

	if (FVector::Dist2D(Home, GetActorLocation()) > LeashRange && Distance > 1500.0f)
	{
		SetState(EFuyuEnemyState::Return);
		return;
	}

	MoveTowards(Player->GetActorLocation(), WalkSpeed);
	FaceTowards(Player->GetActorLocation(), DeltaSeconds);

	if (Distance < CircleDistance + 60.0f)
	{
		SetState(EFuyuEnemyState::Circle);
	}
}

void AFuyuEnemy::TickCircle(float DeltaSeconds, AFuyuSamurai* Player, float Distance)
{
	if (!Player)
	{
		return;
	}

	const FVector PlayerLocation = Player->GetActorLocation();
	FaceTowards(PlayerLocation, DeltaSeconds);

	if (Distance > CircleDistance + 650.0f)
	{
		ReleaseAttackToken();
		SetState(EFuyuEnemyState::Approach);
		return;
	}

	// with a turn in hand, step in and strike
	if (AttackCooldown <= 0.0f && TryTakeAttackToken())
	{
		if (Distance <= AttackRange)
		{
			BeginWindup();
		}
		else
		{
			MoveTowards(PlayerLocation, WalkSpeed * 1.15f);
		}
		return;
	}

	// otherwise prowl round the player at arm's length, changing direction now and then
	if (StateTime > NextCircleFlip)
	{
		NextCircleFlip = StateTime + FMath::FRandRange(1.6f, 4.2f);
		CircleSign = FMath::RandBool() ? 1.0f : -1.0f;
	}

	const FVector Away = (GetActorLocation() - PlayerLocation).GetSafeNormal2D();
	const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Away) * CircleSign;
	const float RangeError = (Distance - CircleDistance) / 150.0f;
	const FVector Goal = GetActorLocation() + (Tangent - Away * FMath::Clamp(RangeError, -1.5f, 1.5f)) * 200.0f;
	MoveTowards(Goal, WalkSpeed * 0.42f);
}

void AFuyuEnemy::BeginWindup()
{
	bHeavyAttack = FMath::FRand() < HeavyChance;
	bUnblockableAttack = bHeavyAttack;
	bHitThisSwing = false;
	WindupDuration = (bHeavyAttack ? 0.6f : 0.42f) / AttackRate;

	ShowGlint(bUnblockableAttack);
	UFuyuAudio::Play(this, EFuyuSound::Glint, GetActorLocation(), bUnblockableAttack ? 1.0f : 0.6f);
	SetState(EFuyuEnemyState::Windup);
}

void AFuyuEnemy::TickWindup(float DeltaSeconds, AFuyuSamurai* Player, float Distance)
{
	if (Player)
	{
		FaceTowards(Player->GetActorLocation(), DeltaSeconds, 720.0f);
	}

	if (StateTime >= WindupDuration)
	{
		BeginAttack();
	}
}

void AFuyuEnemy::BeginAttack()
{
	SetState(EFuyuEnemyState::Attack);
	UFuyuAudio::Play(this, bHeavyAttack ? EFuyuSound::HeavySwing : EFuyuSound::Swing, GetActorLocation(), 0.6f);

	UAnimMontage* Montage = bHeavyAttack ? ChargedAttackMontage : ComboAttackMontage;
	if (bHeavyAttack)
	{
		DoAIChargedAttack();
	}
	else
	{
		DoAIComboAttack();
	}

	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		if (Montage && Anim->Montage_IsPlaying(Montage))
		{
			Anim->Montage_SetPlayRate(Montage, AttackRate);
		}
		else
		{
			// nothing to play, so do not leave the enemy stuck mid-attack
			HandleAttackFinished();
		}
	}
}

void AFuyuEnemy::HandleAttackFinished()
{
	bUnblockableAttack = false;
	ReleaseAttackToken();
	AttackCooldown = FMath::FRandRange(1.4f, 3.2f) / AttackRate;

	if (State == EFuyuEnemyState::Attack || State == EFuyuEnemyState::Windup)
	{
		SetState(EFuyuEnemyState::Recover);
	}
}

void AFuyuEnemy::DoAttackTrace(FName DamageSourceBone)
{
	if (State != EFuyuEnemyState::Attack)
	{
		return;
	}

	AFuyuSamurai* Player = Cast<AFuyuSamurai>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Player || Player->IsDead())
	{
		return;
	}

	const FVector To = Player->GetActorLocation() - GetActorLocation();
	const float Distance = To.Size2D();
	const float Reach = (AttackReach + 35.0f) * GetActorScale3D().X;
	if (Distance > Reach || FMath::Abs(To.Z) > 240.0f)
	{
		return;
	}

	if (Distance > 100.0f && FVector::DotProduct(To.GetSafeNormal2D(), GetActorForwardVector()) < 0.35f)
	{
		return;
	}

	const FVector Dir = To.GetSafeNormal2D();
	const FVector HitLocation = Player->GetActorLocation() - Dir * 30.0f + FVector(0, 0, 40.0f);
	const float Damage = AttackDamage * (bHeavyAttack ? 1.7f : 1.0f);
	const FVector Impulse = Dir * (bHeavyAttack ? 520.0f : 260.0f) + FVector::UpVector * (bHeavyAttack ? 330.0f : 240.0f);

	Player->ApplyDamage(Damage, this, HitLocation, Impulse);
}

void AFuyuEnemy::ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse)
{
	// damage arriving through the generic interface is treated as a plain light blow
	FFuyuStrike Strike;
	Strike.Attacker = DamageCauser;
	Strike.Location = DamageLocation;
	Strike.Direction = DamageImpulse.GetSafeNormal2D();
	Strike.Damage = Damage;
	Strike.PostureDamage = Damage * 0.5f;
	ReceiveStrike(Strike);
}

EFuyuStrikeResult AFuyuEnemy::ReceiveStrike(const FFuyuStrike& Strike)
{
	if (State == EFuyuEnemyState::Dead)
	{
		return EFuyuStrikeResult::Missed;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	const bool bWasStaggered = State == EFuyuEnemyState::Staggered;
	const bool bFromFront = FVector::DotProduct(-Strike.Direction, GetActorForwardVector()) > 0.2f;
	const bool bCounter = Strike.Stance == FuyuCombat::CounterStance(Type);
	const float PostureDamage = Strike.PostureDamage * (bCounter ? 2.1f : 1.0f);

	// being cut wakes up anyone who had not noticed the player
	if (State == EFuyuEnemyState::Idle || State == EFuyuEnemyState::Return)
	{
		SetState(EFuyuEnemyState::Circle);
	}

	LastPostureHitTime = Now;

	const bool bCanGuard = bFromFront && (State == EFuyuEnemyState::Circle || State == EFuyuEnemyState::Approach || State == EFuyuEnemyState::Recover);
	if (bCanGuard && FMath::FRand() < GuardChance)
	{
		if (ImpactFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ImpactFX, Strike.Location);
		}

		ShowGlint(false);
		LastDamageTaken = 0.0f;
		UFuyuAudio::Play(this, EFuyuSound::Guard, Strike.Location);
		GetCharacterMovement()->AddImpulse(Strike.Direction * 180.0f, true);

		// a guarded cut does no harm but still wears the guard down, and a heavy blow hammers it
		Posture += PostureDamage * (Strike.bHeavy ? 1.6f : 1.1f);
		if (Posture >= MaxPosture)
		{
			Posture = MaxPosture;
			PlayReaction(true);
			ReleaseAttackToken();
			SetState(EFuyuEnemyState::Staggered);
			Impact(0.12f);
			return EFuyuStrikeResult::Broken;
		}
		return EFuyuStrikeResult::Guarded;
	}

	const float Damage = Strike.Damage * (bWasStaggered ? 1.6f : 1.0f);
	CurrentHP -= Damage;
	Posture = FMath::Min(Posture + PostureDamage, MaxPosture);

	ReceivedDamage(Damage, Strike.Location, Strike.Direction);
	LastDamageTaken = Damage;

	if (CurrentHP <= 0.0f)
	{
		CurrentHP = 0.0f;
		Die(Strike.Direction * (Strike.bHeavy ? 900.0f : 520.0f) + FVector::UpVector * 240.0f, Strike.Location);

		if (AFuyuSamurai* Player = Cast<AFuyuSamurai>(Strike.Attacker))
		{
			Player->NotifyKill(this);
		}
		FlashTimeLeft = 0.14f;
		return EFuyuStrikeResult::Killed;
	}

	Impact(Strike.bHeavy ? 0.12f : 0.07f);

	if (LifeBarWidget)
	{
		LifeBarWidget->SetLifePercentage(CurrentHP / MaxHP);
	}

	const bool bBroken = Posture >= MaxPosture && !bWasStaggered;
	const bool bInterrupt = bFlinches || Strike.bHeavy || bBroken || bWasStaggered;

	if (bInterrupt)
	{
		if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
		{
			Anim->Montage_Stop(0.1f, ComboAttackMontage);
			Anim->Montage_Stop(0.1f, ChargedAttackMontage);
		}

		ReleaseAttackToken();
		GetCharacterMovement()->AddImpulse(Strike.Direction * (Strike.bHeavy ? 480.0f : 230.0f), true);
		PlayReaction(Strike.bHeavy || bBroken);

		if (bBroken)
		{
			SetState(EFuyuEnemyState::Staggered);
		}
		else if (!bWasStaggered)
		{
			SetState(EFuyuEnemyState::HitStun);
		}
	}

	return bBroken ? EFuyuStrikeResult::Broken : EFuyuStrikeResult::Hit;
}

void AFuyuEnemy::OnParried(AActor* By)
{
	if (State == EFuyuEnemyState::Dead)
	{
		return;
	}

	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		Anim->Montage_Stop(0.08f, ComboAttackMontage);
		Anim->Montage_Stop(0.08f, ChargedAttackMontage);
	}

	ReleaseAttackToken();
	LastPostureHitTime = GetWorld()->GetTimeSeconds();

	// a parry is the fastest way through a guard; the duelist alone can weather more than one
	Posture += MaxPosture * (Type == EFuyuEnemyType::Duelist ? 0.36f : 0.62f);
	PlayReaction(true);

	if (Posture >= MaxPosture)
	{
		Posture = MaxPosture;
		SetState(EFuyuEnemyState::Staggered);
	}
	else
	{
		SetState(EFuyuEnemyState::HitStun);
		StateTime = -0.45f;
	}
}

void AFuyuEnemy::PlayReaction(bool bHeavy)
{
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (!Anim || !ComboAttackMontage || ComboAttackMontage->SlotAnimTracks.Num() == 0)
	{
		return;
	}

	UAnimSequenceBase* Reaction = bHeavy ? HeavyReaction.Get() : (HitReactions.Num() > 0 ? HitReactions[FMath::RandHelper(HitReactions.Num())].Get() : nullptr);
	if (Reaction)
	{
		Anim->PlaySlotAnimationAsDynamicMontage(Reaction, ComboAttackMontage->SlotAnimTracks[0].SlotName, 0.06f, 0.2f, bHeavy ? 0.8f : 1.2f);
	}
}

void AFuyuEnemy::ShowGlint(bool bUnblockable)
{
	GlintTime = 0.0f;

	// white warns of a blow that can be parried, red of one that must be dodged
	const FLinearColor Color = bUnblockable ? FLinearColor(1.0f, 0.06f, 0.03f) : FLinearColor(1.0f, 0.95f, 0.85f);
	if (UMaterialInstanceDynamic* Dyn = Glint->CreateDynamicMaterialInstance(0))
	{
		Dyn->SetVectorParameterValue(TEXT("Tint"), Color);
	}

	GlintLight->SetLightColor(Color);
	Glint->SetVisibility(true);
}

void AFuyuEnemy::UpdateGlint(float DeltaSeconds)
{
	if (GlintTime < 0.0f)
	{
		return;
	}

	GlintTime += DeltaSeconds;
	const float Duration = 0.38f;
	if (GlintTime >= Duration)
	{
		GlintTime = -1.0f;
		Glint->SetVisibility(false);
		GlintLight->SetIntensity(0.0f);
		return;
	}

	const float Pulse = FMath::Sin(PI * GlintTime / Duration);
	Glint->SetWorldScale3D(FVector(0.04f + 0.3f * Pulse));
	GlintLight->SetIntensity(9000.0f * Pulse);
}

void AFuyuEnemy::Die(const FVector& Impulse, const FVector& Location)
{
	HandleDeath();

	// throw the ragdoll in the direction of the killing cut
	GetMesh()->AddImpulseAtLocation(Impulse * GetMesh()->GetMass() * 0.5f, Location);
}

void AFuyuEnemy::HandleDeath()
{
	if (State == EFuyuEnemyState::Dead)
	{
		return;
	}

	ReleaseAttackToken();
	State = EFuyuEnemyState::Dead;
	bUnblockableAttack = false;
	Glint->SetVisibility(false);
	GlintLight->SetIntensity(0.0f);

	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		Anim->Montage_Stop(0.05f);
	}

	Super::HandleDeath();

	if (AFuyuGameMode* GameMode = GetWorld()->GetAuthGameMode<AFuyuGameMode>())
	{
		GameMode->NotifyEnemyKilled(this);
	}
}

#undef LOCTEXT_NAMESPACE
