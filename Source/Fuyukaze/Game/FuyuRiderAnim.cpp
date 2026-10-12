#include "FuyuRiderAnim.h"
#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "BonePose.h"

bool FFuyuRiderProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();

	FCompactPose& Pose = Output.Pose;
	const FBoneContainer& Bones = Pose.GetBoneContainer();

	auto Find = [&Bones](const TCHAR* Name)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
	};

	// a bone's transform in the mesh's own space: its local transform carried up through each parent in turn
	auto InMesh = [&Pose, &Bones](FCompactPoseBoneIndex Bone)
	{
		FTransform Result = Pose[Bone];
		for (FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(Bone); Parent.IsValid(); Parent = Bones.GetParentBoneIndex(Parent))
		{
			Result = Result * Pose[Parent];
		}
		return Result;
	};

	// Turns a bone so that it points from itself towards its child along Direction (in mesh space).
	// Working from where the bones are, rather than from their axes, means it does not matter how the skeleton was built
	auto Aim = [&](const TCHAR* BoneName, const TCHAR* ChildName, const FVector& Direction)
	{
		const FCompactPoseBoneIndex Bone = Find(BoneName), Child = Find(ChildName);
		if (!Bone.IsValid() || !Child.IsValid())
		{
			return;
		}

		const FTransform Current = InMesh(Bone);
		const FVector Pointing = (InMesh(Child).GetLocation() - Current.GetLocation()).GetSafeNormal();
		const FQuat Turn = FQuat::FindBetweenNormals(Pointing, Direction.GetSafeNormal());

		const FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(Bone);
		const FQuat ParentRotation = Parent.IsValid() ? InMesh(Parent).GetRotation() : FQuat::Identity;
		Pose[Bone].SetRotation((ParentRotation.Inverse() * (Turn * Current.GetRotation())).GetNormalized());
	};

	// the mannequin faces along +Y with +Z up. Which way is "out" for each leg is read from where its hip sits
	const FCompactPoseBoneIndex LeftHip = Find(TEXT("thigh_l"));
	if (!LeftHip.IsValid())
	{
		return true;
	}
	const float LeftSide = InMesh(LeftHip).GetLocation().X >= 0.0 ? 1.0f : -1.0f;

	const FVector Forward(0.0, 1.0, 0.0), Up(0.0, 0.0, 1.0);
	const TCHAR* Names[2][7] = {
		{ TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("ball_l"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l") },
		{ TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), TEXT("ball_r"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r") } };

	for (int32 i = 0; i < 2; ++i)
	{
		const FVector Out(i == 0 ? LeftSide : -LeftSide, 0.0, 0.0);

		// thighs wrap the horse's barrel, shins hang down its sides with the heels a little back and the toes forward
		Aim(Names[i][0], Names[i][1], Out * 0.62 + Forward * 0.45 - Up * 0.64);
		Aim(Names[i][1], Names[i][2], Out * 0.06 - Forward * 0.28 - Up * 0.96);
		Aim(Names[i][2], Names[i][3], Out * 0.15 + Forward * 0.95 - Up * 0.25);

		// upper arms hang by the ribs, forearms reach forward and in to hold the reins
		Aim(Names[i][4], Names[i][5], Out * 0.2 + Forward * 0.3 - Up * 0.93);
		Aim(Names[i][5], Names[i][6], Out * -0.28 + Forward * 0.86 - Up * 0.42);
	}

	return true;
}
