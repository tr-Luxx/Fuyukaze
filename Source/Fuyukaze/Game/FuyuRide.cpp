// Riding. E whistles for a horse, mounts one that is within reach (her own, or one taken from a herd), and gets
// down again. The host decides all of it; every machine then shows the horse and the rider from what it is told.

#include "FuyuSamurai.h"
#include "FuyuAudio.h"
#include "FuyuProps.h"
#include "FuyuRiderAnim.h"
#include "FuyuTerrain.h"
#include "FuyuWorld.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

namespace
{
	// the horse's back and the mannequin's hips, in centimetres above their feet, which is what seats one on the other
	const float HorseBackHeight = 148.0f;
	const float RiderHipHeight = 96.0f;

	// strides a second at full gallop, matching the horse material
	const float HorseStrideRate = 2.1f;
}

void AFuyuSamurai::SetupRide()
{
	UStaticMesh* HorseMesh = FuyuProps::Get(FuyuProps::EProp::Horse);
	if (!HorseMesh)
	{
		return;
	}

	// her horse is a single instance, because the horse material takes its stride from per-instance data
	Steed->SetStaticMesh(HorseMesh);
	Steed->SetNumCustomDataFloats(2);
	Steed->SetBoundsScale(1.6f);
	Steed->AddInstance(FTransform(FVector(0.0, 0.0, -100000.0)), true);
	const float Data[2] = { 0.0f, 0.0f };
	Steed->SetCustomData(0, MakeArrayView(Data, 2), true);
	Steed->SetVisibility(false);

	// a second body sits in the saddle while she rides, so her own animation is left alone for when she gets down
	Rider->SetSkeletalMeshAsset(GetMesh()->GetSkeletalMeshAsset());
	Rider->SetAnimInstanceClass(UFuyuRiderAnimInstance::StaticClass());
	Rider->SetRelativeRotation(GetMesh()->GetRelativeRotation());
	Rider->SetRelativeScale3D(GetMesh()->GetRelativeScale3D());
	Rider->SetVisibility(false);
}

