#include "FuyuSamurai.h"
#include "FuyuAudio.h"
#include "FuyuEnemy.h"
#include "FuyuGameMode.h"
#include "FuyuHUD.h"
#include "FuyuProps.h"
#include "FuyuTerrain.h"
#include "FuyuWorld.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"

AFuyuSamurai::AFuyuSamurai()
{
	Katana = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Katana"));
	Katana->SetupAttachment(GetMesh(), TEXT("hand_r"));
	Katana->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Katana->SetRelativeLocationAndRotation(FVector(-8.5f, 3.5f, 1.0f), FRotator(-90.0f, 0.0f, 0.0f));

	Hat = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hat"));
	Hat->SetupAttachment(GetMesh(), TEXT("head"));
	Hat->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Hat->SetRelativeLocationAndRotation(FVector(19.0f, 2.0f, 0.0f), FRotator(-90.0f, 0.0f, 0.0f));
	Hat->SetRelativeScale3D(FVector(0.92f));

	MaxHP = SamuraiMaxHP;
	LifeBarColor = FLinearColor(0.75f, 0.1f, 0.08f);

	GetCharacterMovement()->MaxWalkSpeed = RunSpeed;
	GetCharacterMovement()->MaxAcceleration = 2200.0f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.0f;
	GetCharacterMovement()->SetWalkableFloorAngle(50.0f);
}

void AFuyuSamurai::BeginPlay()
{
	// the base class fills the health bar from MaxHP during its own BeginPlay
	MaxHP = SamuraiMaxHP;

	Super::BeginPlay();

	// health is shown on the HUD, so the floating bar from the template is not needed
	LifeBar->SetHiddenInGame(true);

	Katana->SetStaticMesh(FuyuProps::Get(FuyuProps::EProp::Katana));
	Hat->SetStaticMesh(FuyuProps::Get(FuyuProps::EProp::Kasa));

	DodgeAnim = LoadObject<UAnimSequenceBase>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Dash.MM_Dash"));
	GuardAnim = LoadObject<UAnimSequenceBase>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS.MF_Rifle_Idle_ADS"));
	ImpactFX = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Variant_Combat/VFX/NS_Damage.NS_Damage"));

	for (const TCHAR* Path : {
		TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Lgt_01.MM_HitReact_Front_Lgt_01"),
		TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Lgt_03.MM_HitReact_Front_Lgt_03"),
		TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Med_01.MM_HitReact_Front_Med_01") })
	{
		if (UAnimSequenceBase* Anim = LoadObject<UAnimSequenceBase>(nullptr, Path))
		{
			HitReactAnims.Add(Anim);
		}
	}

	GetCameraBoom()->TargetArmLength = 360.0f;
	GetCameraBoom()->SocketOffset = FVector(0.0f, 48.0f, 62.0f);
	GetCameraBoom()->CameraLagSpeed = 9.0f;
	GetCameraBoom()->CameraRotationLagSpeed = 16.0f;
	GetFollowCamera()->SetFieldOfView(78.0f);

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 640.0f, 0.0f);
	bUseControllerRotationYaw = false;

	LastDryLocation = GetActorLocation();
}

void AFuyuSamurai::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
			{
				Subsystem->RemoveMappingContext(FuyuMapping);
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

UInputAction* AFuyuSamurai::MakeAction(FName Name)
{
	UInputAction* Action = NewObject<UInputAction>(this, Name);
	Action->ValueType = EInputActionValueType::Boolean;
	FuyuActions.Add(Action);
	return Action;
}

void AFuyuSamurai::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		return;
	}

	// the game's own actions are created here rather than as assets, which keeps every binding in one readable table
	FuyuMapping = NewObject<UInputMappingContext>(this, TEXT("FuyuMapping"));
	FuyuActions.Reset();

	auto Map = [this](UInputAction* Action, std::initializer_list<FKey> Keys)
	{
		for (const FKey& Key : Keys)
		{
			FuyuMapping->MapKey(Action, Key);
		}
	};

	if (ComboAttackAction)
	{
		Map(ComboAttackAction, { EKeys::LeftMouseButton, EKeys::Gamepad_FaceButton_Left });
	}
	if (ChargedAttackAction)
	{
		Map(ChargedAttackAction, { EKeys::RightMouseButton, EKeys::Gamepad_FaceButton_Top });
	}

	UInputAction* Block = MakeAction(TEXT("IA_Block"));
	Map(Block, { EKeys::Q, EKeys::MiddleMouseButton, EKeys::Gamepad_LeftShoulder });
	Input->BindAction(Block, ETriggerEvent::Started, this, &AFuyuSamurai::BlockPressed);
	Input->BindAction(Block, ETriggerEvent::Completed, this, &AFuyuSamurai::BlockReleased);

	UInputAction* Dodge = MakeAction(TEXT("IA_Dodge"));
	Map(Dodge, { EKeys::LeftAlt, EKeys::C, EKeys::Gamepad_FaceButton_Right });
	Input->BindAction(Dodge, ETriggerEvent::Started, this, &AFuyuSamurai::DodgePressed);

	UInputAction* Sprint = MakeAction(TEXT("IA_Sprint"));
	Map(Sprint, { EKeys::LeftShift, EKeys::Gamepad_LeftThumbstick });
	Input->BindAction(Sprint, ETriggerEvent::Started, this, &AFuyuSamurai::SprintPressed);
	Input->BindAction(Sprint, ETriggerEvent::Completed, this, &AFuyuSamurai::SprintReleased);

	UInputAction* Lock = MakeAction(TEXT("IA_LockOn"));
	Map(Lock, { EKeys::Tab, EKeys::Gamepad_RightThumbstick });
	Input->BindAction(Lock, ETriggerEvent::Started, this, &AFuyuSamurai::LockPressed);

	UInputAction* Heal = MakeAction(TEXT("IA_Heal"));
	Map(Heal, { EKeys::F, EKeys::Gamepad_DPad_Down });
	Input->BindAction(Heal, ETriggerEvent::Started, this, &AFuyuSamurai::HealPressed);

	UInputAction* Wind = MakeAction(TEXT("IA_GuidingWind"));
	Map(Wind, { EKeys::G, EKeys::Gamepad_RightShoulder });
	Input->BindAction(Wind, ETriggerEvent::Started, this, &AFuyuSamurai::WindPressed);

	UInputAction* Cinema = MakeAction(TEXT("IA_CinemaMode"));
	Map(Cinema, { EKeys::K, EKeys::Gamepad_Special_Left });
	Input->BindAction(Cinema, ETriggerEvent::Started, this, &AFuyuSamurai::CinemaPressed);

	UInputAction* Stone = MakeAction(TEXT("IA_StanceStone"));
	Map(Stone, { EKeys::One, EKeys::Gamepad_DPad_Left });
	Input->BindAction(Stone, ETriggerEvent::Started, this, &AFuyuSamurai::StanceStone);

	UInputAction* WindStance = MakeAction(TEXT("IA_StanceWind"));
	Map(WindStance, { EKeys::Two, EKeys::Gamepad_DPad_Up });
	Input->BindAction(WindStance, ETriggerEvent::Started, this, &AFuyuSamurai::StanceWind);

	UInputAction* Moon = MakeAction(TEXT("IA_StanceMoon"));
	Map(Moon, { EKeys::Three, EKeys::Gamepad_DPad_Right });
	Input->BindAction(Moon, ETriggerEvent::Started, this, &AFuyuSamurai::StanceMoon);

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
			{
				Subsystem->AddMappingContext(FuyuMapping, 1);
			}
		}
	}
}

