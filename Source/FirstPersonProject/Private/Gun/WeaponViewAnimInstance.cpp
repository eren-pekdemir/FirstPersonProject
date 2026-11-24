#include "Gun/WeaponViewAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "Gun/GunBase.h"

void FWeaponViewAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	if (const UWeaponViewAnimInstance* WeaponInstance = Cast<UWeaponViewAnimInstance>(InAnimInstance))
	{
		Parts = WeaponInstance->GetPartPoses();
	}
}

bool FWeaponViewAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();

	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	for (const FWeaponPartPose& Part : Parts)
	{
		if (Part.BoneIndex == INDEX_NONE || Part.Alpha <= 0.f)
		{
			continue;
		}

		const FCompactPoseBoneIndex PoseIndex = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Part.BoneIndex));
		if (PoseIndex != INDEX_NONE)
		{
			Output.Pose[PoseIndex].AddToTranslation(Part.LocalOffset * Part.Alpha);
		}
	}
	return true;
}

void UWeaponViewAnimInstance::SetupParts(const TArray<FWeaponPartKick>& InKicks)
{
	Kicks.Reset();
	PartPoses.Reset();

	const USkeletalMeshComponent* MeshComponent = GetSkelMeshComponent();
	const USkinnedAsset* Asset = MeshComponent ? MeshComponent->GetSkinnedAsset() : nullptr;
	if (!Asset)
	{
		return;
	}

	const FReferenceSkeleton& RefSkeleton = Asset->GetRefSkeleton();
	for (const FWeaponPartKick& Kick : InKicks)
	{
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(Kick.Bone);
		if (BoneIndex == INDEX_NONE)
		{
			continue;
		}

		// The kick is authored in mesh space, the pose is in the parent bone's space (which may be scaled)
		const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		const FTransform ParentComponent = ParentIndex == INDEX_NONE ? FTransform::Identity
			: FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, ParentIndex);

		FWeaponPartPose& Pose = PartPoses.AddDefaulted_GetRef();
		Pose.BoneIndex = BoneIndex;
		Pose.LocalOffset = ParentComponent.InverseTransformVector(Kick.Offset);
		Kicks.Add(Kick);
	}
}

void UWeaponViewAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	// The instance is recreated whenever the mesh reinitializes its animation: take the parts from the weapon again
	const AGunBase* Gun = Cast<AGunBase>(GetOwningActor());
	if (const UWeaponDataAsset* WeaponData = Gun ? Gun->GetWeaponData() : nullptr)
	{
		SetupParts(WeaponData->ViewPartKicks);
		bEmpty = Gun->GetAmmoInMagazine() <= 0;
	}
}

void UWeaponViewAnimInstance::KickParts()
{
	TimeSinceKick = 0.f;
}

float UWeaponViewAnimInstance::EvaluateKick(const FWeaponPartKick& Kick, float Time)
{
	Time -= Kick.Delay;
	if (Time < 0.f)
	{
		return 0.f;
	}

	if (Time < Kick.BackTime)
	{
		// Snaps back fast
		return FMath::InterpEaseOut(0.f, 1.f, Time / FMath::Max(Kick.BackTime, KINDA_SMALL_NUMBER), 2.f);
	}

	Time -= Kick.BackTime + Kick.HoldTime;
	if (Time < 0.f)
	{
		return 1.f;
	}

	// Springs forward again
	const float ReturnAlpha = Time / FMath::Max(Kick.ReturnTime, KINDA_SMALL_NUMBER);
	return ReturnAlpha >= 1.f ? 0.f : 1.f - FMath::InterpEaseIn(0.f, 1.f, ReturnAlpha, 2.f);
}

void UWeaponViewAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (TimeSinceKick < TNumericLimits<float>::Max())
	{
		TimeSinceKick += DeltaSeconds;
	}

	for (int32 Index = 0; Index < PartPoses.Num(); ++Index)
	{
		const FWeaponPartKick& Kick = Kicks[Index];
		float Alpha = EvaluateKick(Kick, TimeSinceKick);

		// A locked part stays open once it got there
		if (bEmpty && Kick.bLockOnEmpty && TimeSinceKick >= Kick.Delay + Kick.BackTime)
		{
			Alpha = 1.f;
		}
		PartPoses[Index].Alpha = Alpha;
	}
}

FAnimInstanceProxy* UWeaponViewAnimInstance::CreateAnimInstanceProxy()
{
	return new FWeaponViewAnimInstanceProxy(this);
}

void UWeaponViewAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}