FVector AFuyuSamurai::GetFeet() const
{
	return GetActorLocation() - FVector(0.0, 0.0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}

int32 AFuyuSamurai::GetHorsePrompt() const
{
	if (IsMounted())
	{
		return 2;
	}

	if ((HorseState == EFuyuHorse::Waiting || HorseState == EFuyuHorse::Coming) && FVector::Dist2D(HorseSpot, GetActorLocation()) < MountReach)
	{
		return 1;
	}

	int32 Index = INDEX_NONE;
	FTransform Where;
	const AFuyuWorld* World = AFuyuWorld::Get(this);
	return World && World->FindHerdHorse(GetActorLocation(), MountReach, Index, Where) ? 1 : 0;
}

void AFuyuSamurai::HorsePressed()
{
	if (IsDead() || IsUltimateActive() || !Steed || Steed->GetInstanceCount() == 0)
	{
		return;
	}

	// a whistled horse comes from behind the camera, so it arrives into view rather than appearing in it
	const FVector Behind = -FRotator(0.0f, GetControlRotation().Yaw, 0.0f).Vector();
	ServerHorse(Behind);
}

void AFuyuSamurai::ServerHorse_Implementation(FVector Behind)
{
	if (IsDead() || IsUltimateActive())
	{
		return;
	}

	if (IsMounted())
	{
		Dismount(true);
		return;
	}

	// her own horse, if it is standing by
	if ((HorseState == EFuyuHorse::Waiting || HorseState == EFuyuHorse::Coming) && FVector::Dist2D(HorseSpot, GetActorLocation()) < MountReach)
	{
		MountAt(HorseSpot, HorseYaw);
		return;
	}

	// one of a herd: it leaves the herd and is hers from now on
	int32 Index = INDEX_NONE;
	FTransform Where;
	const AFuyuWorld* World = AFuyuWorld::Get(this);
	if (World && World->FindHerdHorse(GetActorLocation(), MountReach, Index, Where))
	{
		TakenHorse = Index;
		MountAt(Where.GetLocation(), Where.Rotator().Yaw);
		return;
	}

	// nothing within reach, so whistle. The horse sets out from the first dry spot found behind her
	const FVector Feet = GetFeet();
	const FVector Back = Behind.GetSafeNormal2D(UE_SMALL_NUMBER, -GetActorForwardVector());
	FVector Start = Feet + Back * 900.0f;
	for (const float Turn : { 0.0f, 35.0f, -35.0f, 70.0f, -70.0f, 110.0f, -110.0f })
	{
		const FVector Try = Feet + Back.RotateAngleAxis(Turn, FVector::UpVector) * WhistleDistance;
		if (FuyuTerrain::Height(Try.X, Try.Y) > 120.0f)
		{
			Start = Try;
			break;
		}
	}

	HorseSpot = FVector(Start.X, Start.Y, FuyuTerrain::Height(Start.X, Start.Y));
	HorseYaw = (Feet - HorseSpot).Rotation().Yaw;
	HorseState = EFuyuHorse::Coming;
	HorseCalledAt = GetWorld()->GetTimeSeconds();
	UE_LOG(LogTemp, Display, TEXT("Fuyukaze: %s whistles and a horse sets out from %.0f m away"), *GetPlayerLabel(), FVector::Dist2D(HorseSpot, Feet) / 100.0f);
}

void AFuyuSamurai::MountAt(const FVector& Spot, float Yaw)
{
	// she goes to the horse, not the horse to her
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Seat(Spot.X, Spot.Y, FuyuTerrain::Height(Spot.X, Spot.Y) + HalfHeight + 4.0f);
	TeleportTo(Seat, FRotator(0.0f, Yaw, 0.0f), false, true);
	GetCharacterMovement()->StopMovementImmediately();

	if (bBlocking)
	{
		bBlocking = false;
		OnRep_Blocking();
	}

	HorseState = EFuyuHorse::Mounted;
	UE_LOG(LogTemp, Display, TEXT("Fuyukaze: %s mounts"), *GetPlayerLabel());
}

void AFuyuSamurai::Dismount(bool bStepAside)
{
	if (!IsMounted())
	{
		return;
	}

	// the horse stays where she leaves it
	const FVector Feet = GetFeet();
	HorseSpot = FVector(Feet.X, Feet.Y, FuyuTerrain::Height(Feet.X, Feet.Y));
	HorseYaw = GetActorRotation().Yaw;
	HorseState = EFuyuHorse::Waiting;

	// and she steps down on its near side, if there is ground there to step onto
	if (bStepAside)
	{
		const FVector Side = GetActorLocation() - GetActorRightVector() * 150.0f;
		if (FuyuTerrain::Height(Side.X, Side.Y) > 40.0f)
		{
			TeleportTo(FVector(Side.X, Side.Y, FuyuTerrain::Height(Side.X, Side.Y) + GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 4.0f), GetActorRotation(), false, true);
		}
	}
	GetCharacterMovement()->StopMovementImmediately();
	UE_LOG(LogTemp, Display, TEXT("Fuyukaze: %s dismounts"), *GetPlayerLabel());
}

void AFuyuSamurai::ShowRide(bool bRiding)
{
	bRideShown = bRiding;

	GetMesh()->SetVisibility(!bRiding, false);
	Rider->SetVisibility(bRiding, false);

	// the hat goes with whichever body is showing, and the sword is put away for the ride
	Hat->AttachToComponent(bRiding ? Rider.Get() : GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, TEXT("head"));
	Katana->SetVisibility(!bRiding);

	if (bRiding)
	{
		// the rider wears whatever she is wearing, including the colour that tells players apart
		for (int32 i = 0; i < GetMesh()->GetNumMaterials(); ++i)
		{
			Rider->SetMaterial(i, GetMesh()->GetMaterial(i));
		}
	}
}

void AFuyuSamurai::TickRide(float DeltaSeconds)
{
	if (!Steed || Steed->GetInstanceCount() == 0)
	{
		return;
	}

	const FVector Feet = GetFeet();

	// host: a whistled horse runs to her, and pulls up a few paces short
	if (HasAuthority() && HorseState == EFuyuHorse::Coming)
	{
		const FVector To = FVector(Feet.X - HorseSpot.X, Feet.Y - HorseSpot.Y, 0.0);
		const float Distance = To.Size();
		if (Distance < 310.0f || GetWorld()->GetTimeSeconds() - HorseCalledAt > 15.0f)
		{
			if (Distance >= 310.0f)
			{
				// it has been held up too long: let it be found just behind her
				const FVector Near = Feet - GetActorForwardVector() * 300.0f;
				HorseSpot = FVector(Near.X, Near.Y, FuyuTerrain::Height(Near.X, Near.Y));
			}
			HorseState = EFuyuHorse::Waiting;
		}
		else
		{
			const FVector Step = To / Distance * FMath::Min(Distance - 290.0f, GallopSpeed * 0.9f * DeltaSeconds);
			HorseSpot = FVector(HorseSpot.X + Step.X, HorseSpot.Y + Step.Y, FuyuTerrain::Height(HorseSpot.X + Step.X, HorseSpot.Y + Step.Y));
			HorseYaw = To.Rotation().Yaw;
		}
	}

	// every machine: show what the state says
	if (IsMounted() != bRideShown)
	{
		ShowRide(IsMounted());
	}

	if (HorseState != ShownHorseState)
	{
		if (HorseState == EFuyuHorse::Coming && IsLocallyControlled())
		{
			UFuyuAudio::Play(this, EFuyuSound::Whistle, GetActorLocation(), 0.8f);
		}
		ShownHorseState = HorseState;
	}

	if (HorseState == EFuyuHorse::None)
	{
		Steed->SetVisibility(false);
		return;
	}
	Steed->SetVisibility(true);

	FTransform Place;
	float Gait = 0.0f;
	if (IsMounted())
	{
		Place = FTransform(FRotator(0.0f, GetActorRotation().Yaw, 0.0f), Feet, FVector(SteedScale));
		Gait = GetCharacterMovement()->IsMovingOnGround() ? FMath::Clamp(GetVelocity().Size2D() / GallopSpeed, 0.0f, 1.0f) : 0.55f;
		ShownHorseSpot = Feet;
		ShownHorseYaw = GetActorRotation().Yaw;
	}
	else
	{
		// away from the host its position arrives a few times a second, so glide between the reports
		const bool bFar = FVector::Dist2D(ShownHorseSpot, HorseSpot) > 1200.0f;
		ShownHorseSpot = bFar ? FVector(HorseSpot) : FMath::VInterpTo(ShownHorseSpot, FVector(HorseSpot), DeltaSeconds, 12.0f);
		ShownHorseYaw = bFar ? HorseYaw : FMath::RInterpTo(FRotator(0.0f, ShownHorseYaw, 0.0f), FRotator(0.0f, HorseYaw, 0.0f), DeltaSeconds, 8.0f).Yaw;
		Place = FTransform(FRotator(0.0f, ShownHorseYaw, 0.0f), FVector(ShownHorseSpot.X, ShownHorseSpot.Y, FuyuTerrain::Height(ShownHorseSpot.X, ShownHorseSpot.Y)), FVector(SteedScale));
		Gait = HorseState == EFuyuHorse::Coming ? 1.0f : 0.0f;
	}

	ShownGait = FMath::FInterpTo(ShownGait, Gait, DeltaSeconds, 5.0f);
	Steed->UpdateInstanceTransform(0, Place, true, true, true);
	if (FMath::Abs(ShownGait - SentGait) > 0.01f)
	{
		SentGait = ShownGait;
		Steed->SetCustomDataValue(0, 1, ShownGait, true);
	}

	if (IsMounted())
	{
		// she rises and falls with the horse's stride, which the material times off the same clock
		const float Stride = GetWorld()->GetTimeSeconds() * HorseStrideRate;
		const float Bob = ShownGait * 7.0f * SteedScale * FMath::Sin(Stride * UE_TWO_PI + 0.6f);
		const float Seat = (HorseBackHeight + 9.0f) * SteedScale - RiderHipHeight - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Rider->SetRelativeLocation(FVector(-6.0f * SteedScale, 0.0f, Seat + Bob));

		// hooves drum twice a stride
		HoofTimer -= DeltaSeconds * FMath::Max(ShownGait, 0.0f);
		if (HoofTimer <= 0.0f && ShownGait > 0.2f && GetCharacterMovement()->IsMovingOnGround())
		{
			HoofTimer = 0.5f / HorseStrideRate;
			UFuyuAudio::Play(this, EFuyuSound::Hoof, Feet, 0.25f + 0.45f * ShownGait, FMath::FRandRange(-0.08f, 0.08f));
		}
	}
}