void AFuyuSamurai::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
}

float AFuyuSamurai::GetTimeSinceHit() const
{
	return GetWorld() ? GetWorld()->GetRealTimeSeconds() - LastHitTime : 100.0f;
}

float AFuyuSamurai::GetTimeSinceKill() const
{
	return GetWorld() ? GetWorld()->GetRealTimeSeconds() - LastKillTime : 100.0f;
}

void AFuyuSamurai::KickCamera(float Strength)
{
	CameraKick = FMath::Max(CameraKick, FMath::Clamp(Strength, 0.0f, 1.5f));

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->PlayDynamicForceFeedback(FMath::Clamp(Strength, 0.1f, 1.0f), 0.08f + 0.12f * Strength, true, true, true, true);
	}
}

void AFuyuSamurai::SlowMotion(float Dilation, float RealSeconds)
{
	UGameplayStatics::SetGlobalTimeDilation(this, Dilation);

	// timers run on the slowed clock, so ask for the dilated equivalent of the real duration
	GetWorldTimerManager().SetTimer(SlowMotionTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
	}), RealSeconds * Dilation, false);
}

void AFuyuSamurai::PlayStrikeFeedback(AFuyuEnemy* Enemy, const FFuyuStrike& Strike, EFuyuStrikeResult Result)
{
	AFuyuWorld* World = AFuyuWorld::Get(this);
	const float Now = GetWorld()->GetRealTimeSeconds();
	const FLinearColor StanceColor = FuyuCombat::StanceColor(Stance);

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
		Resolve = FMath::Min(Resolve + 0.25f, (float)MaxResolve);
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
			// a second, larger crescent crosses the first, and the fallen scatter into leaves
			World->SpawnSlash(Enemy->GetActorLocation() + FVector(0, 0, 25.0f), Strike.Direction, FMath::FRandRange(-60.0f, 60.0f), 2.1f, StanceColor);
			World->SpawnLeafBurst(Enemy->GetActorLocation() + FVector(0, 0, 30.0f), 46);
		}
	}

	KickCamera(Weight);

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
	const float Dealt = Enemy->GetLastDamageTaken();
	const bool bStrong = Strike.Stance == FuyuCombat::CounterStance(Enemy->GetEnemyType()) || Dealt > Strike.Damage * 1.1f;
	if (Dealt > 0.0f)
	{
		AFuyuHUD::Popup(this, Strike.Location + FVector(0, 0, 70.0f), FString::FromInt(FMath::RoundToInt32(Dealt)), bStrong ? FLinearColor(1.0f, 0.78f, 0.2f) : FLinearColor(0.96f, 0.93f, 0.86f), bStrong || Strike.bHeavy ? 30.0f : 22.0f);
	}

	if (bBreak)
	{
		AFuyuHUD::Popup(this, Enemy->GetActorLocation() + FVector(0, 0, 150.0f), TEXT("GUARD BROKEN"), FLinearColor(1.0f, 0.78f, 0.2f), 24.0f);
	}

	if (bKill)
	{
		LastKillTime = Now;
		UFuyuAudio::Play(this, EFuyuSound::Kill, Strike.Location);

		// the last foe standing falls in a longer, deeper slow motion
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

float AFuyuSamurai::GetTimeSinceParry() const
{
	return GetWorld() ? GetWorld()->GetRealTimeSeconds() - LastParryTime : 100.0f;
}

void AFuyuSamurai::DoMove(float Right, float Forward)
{
	if (GetController())
	{
		const FRotator YawRotation(0, GetController()->GetControlRotation().Yaw, 0);
		const FVector Fwd = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector Rgt = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		LastMoveInput = (Fwd * Forward + Rgt * Right).GetClampedToMaxSize(1.0f);
	}

	// a dodge is a commitment, the stick has no say until it ends
	if (IsDodging() || IsDead())
	{
		return;
	}

	Super::DoMove(Right, Forward);
}

void AFuyuSamurai::BlockPressed()
{
	if (IsDead() || IsDodging())
	{
		return;
	}

	bBlocking = true;
	BlockStartTime = GetWorld()->GetTimeSeconds();

	// raising the guard cuts short whatever swing was in progress
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		Anim->Montage_Stop(0.08f, ComboAttackMontage);
		Anim->Montage_Stop(0.08f, ChargedAttackMontage);

		if (GuardAnim && ComboAttackMontage && ComboAttackMontage->SlotAnimTracks.Num() > 0)
		{
			GuardMontage = Anim->PlaySlotAnimationAsDynamicMontage(GuardAnim, ComboAttackMontage->SlotAnimTracks[0].SlotName, 0.1f, 0.15f, 1.0f, 9999);
		}
	}
}

void AFuyuSamurai::BlockReleased()
{
	bBlocking = false;
	StopBlockPose();
}

void AFuyuSamurai::BotGuard(bool bGuard)
{
	if (bGuard != bBlocking)
	{
		if (bGuard)
		{
			BlockPressed();
		}
		else
		{
			BlockReleased();
		}
	}
}

void AFuyuSamurai::StopBlockPose()
{
	if (GuardMontage)
	{
		if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
		{
			Anim->Montage_Stop(0.15f, GuardMontage);
		}
		GuardMontage = nullptr;
	}
}

void AFuyuSamurai::DodgePressed()
{
	if (IsDead())
	{
		return;
	}

	// pressed during the tail of another dodge or its cooldown: remember it and go the moment it is allowed
	if (IsDodging() || DodgeCooldownLeft > 0.0f || GetCharacterMovement()->IsFalling())
	{
		Buffer(EBuffered::Dodge);
		return;
	}

	Buffered = EBuffered::None;
	FacingTimeLeft = 0.0f;
	LungeTimeLeft = 0.0f;
	bBlocking = false;
	StopBlockPose();

	// dodge where the stick points, or hop back from the fight if it is centred
	DodgeDirection = LastMoveInput.SizeSquared() > 0.04f ? LastMoveInput.GetSafeNormal2D() : -GetActorForwardVector().GetSafeNormal2D();
	DodgeTimeLeft = DodgeDuration;

	if (LastMoveInput.SizeSquared() > 0.04f)
	{
		SetActorRotation(DodgeDirection.Rotation());
	}

	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		Anim->Montage_Stop(0.05f, ComboAttackMontage);
		Anim->Montage_Stop(0.05f, ChargedAttackMontage);

		if (DodgeAnim && ComboAttackMontage && ComboAttackMontage->SlotAnimTracks.Num() > 0)
		{
			const float Rate = FMath::Max(DodgeAnim->GetPlayLength() / (DodgeDuration + 0.1f), 0.5f);
			Anim->PlaySlotAnimationAsDynamicMontage(DodgeAnim, ComboAttackMontage->SlotAnimTracks[0].SlotName, 0.06f, 0.14f, Rate);
		}
	}
}

void AFuyuSamurai::UpdateDodge(float DeltaSeconds)
{
	DodgeCooldownLeft = FMath::Max(DodgeCooldownLeft - DeltaSeconds, 0.0f);

	if (DodgeTimeLeft <= 0.0f)
	{
		return;
	}

	DodgeTimeLeft -= DeltaSeconds;

	// burst out fast, then bleed speed so the dodge settles rather than snapping to a halt
	const float Progress = 1.0f - FMath::Clamp(DodgeTimeLeft / DodgeDuration, 0.0f, 1.0f);
	const float Speed = DodgeSpeed * FMath::Lerp(1.0f, 0.3f, Progress * Progress);

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->Velocity = FVector(DodgeDirection.X * Speed, DodgeDirection.Y * Speed, Move->Velocity.Z);

	if (DodgeTimeLeft <= 0.0f)
	{
		DodgeTimeLeft = 0.0f;
		DodgeCooldownLeft = DodgeCooldown;
	}
}

void AFuyuSamurai::SprintPressed()
{
	bSprinting = true;
}

void AFuyuSamurai::SprintReleased()
{
	bSprinting = false;
}

void AFuyuSamurai::LockPressed()
{
	if (LockTarget.IsValid())
	{
		LockTarget = nullptr;
		return;
	}

	// favour whoever is nearest the middle of the screen, then whoever is closest
	const FVector ViewLocation = GetFollowCamera()->GetComponentLocation();
	const FVector ViewDirection = GetFollowCamera()->GetForwardVector();

	AFuyuEnemy* Best = nullptr;
	float BestScore = MAX_flt;

	for (TActorIterator<AFuyuEnemy> It(GetWorld()); It; ++It)
	{
		AFuyuEnemy* Enemy = *It;
		if (Enemy->IsDead())
		{
			continue;
		}

		const float Distance = FVector::Dist(Enemy->GetActorLocation(), GetActorLocation());
		if (Distance > LockRange)
		{
			continue;
		}

		const float Facing = FVector::DotProduct((Enemy->GetActorLocation() - ViewLocation).GetSafeNormal(), ViewDirection);
		if (Facing < 0.35f && Distance > 500.0f)
		{
			continue;
		}

		const float Score = (1.0f - Facing) * 2600.0f + Distance;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Enemy;
		}
	}

	LockTarget = Best;
}

void AFuyuSamurai::UpdateLockOn(float DeltaSeconds)
{
	AFuyuEnemy* Target = LockTarget.Get();
	if (!Target)
	{
		return;
	}

	if (Target->IsDead() || FVector::Dist(Target->GetActorLocation(), GetActorLocation()) > LockRange * 1.35f)
	{
		// when the target falls, slide the lock to the next enemy still in the fight
		LockTarget = nullptr;

		float BestDistance = 1400.0f;
		for (TActorIterator<AFuyuEnemy> It(GetWorld()); It; ++It)
		{
			const float Distance = FVector::Dist(It->GetActorLocation(), GetActorLocation());
			if (!It->IsDead() && It->IsAggressive() && Distance < BestDistance)
			{
				BestDistance = Distance;
				LockTarget = *It;
			}
		}
		return;
	}

	if (AController* PC = GetController())
	{
		// frame the fight: look at a point between the two fighters, from slightly above
		const FVector Focus = Target->GetActorLocation() + FVector(0, 0, 20.0f);
		FRotator Desired = (Focus - GetFollowCamera()->GetComponentLocation()).Rotation();
		Desired.Pitch = FMath::Clamp(Desired.Pitch - 6.0f, -35.0f, 20.0f);
		Desired.Roll = 0.0f;
		PC->SetControlRotation(FMath::RInterpTo(PC->GetControlRotation(), Desired, DeltaSeconds, 6.5f));
	}
}

void AFuyuSamurai::UpdateCamera(float DeltaSeconds)
{
	const bool bLocked = LockTarget.IsValid();
	const bool bMovingFast = bSprinting && GetVelocity().Size2D() > RunSpeed + 40.0f;

	float ArmLength = 360.0f;
	FVector Offset(0.0f, 48.0f, 62.0f);
	float FOV = 78.0f;

	if (IsDead())
	{
		ArmLength = DeathCameraDistance;
	}
	else if (bLocked)
	{
		ArmLength = 450.0f;
		Offset = FVector(0.0f, 30.0f, 95.0f);
	}
	else if (bBlocking)
	{
		ArmLength = 320.0f;
	}
	else if (bMovingFast)
	{
		ArmLength = 400.0f;
		FOV = 85.0f;
	}

	USpringArmComponent* Boom = GetCameraBoom();
	Boom->TargetArmLength = FMath::FInterpTo(Boom->TargetArmLength, ArmLength, DeltaSeconds, 4.0f);
	Boom->SocketOffset = FMath::VInterpTo(Boom->SocketOffset, Offset, DeltaSeconds, 4.0f);

	// impact kick: a quick rattle of the camera and a punch-in of the lens that settles in a fifth of a second.
	// It runs on real time so it still reads during hit-stop and slow motion
	const float RealDelta = GetWorld()->GetDeltaSeconds() / FMath::Max(UGameplayStatics::GetGlobalTimeDilation(this), 0.01f);
	const float RealTime = GetWorld()->GetRealTimeSeconds();
	CameraKick = FMath::Max(CameraKick - RealDelta * 5.0f, 0.0f);

	const float Rattle = CameraKick * CameraKick * 0.85f;
	GetFollowCamera()->SetRelativeRotation(FRotator(FMath::Sin(RealTime * 83.0f) * Rattle, FMath::Sin(RealTime * 67.0f + 1.7f) * Rattle, FMath::Sin(RealTime * 51.0f + 0.6f) * Rattle * 0.6f));

	const float BaseFOV = FMath::FInterpTo(SmoothedFOV, FOV, DeltaSeconds, 3.0f);
	SmoothedFOV = BaseFOV;
	GetFollowCamera()->SetFieldOfView(BaseFOV - 3.5f * CameraKick);
}

void AFuyuSamurai::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// hit-stop: freeze on impact for a few frames, timed in real seconds because this actor is the one slowed
	if (HitStopTimeLeft > 0.0f)
	{
		HitStopTimeLeft -= GetWorld()->GetDeltaSeconds();
		if (HitStopTimeLeft <= 0.0f)
		{
			CustomTimeDilation = 1.0f;
		}
	}

	if (IsDead())
	{
		UpdateCamera(DeltaSeconds);
		return;
	}

	UpdateDodge(DeltaSeconds);
	UpdateLockOn(DeltaSeconds);
	UpdateCamera(DeltaSeconds);

	// perform anything that was pressed a moment too early
	if (Buffered != EBuffered::None)
	{
		const EBuffered Action = Buffered;
		if (GetWorld()->GetRealTimeSeconds() - BufferedAt > InputBufferTime)
		{
			Buffered = EBuffered::None;
		}
		else if (Action == EBuffered::Light && (!IsDodging() || DodgeTimeLeft <= DodgeDuration * 0.45f))
		{
			DoComboAttackStart();
		}
		else if (Action == EBuffered::Dodge && !IsDodging() && DodgeCooldownLeft <= 0.0f && !GetCharacterMovement()->IsFalling())
		{
			DodgePressed();
		}
	}

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = bBlocking ? GuardSpeed : (bIsAttacking ? AttackMoveSpeed : (bSprinting ? SprintSpeed : RunSpeed));

	// while guarding or swinging, facing is driven here; otherwise the samurai turns towards where she is running
	const AFuyuEnemy* Target = LockTarget.Get();
	const bool bFaceTarget = Target && (bBlocking || bIsAttacking) && !IsDodging();
	Move->bOrientRotationToMovement = !bFaceTarget && !bIsAttacking;

	if (FacingTimeLeft > 0.0f && !IsDodging())
	{
		FacingTimeLeft -= DeltaSeconds;
		SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), FacingGoal, DeltaSeconds, 1500.0f));
	}
	else if (bFaceTarget)
	{
		const FRotator Desired = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D().Rotation();
		SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), Desired, DeltaSeconds, 720.0f));
	}

	if (LungeTimeLeft > 0.0f && !IsDodging())
	{
		LungeTimeLeft -= DeltaSeconds;

		const AFuyuEnemy* Lunged = LungeTarget.Get();
		const FVector To = Lunged ? Lunged->GetActorLocation() - GetActorLocation() : FVector::ZeroVector;
		if (!Lunged || To.Size2D() < 140.0f || LungeTimeLeft <= 0.0f)
		{
			LungeTimeLeft = 0.0f;
		}
		else
		{
			// full speed at first, bleeding off towards the end of the glide
			const float Ease = FMath::Clamp(LungeTimeLeft / 0.2f, 0.0f, 1.0f);
			const FVector Glide = To.GetSafeNormal2D() * LungeSpeed * FMath::Lerp(0.35f, 1.0f, Ease);
			Move->Velocity = FVector(Glide.X, Glide.Y, Move->Velocity.Z);
		}
	}

	// the terrain only has collision inside the playable square, so turn the samurai back at its edge
	const float Limit = FuyuTerrain::HalfSize - 1500.0f;
	if (FMath::Abs(GetActorLocation().X) > Limit || FMath::Abs(GetActorLocation().Y) > Limit)
	{
		FVector Clamped = GetActorLocation();
		Clamped.X = FMath::Clamp(Clamped.X, -Limit, Limit);
		Clamped.Y = FMath::Clamp(Clamped.Y, -Limit, Limit);
		SetActorLocation(Clamped);
	}

	// remember dry land, and step back onto it if the samurai wades out of her depth
	const FVector Location = GetActorLocation();
	const float Feet = Location.Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (Move->IsMovingOnGround() && Feet > 40.0f)
	{
		LastDryLocation = Location;
	}
	else if (Feet < -95.0f)
	{
		TeleportTo(LastDryLocation + FVector(0, 0, 20.0f), GetActorRotation(), false, true);
		Move->StopMovementImmediately();
	}
}

AFuyuEnemy* AFuyuSamurai::FindAttackTarget(float MaxDistance) const
{
	if (AFuyuEnemy* Locked = LockTarget.Get())
	{
		if (!Locked->IsDead() && FVector::Dist(Locked->GetActorLocation(), GetActorLocation()) < MaxDistance * 1.4f)
		{
			return Locked;
		}
	}

	// with no lock, read intent from the stick first and the character's facing second
	const FVector Intent = LastMoveInput.SizeSquared() > 0.04f ? LastMoveInput.GetSafeNormal2D() : GetActorForwardVector();

	AFuyuEnemy* Best = nullptr;
	float BestScore = MAX_flt;

	for (TActorIterator<AFuyuEnemy> It(GetWorld()); It; ++It)
	{
		if (It->IsDead())
		{
			continue;
		}

		const FVector To = It->GetActorLocation() - GetActorLocation();
		const float Distance = To.Size2D();
		if (Distance > MaxDistance)
		{
			continue;
		}

		const float Facing = FVector::DotProduct(To.GetSafeNormal2D(), Intent);
		if (Facing < 0.1f && Distance > 180.0f)
		{
			continue;
		}

		const float Score = Distance + (1.0f - Facing) * 300.0f;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = *It;
		}
	}

	return Best;
}

void AFuyuSamurai::SnapToTarget()
{
	AFuyuEnemy* Target = FindAttackTarget(520.0f);
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

void AFuyuSamurai::Buffer(EBuffered Action)
{
	Buffered = Action;
	BufferedAt = GetWorld()->GetRealTimeSeconds();
}

void AFuyuSamurai::DoComboAttackStart()
{
	if (IsDead())
	{
		return;
	}

	if (IsDodging())
	{
		// the back half of a dodge can be cut short by an attack; earlier than that the press is remembered
		if (DodgeTimeLeft > DodgeDuration * 0.45f)
		{
			Buffer(EBuffered::Light);
			return;
		}

		DodgeTimeLeft = 0.0f;
		DodgeCooldownLeft = DodgeCooldown;
	}

	Buffered = EBuffered::None;

	bBlocking = false;
	StopBlockPose();

	const bool bWasAttacking = bIsAttacking;
	if (!bIsAttacking)
	{
		HitThisSwing.Reset();
		SnapToTarget();
	}

	Super::DoComboAttackStart();

	if (!bWasAttacking && bIsAttacking)
	{
		UFuyuAudio::Play(this, EFuyuSound::Swing, GetActorLocation(), 0.7f);

		if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
		{
			Anim->Montage_SetPlayRate(ComboAttackMontage, ComboPlayRate);
		}
	}
}

void AFuyuSamurai::DoChargedAttackStart()
{
	if (IsDead() || IsDodging())
	{
		return;
	}

	bBlocking = false;
	StopBlockPose();

	if (!bIsAttacking)
	{
		HitThisSwing.Reset();
		bHeavySwingSounded = false;
		SnapToTarget();
	}

	Super::DoChargedAttackStart();
}

void AFuyuSamurai::CheckCombo()
{
	const int32 Before = ComboCount;
	Super::CheckCombo();

	// each new cut in the string may hit again and re-aims at the target
	if (ComboCount != Before)
	{
		HitThisSwing.Reset();
		SnapToTarget();
		UFuyuAudio::Play(this, EFuyuSound::Swing, GetActorLocation(), 0.7f);
	}
}

void AFuyuSamurai::DoAttackTrace(FName DamageSourceBone)
{
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	const bool bHeavy = Anim && Anim->Montage_IsPlaying(ChargedAttackMontage);

	// the charged montage fires several trace notifies for one swing, the combo fires one per cut
	if (bHeavy && HitThisSwing.Num() == 0)
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

	const FVector Origin = GetActorLocation();
	const FVector Forward = GetActorForwardVector();
	const float ArcCos = FMath::Cos(FMath::DegreesToRadians(SwordArc));
	bool bHitAny = false;

	for (TActorIterator<AFuyuEnemy> It(GetWorld()); It; ++It)
	{
		AFuyuEnemy* Enemy = *It;
		if (Enemy->IsDead() || HitThisSwing.Contains(Enemy))
		{
			continue;
		}

		const FVector To = Enemy->GetActorLocation() - Origin;
		const float Distance = To.Size2D();
		const float Reach = SwordReach + Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius();
		if (Distance > Reach || FMath::Abs(To.Z) > 220.0f)
		{
			continue;
		}

		if (Distance > 110.0f && FVector::DotProduct(To.GetSafeNormal2D(), Forward) < ArcCos)
		{
			continue;
		}

		FFuyuStrike Strike;
		Strike.Attacker = this;
		Strike.Direction = To.GetSafeNormal2D();
		Strike.Location = Enemy->GetActorLocation() - Strike.Direction * 30.0f + FVector(0, 0, 40.0f);
		Strike.bHeavy = bHeavy;
		Strike.Stance = Stance;
		Strike.Damage = bHeavy ? HeavyDamage : LightDamage;
		Strike.PostureDamage = bHeavy ? HeavyPostureDamage : LightPostureDamage;

		HitThisSwing.Add(Enemy);
		const EFuyuStrikeResult Result = Enemy->ReceiveStrike(Strike);
		if (Result == EFuyuStrikeResult::Missed)
		{
			continue;
		}

		PlayStrikeFeedback(Enemy, Strike, Result);
		DealtDamage(Strike.Damage, Strike.Location);
		bHitAny = true;
	}

	if (bHitAny)
	{
		// freeze on the frame of impact; the enemy freezes itself for the same beat
		CustomTimeDilation = 0.04f;
		HitStopTimeLeft = bHeavy ? 0.12f : 0.07f;
	}
}

void AFuyuSamurai::ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse)
{
	if (IsDead())
	{
		return;
	}

	// blows pass through the opening frames of a dodge
	if (IsDodging() && (DodgeDuration - DodgeTimeLeft) < DodgeInvulnerableTime)
	{
		return;
	}

	AFuyuEnemy* Enemy = Cast<AFuyuEnemy>(DamageCauser);
	const bool bUnblockable = Enemy && Enemy->IsAttackUnblockable();

	bool bFromFront = true;
	if (DamageCauser)
	{
		const FVector ToAttacker = (DamageCauser->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		bFromFront = FVector::DotProduct(ToAttacker, GetActorForwardVector()) > -0.1f;

		// with no lock-on the guard still turns to meet the attacker, as long as they are not behind
		if (bBlocking && bFromFront)
		{
			SetActorRotation(ToAttacker.Rotation());
		}
	}

	if (bBlocking && bFromFront && !bUnblockable)
	{
		const float GuardHeld = GetWorld()->GetTimeSeconds() - BlockStartTime;

		if (ImpactFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ImpactFX, DamageLocation);
		}

		if (GuardHeld <= ParryWindow)
		{
			// a perfect parry: the blow is turned aside, time hangs for a beat and the attacker is left open
			LastParryTime = GetWorld()->GetRealTimeSeconds();
			Resolve = FMath::Min(Resolve + 0.34f, (float)MaxResolve);
			UFuyuAudio::Play(this, EFuyuSound::Clang, DamageLocation);

			if (Enemy)
			{
				Enemy->OnParried(this);
			}

			SlowMotion(0.25f, 0.28f);
			KickCamera(0.6f);
			if (AFuyuWorld* World = AFuyuWorld::Get(this))
			{
				World->SpawnSparks(DamageLocation, FVector::UpVector, FLinearColor(1.0f, 0.95f, 0.75f), 30, 950.0f);
				World->PunchScreen(0.7f);
			}
		}
		else
		{
			// an ordinary block holds, but shoves the samurai back
			GetCharacterMovement()->AddImpulse(DamageImpulse.GetSafeNormal2D() * 260.0f, true);
			UFuyuAudio::Play(this, EFuyuSound::Guard, DamageLocation);
		}

		return;
	}

	bBlocking = false;
	StopBlockPose();

	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		Anim->Montage_Stop(0.1f, ComboAttackMontage);
		Anim->Montage_Stop(0.1f, ChargedAttackMontage);
	}

	// being cut ends the chain
	HitChain = 0;
	KickCamera(0.8f);
	if (AFuyuWorld* World = AFuyuWorld::Get(this))
	{
		World->PunchScreen(0.6f);
		World->SpawnSparks(DamageLocation, DamageImpulse.GetSafeNormal(), FLinearColor(1.0f, 0.2f, 0.1f), 14, 600.0f);
	}

	UFuyuAudio::Play(this, EFuyuSound::Hit, DamageLocation);

	Buffered = EBuffered::None;
	FacingTimeLeft = 0.0f;
	LungeTimeLeft = 0.0f;

	// the template launches a struck character into the air and blends in ragdoll physics until it lands,
	// which takes control away for a long, floaty moment. Here the samurai keeps her feet: she is shoved
	// back along the ground and flinches, and can act again almost at once
	FDamageEvent DamageEvent;
	TakeDamage(Damage, DamageEvent, nullptr, DamageCauser);

	if (!IsDead())
	{
		GetMesh()->SetPhysicsBlendWeight(0.0f);
		GetCharacterMovement()->AddImpulse(DamageImpulse.GetSafeNormal2D() * FMath::Min(DamageImpulse.Size2D(), 420.0f), true);

		UAnimInstance* Anim = GetMesh()->GetAnimInstance();
		if (Anim && HitReactAnims.Num() > 0 && ComboAttackMontage && ComboAttackMontage->SlotAnimTracks.Num() > 0)
		{
			Anim->PlaySlotAnimationAsDynamicMontage(HitReactAnims[FMath::RandHelper(HitReactAnims.Num())], ComboAttackMontage->SlotAnimTracks[0].SlotName, 0.05f, 0.18f, 1.5f);
		}
	}
	else
	{
		GetMesh()->AddImpulseAtLocation(DamageImpulse * GetMesh()->GetMass(), DamageLocation);
	}
}

void AFuyuSamurai::HandleDeath()
{
	LockTarget = nullptr;
	bBlocking = false;
	bSprinting = false;
	DodgeTimeLeft = 0.0f;
	CustomTimeDilation = 1.0f;
	UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);

	Super::HandleDeath();
}

void AFuyuSamurai::HealPressed()
{
	if (IsDead() || Resolve < 1.0f || CurrentHP >= MaxHP)
	{
		return;
	}

	Resolve -= 1.0f;
	CurrentHP = FMath::Min(CurrentHP + MaxHP * HealFraction, MaxHP);
	UFuyuAudio::Play(this, EFuyuSound::Heal, GetActorLocation());
}

void AFuyuSamurai::Restore(float HealthFraction, float ResolveGain)
{
	if (!IsDead())
	{
		CurrentHP = FMath::Min(CurrentHP + MaxHP * HealthFraction, MaxHP);
		Resolve = FMath::Min(Resolve + ResolveGain, (float)MaxResolve);
	}
}

void AFuyuSamurai::NotifyKill(AFuyuEnemy* Enemy)
{
	Resolve = FMath::Min(Resolve + 0.5f, (float)MaxResolve);
}

void AFuyuSamurai::WindPressed()
{
	AFuyuWorld* World = AFuyuWorld::Get(this);
	AFuyuGameMode* GameMode = GetWorld()->GetAuthGameMode<AFuyuGameMode>();
	if (World && GameMode)
	{
		World->BlowGuidingWind(GameMode->GetObjectiveLocation());
	}
}

void AFuyuSamurai::CinemaPressed()
{
	if (AFuyuWorld* World = AFuyuWorld::Get(this))
	{
		World->SetCinemaMode(!World->IsCinemaMode());
	}
}

void AFuyuSamurai::StanceStone()
{
	SetStance(EFuyuStance::Stone);
}

void AFuyuSamurai::StanceWind()
{
	SetStance(EFuyuStance::Wind);
}

void AFuyuSamurai::StanceMoon()
{
	SetStance(EFuyuStance::Moon);
}

void AFuyuSamurai::SetStance(EFuyuStance NewStance)
{
	Stance = NewStance;
}
